"""Pico USB-CDC / SX1262 bridge for the browser GUI.

Parses formats from the user-provided PyQt6 GUI (P2P_CMD_V1), without
requiring Qt/OpenGL on the web server. Never fakes missing telemetry.
"""
from __future__ import annotations

from collections import deque
import math
import queue
import re
import secrets
import threading
import time

COMMANDS = {'PING','GET_STATUS','SET_BETA','SET_MODE','SET_RATE','CAL_GYRO',
            'CAL_ACCEL','MAG_START','MAG_STOP','RESET_FILTER','SAVE_CONFIG','LOAD_CONFIG','RELAY','WHEEL'}
ASCII_RE = re.compile(
    r'T=(\d+)\s*\|\s*AX=(-?\d+)\s+AY=(-?\d+)\s+AZ=(-?\d+)\s*\|'
    r'\s*GX=(-?\d+)\s+GY=(-?\d+)\s+GZ=(-?\d+)\s*\|'
    r'\s*MX=(-?\d+)\s+MY=(-?\d+)\s+MZ=(-?\d+)\s*\|'
    r'\s*Q=(-?\d+),(-?\d+),(-?\d+),(-?\d+)\s*\|\s*MAG=(OK|ERR)', re.I)
Q_RE = re.compile(r'\bQ\s*[,=]\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*,\s*([+-]?\d+)(?!\d)', re.I)
RE_PAYLOAD = re.compile(r'^\[RX\]\s*Payload:\s*"(.*)"\s*$')
RE_STAT = {
    'rssi': re.compile(r'^\[RX\]\s*RSSI:\s*([-+\d.]+)\s*dBm'),
    'snr': re.compile(r'^\[RX\]\s*SNR:\s*([-+\d.]+)\s*dB'),
    'freq_error': re.compile(r'^\[RX\]\s*Freq error:\s*([-+\d.]+)\s*Hz'),
}
SENSOR_FIELDS = ('ax','ay','az','gx','gy','gz','mx','my','mz','heading',
                 'mag_ok','mag_detected','mag_valid','mag_calibrated')


def parse_telemetry(text: str):
    text = text.strip()
    mag_status = None
    if text.startswith('TEL,'):
        text, *suffix = text.split('|')
        mag_status = next((re.fullmatch(r'MAGSTAT,([01]),([01]),([01])', item)
                           for item in suffix if item.startswith('MAGSTAT,')), None)
    if text.startswith('TEL,'):
        p = text.split(',')
        if len(p) != 17:
            return None
        try:
            values = list(map(int, p[1:]))
        except ValueError:
            return None
        seq, t_ms = values[:2]
        a, q, mag = values[2:11], values[11:15], values[15]
        if not (0 <= seq <= 65535 and t_ms >= 0 and mag in (0, 1)):
            return None
        result = dict(zip(('ax','ay','az','gx','gy','gz','mx','my','mz'), a))
        result.update(seq=seq, t_ms=t_ms, mag_ok=bool(mag))
        if mag_status:
            detected, valid, calibrated = (value == '1' for value in mag_status.groups())
            result.update(mag_detected=detected, mag_valid=valid, mag_calibrated=calibrated,
                          mag_ok=detected and valid and calibrated)
            if not detected or not valid:
                result.update(mx=None, my=None, mz=None)
    else:
        m = ASCII_RE.search(text)
        if m:
            g = m.groups()
            q = [int(v) for v in g[10:14]]
            result = dict(zip(('ax','ay','az','gx','gy','gz','mx','my','mz'),
                              [int(v) for v in g[1:10]]))
            result.update(seq=None, t_ms=int(g[0]), mag_ok=g[14].upper() == 'OK')
        else:
            m = Q_RE.search(text)
            if not m:
                return None
            q = list(map(int, m.groups()))
            result = {k: None for k in ('ax','ay','az','gx','gy','gz','mx','my','mz')}
            result.update(seq=None, t_ms=None, mag_ok=None)
    if any(not (-32767 <= v <= 32767) for v in q):
        return None
    result.update(dict(zip(('q0','q1','q2','q3'), [v / 10000 for v in q])))
    result['norm'] = math.sqrt(sum((v/10000)**2 for v in q))
    if result['mag_ok'] is True and result['mx'] is not None and result['my'] is not None:
        result['heading'] = (math.degrees(math.atan2(result['my'], result['mx'])) + 360) % 360
    else:
        result['heading'] = None
    return result


def validate_command(name: str, args: list) -> str:
    if name not in COMMANDS:
        raise ValueError('Comando no permitido')
    args = [str(v) for v in args]
    if name == 'SET_BETA':
        if len(args) != 1 or not math.isfinite(float(args[0])) or not 0.001 <= float(args[0]) <= 2:
            raise ValueError('Beta debe estar entre 0.001 y 2.0')
        args = [f'{float(args[0]):.3f}']
    elif name == 'SET_MODE':
        if args not in (['6DOF'], ['9DOF']):
            raise ValueError('Modo debe ser 6DOF o 9DOF')
    elif name == 'SET_RATE':
        if len(args) != 1 or not args[0].isdigit() or not 1 <= int(args[0]) <= 10:
            raise ValueError('Tasa debe estar entre 1 y 10 Hz')
    elif name == 'RELAY':
        if args not in (['ON'], ['OFF'], ['STATUS']):
            raise ValueError('Relé admite ON, OFF o STATUS')
    elif name == 'WHEEL':
        if len(args) != 1:
            raise ValueError('Indica la orden de rueda')
        action = args[0]
        if re.fullmatch(r'RUN:[0-9]{4}:[0-9]{1,2}', action):
            pulse, seconds = map(int, action.split(':')[1:])
            if not 1505 <= pulse <= 1575 or pulse % 5 or not 1 <= seconds <= 60:
                raise ValueError('PWM de 1505 a 1575 µs en pasos de 5 y duración de 1 a 60 s')
            args = [f'RUN:{pulse}:{seconds}']
        elif re.fullmatch(r'RUNS:[0-9]{4}', action):
            pulse = int(action.split(':')[1])
            if not 1505 <= pulse <= 1575 or pulse % 5:
                raise ValueError('PWM entre 1505 y 1575 µs, en pasos de 5')
        elif re.fullmatch(r'TURN:-?[0-9]{1,3}', action):
            if not 1 <= abs(int(action.split(':')[1])) <= 180:
                raise ValueError('Indica un giro entre −180° y +180°, distinto de cero')
        elif re.fullmatch(r'LIGHT(?:INV)?:[0-9]{1,3}', action):
            if not 0 <= int(action.split(':')[1]) < 360:
                raise ValueError('El azimut inicial debe estar entre 0° y 359°')
        elif re.fullmatch(r'AXIS_[XYZ][+-]', action):
            pass
        elif action not in ('ARM','STOP','STATUS','DETUMBLE','PROFILE_AIR','PROFILE_BIDIR','ENABLE','BENCH_ARM','RESULTS','BRAKE'):
            if not re.fullmatch(r'[+-]?[0-9]{1,2}', action) or not 1 <= abs(int(action)) <= 15:
                raise ValueError('El nivel de prueba debe estar entre −15 y +15, distinto de cero')
            args = [str(int(action))]
    elif args:
        raise ValueError('Comando sin argumentos')
    return name + (',' + ','.join(args) if args else '')


class PicoBridge:
    def __init__(self, port: str | None, baud: int = 115200, on_aircraft=None):
        self.on_aircraft = on_aircraft
        self.barometer = None
        self.solar = None
        self.solar_at = None
        self.solar_history = deque(maxlen=120)
        self.test_panel = None
        self.panels = None
        self.panel_history = deque(maxlen=120)
        self.port, self.baud = port, baud
        self.lock = threading.Lock()
        self.queue = queue.Queue(maxsize=32)
        self.connected = False
        self.capable = False
        self.status = 'Pico no configurado' if not port else 'Esperando conexión Pico'
        self.last_error = ''
        self.latest = None
        self.last_sample_at = 0
        self.last_sensor_at = None
        self.wheel = None
        self.adcs_history = deque(maxlen=120)
        self.wheel_at = 0.0
        self.last_payload = ''
        self.packet_count = 0
        self.sample_count = 0
        self.rf = {'rssi': None, 'snr': None, 'freq_error': None}
        self.logs = deque(maxlen=45)
        self.history = deque(maxlen=120)
        # The STM32 keeps recent IDs across browser reloads and USB reconnects.
        self.next_id = secrets.randbelow(65535) + 1
        self.commands = deque(maxlen=35)
        self.brake_batch = None
        self.relay_command = None
        self.relay_requested_at = 0.0
        self.pending_packet = None
        self.packet_at = 0

    def log(self, msg):
        self.logs.append({'t':time.strftime('%H:%M:%S'), 'text':msg[:180]})

    def snapshot(self):
        with self.lock:
            latest = dict(self.latest) if self.latest else None
            if latest:
                latest['age'] = round(time.time() - self.last_sample_at, 1)
                latest['sensor_age'] = (round(time.time() - self.last_sensor_at, 1)
                                        if self.last_sensor_at is not None else None)
            return dict(connected=self.connected, capable=self.capable,
                        status=self.status, error=self.last_error, latest=latest,
                        barometer=self.barometer,
                        solar=dict(self.solar, age=round(time.monotonic() - self.solar_at, 1)) if self.solar else None,
                        solar_history=list(self.solar_history),
                        test_panel=dict(self.test_panel, age=round(time.monotonic() - self.test_panel['at'], 1))
                        if self.test_panel else None,
                        panels=dict(self.panels, age=round(time.monotonic() - self.panels['at'], 1))
                        if self.panels else None,
                        panel_history=list(self.panel_history),
                        relay=self._relay_status(),
                        wheel=dict(self.wheel, age=round(time.monotonic() - self.wheel_at, 1)) if self.wheel else None,
                        adcs_history=list(self.adcs_history),
                        brake_batch=self.brake_batch, packets=self.packet_count, samples=self.sample_count,
                        rf=dict(self.rf), payload=self.last_payload[:200],
                        history=list(self.history), commands=list(self.commands),
                        logs=list(self.logs))

    def send_command(self, name: str, args: list, operator=None):
        payload = validate_command(name, args)
        with self.lock:
            if not (self.connected and self.capable):
                raise RuntimeError('El Pico aún no confirma PICO_READY,P2P_CMD_V1')
            cmd_id = self.next_id
            self.next_id = self.next_id % 65535 + 1
            frame = f'CMD,{cmd_id},{payload}\n'
            if self.queue.full():
                raise RuntimeError('Cola de comandos llena')
            self.queue.put_nowait(frame)
            command = {'id':cmd_id, 'name':name,'state':'EN COLA','detail':'Esperando entrega al Pico'}
            if operator:
                command['operator'] = operator
            self.commands.append(command)
            if name == 'RELAY':
                self.relay_command = command
                self.relay_requested_at = time.monotonic()
            origin = f'[PC · {operator}]' if operator else '[PC]'
            self.log(f'{origin} #{cmd_id} {name}: en cola, NO confirmado por STM32')
        return {'id':cmd_id,'state':'EN COLA','message':'Pendiente de ACK de STM32'}

    def _update_command(self, cmd_id, state, detail):
        for cmd in reversed(self.commands):
            if cmd['id'] == cmd_id:
                cmd['state'],cmd['detail'] = state,detail[:120]
                break

    def _relay_status(self):
        cmd = self.relay_command
        if not self.connected or not self.capable or not cmd:
            return 'UNKNOWN'
        if cmd['state'] == 'OK':
            return {'RELAY,ON':'ON', 'RELAY,OFF':'OFF'}.get(cmd['detail'], 'UNKNOWN')
        if cmd['state'] in ('EN COLA', 'PICO QUEUED', 'PICO SENT'):
            return 'PENDING' if time.monotonic() - self.relay_requested_at < 15 else 'UNKNOWN'
        return 'UNKNOWN'

    def _handle_payload(self, payload, rf=None):
        self.packet_count += 1
        self.last_payload = payload[:200]
        if rf:
            self.rf.update(rf)
        m = re.match(r'^ACK,(\d+),(OK|ERR)(?:,(.*))?$', payload)
        if m:
            detail = m[3] or ''
            self._update_command(int(m[1]),m[2],detail)
            brake = re.fullmatch(r'BRAKE,(\d{1,4}),([0-5]),([0-6]),([01])((?:,\d{1,4}){0,20})', detail)
            if m[2] == 'OK' and brake:
                pulse, phase, error, armed = map(int, brake.groups()[:4])
                centiseconds = [int(t) for t in brake[5].split(',') if t]
                if (pulse == 0 or (1505 <= pulse <= 1575 and not pulse % 5)) and all(t <= 6000 for t in centiseconds):
                    self.brake_batch = dict(pulse=pulse, phase=phase, error=error, armed=bool(armed),
                                            seconds=[t / 100 for t in centiseconds])
            else:
                self.log(f'[STM32] ACK #{m[1]}: {m[2]} {detail}')
        e = re.match(r'^EVT,(\d+),([A-Z_]+),(DONE|ERR)(?:,(.*))?$', payload)
        if e:
            self._update_command(int(e[1]),e[3],e[4] or '')
            self.log(f'[STM32] EVENT #{e[1]} {e[2]}: {e[3]} {e[4] or ""}'.rstrip())
        records = payload.split('|')
        for record in records:
            panels = re.fullmatch(r'PV3,([0-7]),([0-7]),(\d{1,5}),(\d{1,5}),(\d{1,5})', record)
            if panels:
                valid, saturated, *mv = map(int, panels.groups())
                if not saturated & ~valid:
                    channels = [dict(voltage=value / 1000 if valid & (1 << index) else None,
                                     saturated=bool(saturated & (1 << index)))
                                for index, value in enumerate(mv)]
                    self.panels = dict(channels=channels, at=time.monotonic())
                    self.panel_history.append(dict(t=time.time(), **{
                        f'panel{index + 1}': channel['voltage'] if not channel['saturated'] else None
                        for index, channel in enumerate(channels)
                    }))
                break
            panel = re.fullmatch(r'PTEST,([0-2]),(\d{1,5})', record)
            if panel:
                status, mv = map(int, panel.groups())
                self.test_panel = dict(voltage=mv / 1000 if status else None,
                                       saturated=status == 2, at=time.monotonic())
                break
        for record in records:
            ina = re.fullmatch(r'EPSI,(\d{1,5}),([0-3]),(\d{1,5}),(-?\d{1,8}),(\d{1,5})', record)
            if ina:
                seq, flags, solar_mv, current_ua, battery_mv = map(int, ina.groups())
                if seq <= 65535 and solar_mv <= 32000 and abs(current_ua) <= 10000000 and battery_mv <= 33000:
                    solar_voltage = solar_mv / 1000 if flags & 1 else None
                    battery_voltage = battery_mv / 1000 if flags & 2 else None
                    channels = [dict(voltage=value, saturated=False)
                                for value in (solar_voltage, None, None, battery_voltage)]
                    self.solar = dict(source='ina219', seq=seq, channels=channels,
                                      current_ma=current_ua / 1000 if flags & 1 else None)
                    self.solar_at = time.monotonic()
                    self.solar_history.append(dict(t=time.time(), pair1=solar_voltage, battery=battery_voltage))
                break
            eps = re.fullmatch(r'EPS,(\d{1,2}),(\d{1,2}),((?:\d{1,8},){3}\d{1,8})', record)
            if eps:
                valid, saturated = int(eps[1]), int(eps[2])
                if valid <= 15 and saturated <= 15 and not (saturated & ~valid):
                    readings = [int(value) / 1000 for value in eps[3].split(',')]
                    channels = [
                        dict(voltage=voltage if valid & (1 << index) else None,
                             saturated=bool(saturated & (1 << index)))
                        for index, voltage in enumerate(readings)
                    ]
                    self.solar = dict(channels=channels)
                    self.solar_at = time.monotonic()
                    self.solar_history.append(dict(t=time.time(), **{
                        key: None if channel['saturated'] else channel['voltage']
                        for key, channel in zip(('pair1', 'pair2', 'pair3', 'battery'), channels)
                    }))
                break
            if record == 'SOLAR,NA,NA':
                self.solar = dict(adc=None, voltage=None, saturated=False)
                self.solar_at = time.monotonic()
                break
            reading = re.fullmatch(r'SOLAR,(\d{1,4}),(\d{1,8})', record)
            if reading:
                adc, mv = map(int, reading.groups())
                if adc <= 4095:
                    saturated = adc >= 4090
                    self.solar = dict(adc=adc, voltage=mv / 1000, saturated=saturated)
                    self.solar_at = time.monotonic()
                    self.solar_history.append(dict(t=time.time(), voltage=None if saturated else mv / 1000))
                break
        adcs = next((re.fullmatch(r'ADCS,(\d),(\d{1,2}),(-?\d{1,5}),(\d{1,4}),(\d{1,3})', record)
                     for record in records if record.startswith('ADCS,')), None)
        light = next((re.fullmatch(r'LIGHT,(\d{1,2}),(\d{1,4}),(\d{1,4}),(\d{1,4}),(\d{1,4})', record)
                      for record in records if record.startswith('LIGHT,')), None)
        maneuver = next((re.fullmatch(r'MAN,([0-4]),(-?\d{1,5}),(-?\d{1,5}),(-?\d{1,5}),(-?\d{1,5})', record)
                         for record in records if record.startswith('MAN,')), None)
        for record in records:
            if record.startswith('$ADSB,') and self.on_aircraft:
                self.on_aircraft(record)
            wheel = re.fullmatch(r'WHEEL,(\d),(\d),(\d{1,4}),(-?\d{1,2}),(\d{1,5})', record)
            if wheel:
                profile, state, pulse, level, rpm = map(int, wheel.groups())
                if profile <= 2 and state <= 4 and (pulse == 0 or 1000 <= pulse <= 2000) and abs(level) <= 15 and rpm <= 65535:
                    self.wheel = dict(profile=profile, state=state, pulse=pulse, level=level,
                                      rpm=None if rpm == 65535 else rpm)
                    if adcs:
                        axis, result, rate, elapsed, status = map(int, adcs.groups())
                        if axis <= 7 and result <= 9 and -32768 <= rate <= 32767 and elapsed <= 6000 and status <= 255:
                            self.wheel.update(axis=axis & 3, inverted=bool(axis & 4), detumble=result,
                                              body_rpm=None if rate == -32768 else rate / 100,
                                              elapsed_s=elapsed / 100, leds=status & 15,
                                              gyro_calibrated=bool(status & 16),
                                              mag_calibrated=bool(status & 32),
                                              mag_valid=bool(status & 64), fusion_9dof=bool(status & 128))
                            if light:
                                mask, *channels = map(int, light.groups())
                                if mask <= 15 and all(value <= 4095 for value in channels):
                                    self.wheel.update({f'light_{i+1}': value if mask & (1 << i) else None
                                                       for i, value in enumerate(channels)})
                            if maneuver:
                                mode, *angles = map(int, maneuver.groups())
                                if mode and all(-32768 <= angle <= 32767 for angle in angles):
                                    keys = (('brake_to_5rpm_s', 'brake_initial_rpm') if mode == 1 else
                                            ('spin_seconds', 'spin_pwm', 'spin_result') if mode == 4 else
                                            ('rotation_deg', 'target_deg', 'reference_deg', 'light_azimuth_deg'))
                                    self.wheel.update(maneuver=mode, **{
                                        key: None if angle == -32768 else angle / 10
                                        for key, angle in zip(keys, angles)
                                    })
                            self.adcs_history.append(dict(self.wheel, t=time.time()))
                    self.wheel_at = time.monotonic()
        baro = next((r for r in records if r.startswith('BARO,')), None)
        if records[0].startswith(('TEL,', 'Q,', 'QERR,')) or baro:
            self.barometer = None
            if baro:
                try:
                    _, pressure, temperature = baro.split(',')
                    pressure, temperature = int(pressure), int(temperature)
                    if 30000 <= pressure <= 110000 and -4000 <= temperature <= 8500:
                        self.barometer = {'pressure_pa': pressure, 'temperature_c': temperature / 100}
                except ValueError:
                    pass
        tel = parse_telemetry(payload if payload.startswith('TEL,') else records[0])
        if tel:
            self.last_sample_at = time.time()
            self.sample_count += 1
            if tel['ax'] is not None:
                self.last_sensor_at = self.last_sample_at
                self.latest = tel
            elif self.latest and self.last_sensor_at is not None:
                self.latest = {**tel, **{key: self.latest.get(key) for key in SENSOR_FIELDS}}
            else:
                self.latest = tel
            item = {'t':round(self.last_sample_at,1)}
            item.update({k:tel.get(k) for k in ('q0','q1','q2','q3','norm','ax','ay','az','gx','gy','gz','mx','my','mz','heading')})
            self.history.append(item)

    def _flush_packet(self):
        if self.pending_packet:
            p = self.pending_packet
            self._handle_payload(p['payload'], p['rf'])
            self.pending_packet = None

    def _parse_line(self, line: str):
        if not line:
            return
        if line == 'PICO_READY,P2P_CMD_V1':
            self.capable = True
            self.log('[PICO] Confirmó protocolo bidireccional P2P_CMD_V1')
            return
        simple = re.fullmatch(r'\[RX\] (.*) \| len=\d+ RSSI=([-+\d.]+) dBm SNR=([-+\d.]+) dB', line)
        if simple:
            self._flush_packet()
            self._handle_payload(simple[1], {'rssi': float(simple[2]), 'snr': float(simple[3]), 'freq_error': None})
            return
        m = RE_PAYLOAD.match(line)
        if m:
            self._flush_packet()
            self.pending_packet = {'payload':m[1], 'rf':{k:None for k in RE_STAT}}
            self.packet_at = time.monotonic()
            return
        if self.pending_packet:
            for key,pattern in RE_STAT.items():
                stat = pattern.match(line)
                if stat:
                    self.pending_packet['rf'][key] = float(stat[1])
                    if key == 'freq_error':
                        self._flush_packet()
                    return
        m = re.match(r'^PICO_TX,(\d+),(QUEUED|SENT|ERR)(?:,(.*))?$', line)
        if m:
            self._update_command(int(m[1]),'PICO '+m[2],m[3] or '')
            self.log('[PICO] '+line)
            return
        timeout = re.fullmatch(r'PICO_ACK_TIMEOUT,(\d+),UNKNOWN', line)
        if timeout:
            cmd_id = int(timeout[1])
            if any(cmd['id'] == cmd_id and cmd['state'] == 'PICO SENT' for cmd in self.commands):
                self._update_command(cmd_id, 'SIN CONFIRMACIÓN', 'Sin ACK de STM32')
            return
        if line.startswith(('TEL,','Q,','Q=','QERR,','BARO,','$ADSB,','T=','ACK,','EVT,')):
            self._flush_packet()
            self._handle_payload(line)

    def run(self):
        if not self.port:
            return
        import serial
        while True:
            try:
                with serial.Serial(self.port, self.baud, timeout=0.2) as s:
                    with self.lock:
                        self.connected = True
                        self.capable = False
                        self.status = f'Pico conectado a {self.port} · esperando HELLO'
                        self.last_error = ''
                        self.log(f'[USB] Conectado {self.port}')
                    s.write(b'HELLO\n')
                    while True:
                        with self.lock:
                            if self.pending_packet and time.monotonic()-self.packet_at>0.6:
                                self._flush_packet()
                        try:
                            frame = self.queue.get_nowait()
                        except queue.Empty:
                            frame = None
                        if frame:
                            s.write(frame.encode('ascii'))
                        line = s.readline()
                        if line:
                            with self.lock:
                                self._parse_line(line.decode('utf-8', errors='replace').strip())
            except (serial.SerialException, OSError) as e:
                with self.lock:
                    self.connected = False
                    self.capable = False
                    self.relay_command = None
                    self.latest = None
                    self.last_sensor_at = None
                    self.wheel = None
                    self.solar = None
                    self.test_panel = None
                    self.panels = None
                    self.status = 'Pico desconectado · reintentando'
                    self.last_error = str(e)[:150]
                    self.log('[USB] '+self.last_error)
                time.sleep(3)
