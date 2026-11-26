#!/usr/bin/env python3
"""Relay readsb BaseStation/SBS data to CubeSat Ground Station over UART."""

import argparse
import math
import re
import socket
import sys
import time


class SbsRelay:
    def __init__(self):
        self.aircraft = {}

    def frame(self, line):
        fields = line.strip().split(',')
        if len(fields) < 22 or fields[0] != 'MSG':
            return None
        icao = fields[4].strip().upper()
        if not re.fullmatch(r'[0-9A-F]{6}', icao):
            return None
        now = time.monotonic()
        state, seen = self.aircraft.get(icao, ({}, now))
        if now - seen >= 120:
            state = {}

        callsign = re.sub(r'[^A-Z0-9]', '', fields[10].upper())[:8]
        values = {
            'callsign': callsign,
            'altitude': number(fields[11], -2000, 100000),
            'speed': number(fields[12], 0, 5000),
            'heading': number(fields[13], 0, 360),
            'vr': number(fields[16], -50000, 50000),
            'squawk': fields[17].strip() if re.fullmatch(r'[0-7]{4}', fields[17].strip()) else '',
        }
        updates = {key: value for key, value in values.items() if value not in (None, '')}
        lat = number(fields[14], -90, 90)
        lon = number(fields[15], -180, 180)
        has_position = lat is not None and lon is not None
        if not updates and not has_position:
            return None
        state.update(updates)
        self.aircraft[icao] = (state, now)

        def field(key):
            value = state.get(key)
            return '' if value is None else str(round(value))

        payload = [
            '$ADSB', '2', icao, state.get('callsign', icao),
            field('altitude'), field('speed'), field('heading'),
            f'{lat:.6f}' if has_position else '', f'{lon:.6f}' if has_position else '',
            field('vr'), state.get('squawk', ''),
        ]
        return (','.join(payload) + '\r\n').encode('ascii')

    def expire(self):
        cutoff = time.monotonic() - 120
        self.aircraft = {icao: record for icao, record in self.aircraft.items() if record[1] > cutoff}


def number(raw, low, high):
    try:
        value = float(raw)
    except ValueError:
        return None
    return value if math.isfinite(value) and low <= value <= high else None


def send(args, output):
    while True:
        try:
            source = socket.create_connection((args.host, args.port), timeout=10)
        except OSError as error:
            print(f'SBS: {error}', file=sys.stderr, flush=True)
        else:
            with source:
                print(f'SBS conectado a {args.host}:{args.port}', file=sys.stderr, flush=True)
                relay = SbsRelay()
                pending = b''
                cleanup_at = time.monotonic() + 60
                while True:
                    try:
                        chunk = source.recv(4096)
                    except socket.timeout:
                        continue
                    except OSError:
                        break
                    if not chunk:
                        break
                    *lines, pending = (pending + chunk).split(b'\n')
                    if len(pending) > 4096:
                        pending = b''
                    for line in lines:
                        frame = relay.frame(line.decode('ascii', errors='replace'))
                        if frame and output.write(frame) != len(frame):
                            raise OSError('Escritura incompleta en la UART')
                    if args.stdout:
                        output.flush()
                    if time.monotonic() >= cleanup_at:
                        relay.expire()
                        cleanup_at = time.monotonic() + 60
        print('Reconectando SBS en 3 s', file=sys.stderr, flush=True)
        time.sleep(3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='127.0.0.1', help='IP de readsb')
    parser.add_argument('--port', type=int, default=30003, help='Puerto TCP SBS')
    parser.add_argument('--uart', default='/dev/serial0', help='Dispositivo UART de Linux')
    parser.add_argument('--stdout', action='store_true', help='Muestra tramas sin abrir la UART')
    args = parser.parse_args()
    try:
        if args.stdout:
            send(args, sys.stdout.buffer)
            return
        try:
            import serial
        except ImportError:
            parser.exit(1, 'Instala pyserial con sudo apt install python3-serial\n')
        while True:
            try:
                with serial.Serial(args.uart, baudrate=115200, write_timeout=2) as output:
                    print(f'UART {args.uart} a 115200 8N1', file=sys.stderr, flush=True)
                    send(args, output)
            except OSError as error:
                print(f'UART: {error}; reintento en 3 s', file=sys.stderr, flush=True)
                time.sleep(3)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
