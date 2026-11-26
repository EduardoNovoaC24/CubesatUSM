"""Run: python3 -m unittest -v test_web.py"""
import io
import json
import threading
import unittest
from http.server import ThreadingHTTPServer
from urllib.request import Request, urlopen
from urllib.error import HTTPError

from pico_bridge import PicoBridge, parse_telemetry, validate_command
from radar_stm32 import SharedControl, State, make_handler, parse_aircraft


class DecoderTests(unittest.TestCase):
    def test_adsb_complete(self):
        a = parse_aircraft('$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810')
        self.assertEqual(a['icao'],'E8032E')
        self.assertEqual(a['lat'],-33.472)

    def test_telemetry_complete(self):
        a = parse_telemetry('TEL,14,15984,12,-10,992,0,1,-2,113,-65,80,10000,0,0,0,1')
        self.assertEqual((a['ax'],a['az'],a['q0']), (12,992,1.0))
        self.assertAlmostEqual(a['heading'],330.09,places=1)

    def test_quaternion_only(self):
        a = parse_telemetry('Q,+10000,+00000,+00000,+00000')
        self.assertIsNone(a['ax'])
        self.assertIsNone(a['heading'])

    def test_solar_record_persists_between_attitude_frames(self):
        bridge = PicoBridge(None)
        bridge._handle_payload('Q,10000,0,0,0|SOLAR,2048,9659')
        bridge._handle_payload('Q,10000,0,0,0')
        self.assertAlmostEqual(bridge.snapshot()['solar']['voltage'], 9.659)
        bridge._handle_payload('QERR,NO_DATA|SOLAR,4095,19315')
        self.assertTrue(bridge.snapshot()['solar']['saturated'])
        self.assertIsNone(bridge.snapshot()['solar_history'][-1]['voltage'])
        bridge._handle_payload('QERR,NO_DATA|SOLAR,NA,NA')
        self.assertIsNone(bridge.snapshot()['solar']['adc'])

    def test_eps_four_channels_with_independent_validity_and_saturation(self):
        bridge = PicoBridge(None)
        bridge._handle_payload('Q,10000,0,0,0|EPS,13,4,7100,0,19300,15950')
        bridge._handle_payload('Q,10000,0,0,0')
        state = bridge.snapshot()
        channels = state['solar']['channels']
        self.assertEqual([channel['voltage'] for channel in channels], [7.1,None,19.3,15.95])
        self.assertTrue(channels[2]['saturated'])
        self.assertIsNone(state['solar_history'][-1]['pair3'])
        self.assertEqual(state['solar_history'][-1]['battery'], 15.95)

    def test_eps_ina_line_does_not_infer_battery_voltage(self):
        bridge = PicoBridge(None)
        bridge._handle_payload('Q,10000,0,0,0|EPSI,15,1,7100,-1200,0')
        solar = bridge.snapshot()['solar']
        self.assertEqual(solar['current_ma'], -1.2)
        self.assertIsNone(solar['channels'][3]['voltage'])
        bridge._handle_payload('Q,10000,0,0,0|EPSI,16,3,7100,1200,15950')
        self.assertEqual(bridge.snapshot()['solar']['channels'][3]['voltage'], 15.95)

    def test_magnetometer_status_preserves_uncalibrated_readings(self):
        frame = 'TEL,14,15984,12,-10,992,0,1,-2,113,-65,80,10000,0,0,0,0'
        active = parse_telemetry(frame + '|MAGSTAT,1,1,0')
        self.assertEqual((active['mx'], active['my'], active['mz']), (113, -65, 80))
        self.assertTrue(active['mag_detected'])
        self.assertFalse(active['mag_calibrated'])
        self.assertIsNone(active['heading'])
        invalid = parse_telemetry(frame + '|MAGSTAT,1,0,1')
        self.assertIsNone(invalid['mx'])

    def test_quaternion_update_keeps_last_sensor_readings(self):
        b = PicoBridge(None)
        b._handle_payload('TEL,14,15984,12,-10,992,0,1,-2,113,-65,80,10000,0,0,0,0|MAGSTAT,1,1,0')
        b._handle_payload('Q,0,10000,0,0')
        state = b.snapshot()
        self.assertEqual((state['latest']['ax'], state['latest']['mx'], state['latest']['q1']),
                         (12, 113, 1.0))
        self.assertIsNotNone(state['latest']['sensor_age'])
        self.assertIsNone(state['history'][-1]['ax'])

    def test_wheel_binary_usb_record_and_limits(self):
        b = PicoBridge(None)
        b._handle_payload('Q,10000,0,0,0|WHEEL,2,3,1476,-5,65535')
        wheel = b.snapshot()['wheel']
        self.assertEqual((wheel['profile'], wheel['pulse'], wheel['level']), (2,1476,-5))
        self.assertIsNone(wheel['rpm'])
        b._handle_payload('QERR,NO_DATA|WHEEL,2,4,1500,0,600')
        self.assertEqual(b.snapshot()['wheel']['rpm'], 600)
        self.assertEqual(validate_command('WHEEL', ['+5']), 'WHEEL,5')
        b._handle_payload('Q,10000,0,0,0|WHEEL,2,3,1505,1,65535|ADCS,6,1,-6000,125,243')
        self.assertEqual(validate_command('WHEEL', ['DETUMBLE']), 'WHEEL,DETUMBLE')
        snapshot = b.snapshot()
        self.assertEqual(snapshot['wheel']['body_rpm'], -60)
        self.assertEqual(snapshot['wheel']['axis'], 2)
        self.assertTrue(snapshot['wheel']['inverted'])
        self.assertEqual(snapshot['wheel']['leds'], 3)
        self.assertTrue(snapshot['wheel']['fusion_9dof'])
        self.assertEqual(snapshot['adcs_history'][-1]['elapsed_s'], 1.25)
        b._handle_payload('QERR,NO_DATA|WHEEL,2,4,1500,0,65535|ADCS,2,4,-32768,6000,0')
        self.assertIsNone(b.snapshot()['wheel']['body_rpm'])
        b._handle_payload('QERR,NO_DATA|WHEEL,2,0,0,0,65535|ADCS,3,0,-32768,0,64|LIGHT,13,0,2000,4095,256')
        light = b.snapshot()['wheel']
        self.assertEqual(tuple(light[f'light_{i}'] for i in range(1,5)), (0,None,4095,256))
        self.assertEqual(b.snapshot()['adcs_history'][-1]['light_3'], 4095)
        b._handle_payload('QERR,NO_DATA|WHEEL,2,3,1510,2,600|ADCS,2,1,100,200,16'
                          '|LIGHT,15,2100,2000,2000,2000|MAN,3,-35,-32768,900,875')
        self.assertEqual(b.snapshot()['wheel']['rotation_deg'], -3.5)
        self.assertIsNone(b.snapshot()['wheel']['target_deg'])
        self.assertEqual(b.snapshot()['adcs_history'][-1]['light_azimuth_deg'], 87.5)
        for action in ('PROFILE_AIR', 'PROFILE_BIDIR', 'ENABLE', 'TURN:-45',
                       'AXIS_Z-', 'LIGHT:0', 'LIGHTINV:359'):
            self.assertEqual(validate_command('WHEEL', [action]), 'WHEEL,' + action)
        for invalid in ('16', '0', '1500', 'PROFILE', 'TURN:0', 'TURN:-181', 'LIGHT:360', 'AXIS_A+'):
            with self.assertRaises(ValueError):
                validate_command('WHEEL', [invalid])

    def test_no_synthetic_imu(self):
        self.assertIsNone(parse_telemetry('TEST_LORA'))

    def test_safe_command_validation(self):
        self.assertEqual(validate_command('SET_BETA',['0.1']),'SET_BETA,0.100')
        with self.assertRaises(ValueError): validate_command('SET_RATE',['50'])
        with self.assertRaises(ValueError): validate_command('POWER_OFF',[])

    def test_pico_commands_locked_until_handshake(self):
        b=PicoBridge(None)
        with self.assertRaises(RuntimeError):b.send_command('PING',[])
        b._parse_line('PICO_READY,P2P_CMD_V1')
        with self.assertRaises(RuntimeError):b.send_command('PING',[])
        b.connected=True
        self.assertEqual(b.send_command('PING',[])['state'],'EN COLA')
        b._parse_line('PICO_TX,1,SENT')
        self.assertNotEqual(b.snapshot()['commands'][-1]['state'],'OK')
        b._parse_line('ACK,1,OK,PONG')
        self.assertEqual(b.snapshot()['commands'][-1]['state'],'OK')

    def test_relay_status_requires_stm32_ack(self):
        b = PicoBridge(None)
        b.connected = True
        b._parse_line('PICO_READY,P2P_CMD_V1')
        with self.assertRaises(ValueError):
            b.send_command('RELAY', ['TOGGLE'])
        b.send_command('RELAY', ['ON'])
        self.assertEqual(b.queue.get_nowait(), 'CMD,1,RELAY,ON\n')
        b._parse_line('PICO_TX,1,SENT')
        self.assertEqual(b.snapshot()['relay'], 'PENDING')
        b._parse_line('PICO_ACK_TIMEOUT,1,UNKNOWN')
        self.assertEqual(b.snapshot()['relay'], 'UNKNOWN')
        b._parse_line('ACK,1,OK,RELAY,ON')
        self.assertEqual(b.snapshot()['relay'], 'ON')
        b.send_command('RELAY', ['STATUS'])
        b._parse_line('ACK,1,OK,RELAY,ON')
        self.assertEqual(b.snapshot()['relay'], 'PENDING')
        b._parse_line('ACK,2,OK,RELAY,OFF')
        self.assertEqual(b.snapshot()['relay'], 'OFF')

    def test_web_endpoints(self):
        b=PicoBridge(None)
        server=ThreadingHTTPServer(('127.0.0.1',0),make_handler(State(demo=True),b))
        th=threading.Thread(target=server.serve_forever,daemon=True);th.start()
        try:
            port=server.server_port
            for path in ['/','/assets/cubesat_web.css','/assets/cubesat_web.js',
                         '/assets/usm-cubesat-logo.svg','/api/aircraft','/api/pico']:
                with urlopen(f'http://127.0.0.1:{port}{path}',timeout=2) as response:
                    self.assertEqual(response.status,200)
                    self.assertTrue(response.read())
            req=Request(f'http://127.0.0.1:{port}/api/pico/command',
                        data=json.dumps({'command':'PING','args':[]}).encode(),
                        headers={'Content-Type':'application/json'}, method='POST')
            with self.assertRaises(HTTPError) as exc:urlopen(req,timeout=2)
            self.assertEqual(exc.exception.code,400)
        finally:
            server.shutdown()
            server.server_close()

    def test_shared_radio_uses_one_queue_and_tracks_operators(self):
        pico = PicoBridge(None)
        pico.connected = True
        pico._parse_line('PICO_READY,P2P_CMD_V1')
        shared = SharedControl('127.0.0.1')
        handler = make_handler(State(demo=False), pico, shared)
        with ThreadingHTTPServer(('127.0.0.1', 0), handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            address = f'http://127.0.0.1:{server.server_port}'

            def submit(client, operator, name, args, key=shared.key, path='/api/pico/command'):
                payload = {'operator': operator, 'command': name, 'args': args}
                request = Request(address + path, data=json.dumps(payload).encode(), headers={
                    'Content-Type': 'application/json', 'X-Station-Key': key,
                    'X-Station-Client': client * 32, 'Origin': address,
                })
                with urlopen(request, timeout=2) as response:
                    return json.load(response)

            try:
                with urlopen(address, timeout=2) as response:
                    page = response.read().decode()
                self.assertIn('lan-key', page)
                self.assertNotIn(shared.key, page)
                with self.assertRaises(HTTPError) as denied:
                    submit('a', 'Operadora', 'PING', [], key='incorrecta')
                self.assertEqual(denied.exception.code, 403)
                denied.exception.close()
                submit('a', 'Operadora', 'SET_MODE', ['6DOF'])
                with self.assertRaises(HTTPError) as busy:
                    submit('b', 'Operador', 'SET_MODE', ['6DOF'])
                self.assertEqual(busy.exception.code, 403)
                busy.exception.close()
                ping = submit('b', 'Operador', 'PING', [])
                self.assertEqual(pico.queue.get_nowait(), 'CMD,1,SET_MODE,6DOF\n')
                self.assertEqual(pico.queue.get_nowait(), 'CMD,2,PING\n')
                pico._parse_line(f"ACK,{ping['id']},OK,PONG")
                for _ in range(2):
                    with urlopen(address + '/api/pico', timeout=2) as response:
                        snapshot = json.load(response)
                    self.assertEqual(snapshot['commands'][-1]['operator'], 'Operador')
                    self.assertEqual(snapshot['commands'][-1]['state'], 'OK')
                submit('a', 'Operadora', '', [], path='/api/control/release')
                self.assertEqual(submit('b', 'Operador', 'SET_MODE', ['6DOF'])['id'], 3)
            finally:
                server.shutdown()
                thread.join(2)


if __name__=='__main__': unittest.main()
