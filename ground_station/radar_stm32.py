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
import json
import math
import random
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
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
HEX = re.compile(r'^[0-9A-Fa-f]{6}$')
LOG_PATTERN = re.compile(
    r'^\[ADSB\]\s+ICAO=([0-9A-Fa-f]{6})\s+FLIGHT=(\S+)\s+ALT=([-+]?\d+)',
    re.IGNORECASE,
)
AIRCRAFT_TTL_S = 120


def to_optional_float(value: str) -> float | None:
    if value.strip() == '':
        return None
    result = float(value)
    if not math.isfinite(result):
        raise ValueError('non-finite value')
    return result


def parse_aircraft(line: str) -> dict | None:
    """Parse validated protocol frames or debug summaries; ignore other logs."""
    line = line.strip()
    fields = line.split(',')
    if fields[0] == '$ADSB' and len(fields) == 9 and fields[1] == '1':
        payload = fields[2:]
    elif fields[0] in ('@PCADSB', 'PCADSB') and len(fields) == 8:
        payload = fields[1:]
    else:
        m = LOG_PATTERN.match(line)
        if m:
            return {
                'icao': m.group(1).upper(), 'flight': m.group(2)[:8],
                'altitude': int(m.group(3)), 'speed': None,
                'heading': None, 'lat': None, 'lon': None,
            }
        return None

    icao, flight, alt, speed, heading, lat, lon = payload
    if not HEX.fullmatch(icao):
        raise ValueError('invalid ICAO')
    if not re.fullmatch(r'[A-Za-z0-9]{1,8}', flight):
        raise ValueError('invalid callsign')
    alt = to_optional_float(alt)
    speed = to_optional_float(speed)
    heading = to_optional_float(heading)
    lat = to_optional_float(lat)
    lon = to_optional_float(lon)
    if (lat is None) != (lon is None):
        raise ValueError('position incomplete')
    if lat is not None and not (-90 <= lat <= 90 and -180 <= lon <= 180):
        raise ValueError('position out of range')
    if heading is not None and not (0 <= heading <= 360):
        raise ValueError('heading out of range')
    if speed is not None and not (0 <= speed <= 5000):
        raise ValueError('speed out of range')
    return {
        'icao': icao.upper(), 'flight': flight.upper(),
        'altitude': round(alt) if alt is not None else None,
        'speed': round(speed) if speed is not None else None,
        'heading': round(heading) % 360 if heading is not None else None,
        'lat': lat, 'lon': lon,
    }


class State:
    def __init__(self, demo: bool):
        self.lock = threading.Lock()
        self.aircraft: dict[str, dict] = {}
        self.total = 0
        self.invalid = 0
        self.status = 'SIMULACIÓN' if demo else 'Iniciando puerto serie'
        self.last_error = ''
        self.demo = demo

    def ingest(self, line: str) -> None:
        try:
            aircraft = parse_aircraft(line)
        except (ValueError, IndexError):
            with self.lock:
                self.invalid += 1
            return
        if aircraft is None:
            return
        now = time.time()
        with self.lock:
            existing = self.aircraft.get(aircraft['icao'])
            # No perder posiciones antiguas ante mensajes parciales (pero expiran).
            if existing and aircraft['lat'] is None and now - existing['position_at'] < 20:
                aircraft['lat'], aircraft['lon'] = existing['lat'], existing['lon']
                aircraft['position_at'] = existing['position_at']
            else:
                aircraft['position_at'] = now if aircraft['lat'] is not None else 0
            aircraft['updated_at'] = now
            aircraft['message_count'] = 1 + (existing['message_count'] if existing else 0)
            self.aircraft[aircraft['icao']] = aircraft
            self.total += 1

    def snapshot(self) -> dict:
        now = time.time()
        with self.lock:
            self.aircraft = {
                k: a for k, a in self.aircraft.items()
                if now - a['updated_at'] < AIRCRAFT_TTL_S
            }
            items = [dict(a) for a in self.aircraft.values()]
            for a in items:
                a['age'] = round(now - a['updated_at'], 1)
                if a['position_at'] and now - a['position_at'] > 20:
                    a['lat'] = None
                    a['lon'] = None
            return {
                'status': self.status,
                'error': self.last_error,
                'demo': self.demo,
                'total': self.total,
                'invalid': self.invalid,
                'aircraft': sorted(items, key=lambda a: a['age']),
            }

    def set_status(self, status: str, error: str = '') -> None:
        with self.lock:
            self.status = status
            self.last_error = error


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


def make_handler(state: State, pico: PicoBridge):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            route = urlparse(self.path).path
            if route == '/':
                data = INDEX
                mimetype = 'text/html; charset=utf-8'
            elif route in WEB_ASSETS:
                mimetype, data = WEB_ASSETS[route]
            elif route == '/api/aircraft':
                data = json.dumps(state.snapshot(), ensure_ascii=False).encode('utf-8')
                mimetype = 'application/json; charset=utf-8'
            elif route == '/api/pico':
                data = json.dumps(pico.snapshot(), ensure_ascii=False).encode('utf-8')
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
            # Never accept commands from arbitrary remote browser origins.
            origin = self.headers.get('Origin', '')
            host = self.headers.get('Host', '')
            if route != '/api/pico/command' or host not in (
                    f'127.0.0.1:{self.server.server_port}',
                    f'localhost:{self.server.server_port}') or (
                    origin and origin not in (
                        f'http://127.0.0.1:{self.server.server_port}',
                        f'http://localhost:{self.server.server_port}')):
                self.send_error(403, 'Not allowed')
                return
            try:
                length = int(self.headers.get('Content-Length','0'))
                if not 0 < length < 1024:
                    raise ValueError('Invalid request size')
                payload = json.loads(self.rfile.read(length))
                if not isinstance(payload, dict):
                    raise ValueError('Invalid JSON')
                name, args = payload.get('command'), payload.get('args', [])
                if not isinstance(name, str) or not isinstance(args, list):
                    raise ValueError('Invalid command')
                result = pico.send_command(name, args)
                status = 202
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
    parser.add_argument('--demo', action='store_true', help='Datos sintéticos para probar la interfaz')
    parser.add_argument('--list-ports', action='store_true', help='Listar puertos serie')
    parser.add_argument('--pico-port', help='Puerto USB CDC del Pico SX1262 (opcional), diferente de ST-LINK')
    parser.add_argument('--pico-baud', type=int, default=115200)
    args = parser.parse_args()

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

    url = f'http://127.0.0.1:{args.http_port}'
    print(f'[WEB] Radar STM32 disponible en {url}')
    print('[WEB] Solo accesible desde este PC. Ctrl+C para cerrar.')
    print('[PICO] '+(args.pico_port or 'sin conectar (visualización LoRa en espera)'))
    with ThreadingHTTPServer(('127.0.0.1', args.http_port), make_handler(state, pico)) as server:
        try:
            server.serve_forever(poll_interval=0.5)
        except KeyboardInterrupt:
            print('\n[WEB] Cerrando')


if __name__ == '__main__':
    main()
