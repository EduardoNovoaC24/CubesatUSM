#!/usr/bin/env python3
"""Forward a selectable local SBS feed to the STM32 over a full-duplex UART."""

import argparse
import select
import socket
import time

from raspberry_adsb_uart import SbsRelay


def run(uart, ports):
    source = 'READSB'
    feed = None
    candidate = None
    candidate_name = None
    candidate_until = 0.0
    candidate_data = b''
    candidate_decoder = None
    decoder = SbsRelay()
    packet = b''
    pending = bytearray()
    retry_at = 0.0

    def report(state, detail):
        uart.write(f'ADSB_SOURCE,{state},{detail}\n'.encode('ascii'))

    def read_feed(connection, buffered, relay):
        try:
            chunk = connection.recv(4096)
        except OSError:
            return None, []
        if not chunk:
            return None, []
        *lines, remainder = (buffered + chunk).split(b'\n')
        frames = []
        for line in lines:
            frame = relay.frame(line.decode('ascii', errors='replace'))
            if frame:
                frames.append(frame)
        return remainder if len(remainder) < 4096 else b'', frames

    try:
        while True:
            now = time.monotonic()
            if candidate is not None and now >= candidate_until:
                candidate.close()
                candidate = None
                report('ERR', candidate_name + '_NO_SBS')
            if feed is None and now >= retry_at:
                try:
                    feed = socket.create_connection(('127.0.0.1', ports[source]), timeout=1)
                except OSError:
                    retry_at = time.monotonic() + 3
                    report('ERR', source + '_UNAVAILABLE')
                else:
                    print(f'SBS {source} conectado en {ports[source]}', flush=True)
                    report('OK', source)

            inputs = [uart] + [conn for conn in (feed, candidate) if conn is not None]
            ready, _, _ = select.select(inputs, [], [], 0.2)
            if uart in ready:
                pending.extend(uart.read(uart.in_waiting or 1))
                *commands, remainder = pending.split(b'\n')
                pending = bytearray(remainder if len(remainder) < 128 else b'')
                for raw in commands:
                    command = raw.strip()
                    if command == b'HELLO_RASPBERRY':
                        uart.write(b'HELLO_STM32\n')
                        continue
                    if command not in (b'SOURCE,READSB', b'SOURCE,MORE1090'):
                        continue
                    requested = command.split(b',')[1].decode('ascii')
                    if candidate is not None:
                        candidate.close()
                        candidate = None
                    if requested == source and feed is not None:
                        report('OK', source)
                        continue
                    try:
                        candidate = socket.create_connection(('127.0.0.1', ports[requested]), timeout=1)
                    except OSError:
                        report('ERR', requested + '_UNAVAILABLE')
                    else:
                        candidate_name = requested
                        candidate_until = time.monotonic() + 8
                        candidate_data = b''
                        candidate_decoder = SbsRelay()

            if candidate is not None and candidate in ready:
                candidate_data, frames = read_feed(candidate, candidate_data, candidate_decoder)
                if candidate_data is None:
                    candidate.close()
                    candidate = None
                    report('ERR', candidate_name + '_DISCONNECTED')
                elif frames:
                    if feed is not None:
                        feed.close()
                    feed, source, decoder, packet = candidate, candidate_name, candidate_decoder, candidate_data
                    candidate = None
                    report('OK', source)
                    for frame in frames:
                        uart.write(frame)
                    continue

            if feed is None or feed not in ready:
                continue
            packet, frames = read_feed(feed, packet, decoder)
            if packet is None:
                feed.close()
                feed, packet = None, b''
                decoder = SbsRelay()
                retry_at = time.monotonic() + 3
                report('ERR', source + '_DISCONNECTED')
                continue
            for frame in frames:
                uart.write(frame)
            decoder.expire()
    finally:
        for connection in (feed, candidate):
            if connection is not None:
                connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--uart', default='/dev/serial0')
    parser.add_argument('--readsb-port', type=int, default=30003)
    parser.add_argument('--more1090-port', type=int, default=31003)
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        parser.exit(1, 'Install pyserial: sudo apt install python3-serial\n')
    with serial.Serial(args.uart, 115200, timeout=0, write_timeout=1) as uart:
        try:
            run(uart, {'READSB': args.readsb_port, 'MORE1090': args.more1090_port})
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
