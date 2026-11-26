#!/usr/bin/env python3
"""Radar ADS-B local: STM32 USART2/ST-LINK COM -> mapa Leaflet en PC.

La fuente de datos es EXCLUSIVAMENTE el COM de la STM32. No lee readsb
ni tar1090. Soporta tramas:
    $ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810
    @PCADSB,E8032E,SKU176,12575,253,32,-33.472,-70.810
    [ADSB] ICAO=E8032E FLIGHT=SKU176 ALT=12575 (sin posición)
"""

from __future__ import annotations

import argparse
import hmac
import ipaddress
import json
import math
import random
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from aircraft_state import State, parse_aircraft
from pico_bridge import PicoBridge
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
INDEX = (ROOT / 'templates' / 'index.html').read_bytes()
WEB_ASSETS = {
    '/assets/usm-cubesat-logo.svg': ('image/svg+xml', (ROOT / 'assets' / 'usm-cubesat-logo.svg').read_bytes()),
    '/assets/cubesat_web.css': ('text/css; charset=utf-8', (ROOT / 'assets' / 'cubesat_web.css').read_bytes()),
    '/assets/cubesat_web.js': ('application/javascript; charset=utf-8', (ROOT / 'assets' / 'cubesat_web.js').read_bytes()),
}
def serial_worker(state: State, port: str, baud: int, ingest_aircraft: bool = True) -> None:
    import serial  # pyserial installed by user
    while True:
        try:
            state.set_status(f'Conectando a {port}...')
            with serial.Serial(port, baud, timeout=1) as connection:
                state.set_status(f'Conectado a {port} · {baud} bps')
                print(f'[UART] Conectado a {port} ({baud} bps)', flush=True)
                while True:
                    data = connection.readline()
                    if not data:
                        continue
                    line = data.decode('ascii', errors='replace').strip()
                    if line:
                        print(f'[RX] {line}', flush=True)
                        if ingest_aircraft:
                            state.ingest(line)
        except serial.SerialException as exc:
            error = str(exc)
            state.set_status('UART desconectado · reintentando', error)
            print('[UART]', error, flush=True)
            time.sleep(3)


def demo_worker(state: State) -> None:
    # Posiciones totalmente sintéticas: NO representan tráfico real.
    planes = [
        ('ABC123', 'DEMO01', -33.35, -70.78, 12000, 230, 45),
        ('DEF456', 'DEMO02', -33.72, -70.48, 19000, 310, 200),
        ('FED789', 'DEMO03', -33.46, -70.32, 8000, 200, 290),
    ]
    step = 0
    while True:
        for i, (icao, flight, lat0, lon0, alt, speed, heading) in enumerate(planes):
            t = step * 0.001
            lat = lat0 + math.cos(math.radians(heading)) * t
            lon = lon0 + math.sin(math.radians(heading)) * t
            state.ingest(f'$ADSB,1,{icao},{flight},{alt},{speed},{heading},{lat:.6f},{lon:.6f}')
        step += 1
        time.sleep(2)


LAN_TOOLBAR = """
<section class="lan-toolbar" aria-label="Estación compartida">
  <b>Red local</b>
  <label>Operador <input id="lan-operator" maxlength="32" autocomplete="off"></label>
  <label>Clave de mandos <input id="lan-key" type="password" maxlength="64" autocomplete="off"></label>
  <button type="button" id="lan-release">Soltar control</button>
  <span id="lan-status" role="status">Telemetría compartida</span>
  <span id="lan-feedback" role="status"></span>
</section>
"""


class SharedControl:
    def __init__(self, ip):
        self.ip = ip
        self.key = secrets.token_hex(6)
        self.lock = threading.Lock()
        self.owner = None
        self.operator = None
        self.expires = 0

    def snapshot(self):
        with self.lock:
            remaining = max(0, self.expires - time.monotonic())
            return {'operator': self.operator if remaining else None, 'seconds': math.ceil(remaining)}

    def submit(self, pico, client, operator, name, args):
        independent = (name in ('PING', 'GET_STATUS') or
                       (name in ('WHEEL', 'RELAY') and args == ['STATUS']) or
                       (name == 'WHEEL' and args in (['STOP'], ['RESULTS'], ['BRAKE'])))
        with self.lock:
            if not independent and self.owner != client and time.monotonic() < self.expires:
                raise PermissionError(f'{self.operator} tiene el turno de control')
            result = pico.send_command(name, args, operator=operator)
            if not independent:
                self.owner, self.operator = client, operator
                self.expires = time.monotonic() + (125 if name == 'MAG_START' else 60)
            return result

    def release(self, client):
        with self.lock:
            if self.owner == client:
                self.owner = self.operator = None
                self.expires = 0


def make_handler(state: State, pico: PicoBridge, shared=None):
    index = (INDEX.replace(b'<nav ', LAN_TOOLBAR.encode('utf-8') + b'<nav ', 1)
             if shared else INDEX)

    class Handler(BaseHTTPRequestHandler):
        def allowed_hosts(self):
            names = (shared.ip,) if shared else ('127.0.0.1', 'localhost')
            return {f'{name}:{self.server.server_port}' for name in names}

        def do_GET(self):
            if shared and self.headers.get('Host') not in self.allowed_hosts():
                self.send_error(403, 'Not allowed')
                return
            route = urlparse(self.path).path
            if route == '/':
                data = index
                mimetype = 'text/html; charset=utf-8'
            elif route in WEB_ASSETS:
                mimetype, data = WEB_ASSETS[route]
            elif route == '/api/aircraft':
                data = json.dumps(state.snapshot(), ensure_ascii=False).encode('utf-8')
                mimetype = 'application/json; charset=utf-8'
            elif route == '/api/pico':
                snapshot = pico.snapshot()
                if shared:
                    snapshot['shared_control'] = shared.snapshot()
                data = json.dumps(snapshot, ensure_ascii=False).encode('utf-8')
                mimetype = 'application/json; charset=utf-8'
            else:
                self.send_error(404, 'Not found')
                return
            self.send_response(200)
            self.send_header('Content-Type', mimetype)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            route = urlparse(self.path).path
            origin = self.headers.get('Origin', '')
            host = self.headers.get('Host', '')
            routes = ('/api/pico/command', '/api/control/release') if shared else ('/api/pico/command',)
            if (route not in routes or host not in self.allowed_hosts() or
                    (origin and origin != f'http://{host}')):
                self.send_error(403, 'Not allowed')
                return
            try:
                if shared:
                    key = self.headers.get('X-Station-Key', '').encode('utf-8')
                    if not hmac.compare_digest(key, shared.key.encode('ascii')):
                        raise PermissionError('Introduce la clave de mandos del PC anfitrión')
                    client = self.headers.get('X-Station-Client', '')
                    if len(client) != 32 or any(c not in '0123456789abcdef' for c in client):
                        raise PermissionError('Recarga la página de la estación')
                length = int(self.headers.get('Content-Length','0'))
                if not 0 < length < 1024:
                    raise ValueError('Invalid request size')
                payload = json.loads(self.rfile.read(length))
                if not isinstance(payload, dict):
                    raise ValueError('Invalid JSON')
                name, args = payload.get('command'), payload.get('args', [])
                if route == '/api/control/release':
                    shared.release(client)
                    result, status = {'released': True}, 200
                else:
                    if not isinstance(name, str) or not isinstance(args, list):
                        raise ValueError('Invalid command')
                    if shared:
                        operator = payload.get('operator')
                        if not isinstance(operator, str) or not 1 <= len(operator.strip()) <= 32:
                            raise ValueError('Introduce tu nombre de operador')
                        if not operator.isprintable():
                            raise ValueError('Nombre de operador inválido')
                        result = shared.submit(pico, client, operator.strip(), name, args)
                    else:
                        result = pico.send_command(name, args)
                    status = 202
            except PermissionError as exc:
                result, status = {'error': str(exc)}, 403
            except (ValueError, TypeError, RuntimeError, OverflowError) as exc:
                result = {'error':str(exc)}
                status = 400
            data = json.dumps(result, ensure_ascii=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type','application/json; charset=utf-8')
            self.send_header('Content-Length',str(len(data)))
            self.send_header('Cache-Control','no-store')
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, fmt, *args):
            return

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description='Radar local STM32 ADS-B (COM -> mapa)')
    parser.add_argument('--port', help='Puerto COM ST-LINK, ej. COM15')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--http-port', type=int, default=8765)
    parser.add_argument('--lan-ip', help='IPv4 privada de este PC para compartir el panel')
    parser.add_argument('--demo', action='store_true', help='Datos sintéticos para probar la interfaz')
    parser.add_argument('--list-ports', action='store_true', help='Listar puertos serie')
    parser.add_argument('--pico-port', help='Puerto USB CDC del Pico SX1262 (opcional), diferente de ST-LINK')
    parser.add_argument('--pico-baud', type=int, default=115200)
    args = parser.parse_args()
    shared = None
    if args.lan_ip:
        try:
            ip = ipaddress.IPv4Address(args.lan_ip)
            if not ip.is_private or ip.is_loopback or ip.is_unspecified or ip.is_link_local:
                raise ValueError
        except ValueError:
            parser.error('--lan-ip requiere la IPv4 privada de la red local')
        shared = SharedControl(str(ip))

    if args.list_ports:
        try:
            from serial.tools import list_ports
        except ModuleNotFoundError:
            parser.error('Instala pyserial con: py -m pip install pyserial')
        for p in list_ports.comports():
            print(f'{p.device:10} {p.description}')
        return
    if not args.demo and not (args.port or args.pico_port):
        parser.error('Indica --port COMxx, --pico-port COMxx o --demo')
    if not args.demo:
        try:
            import serial  # noqa: F401
        except ModuleNotFoundError:
            parser.error('Instala pyserial con: py -m pip install pyserial')

    if args.pico_port and args.pico_port == args.port:
        parser.error('ST-LINK y Pico deben utilizar puertos serie diferentes')
    state = State(demo=args.demo)
    pico = PicoBridge(args.pico_port, args.pico_baud, None if args.demo else state.ingest)
    if args.pico_port:
        state.set_status(f'ADS-B por LoRa desde {args.pico_port}')
        threading.Thread(target=pico.run, daemon=True).start()
    if args.demo:
        threading.Thread(target=demo_worker, args=(state,), daemon=True).start()
    elif args.port:
        threading.Thread(target=serial_worker, args=(state, args.port, args.baud, not args.pico_port),
                         daemon=True).start()

    bind_ip = shared.ip if shared else '127.0.0.1'
    url = f'http://{bind_ip}:{args.http_port}'
    print(f'[WEB] CubeSat - Ground Station en {url}')
    if shared:
        print(f'[WEB] Clave de mandos para esta sesión: {shared.key}')
        print('[WEB] Ambos operadores deben abrir la dirección indicada.')
    else:
        print('[WEB] Solo accesible desde este PC.')
    print('[PICO] '+(args.pico_port or 'sin conectar (visualización LoRa en espera)'))
    with ThreadingHTTPServer((bind_ip, args.http_port), make_handler(state, pico, shared)) as server:
        try:
            server.serve_forever(poll_interval=0.5)
        except KeyboardInterrupt:
            print('\n[WEB] Cerrando')


if __name__ == '__main__':
    main()
