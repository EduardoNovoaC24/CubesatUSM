"""Browser entry points for the same telemetry and command handling as the local server."""

import json
import time

from aircraft_state import State
from pico_bridge import PicoBridge


aircraft = State(False)
aircraft.set_status('Esperando datos ADS-B por LoRa o USB')
rp = State(False)
rp_connected = False
pico = PicoBridge(None, on_aircraft=aircraft.ingest)


def link_status(source, connected, detail):
    global rp_connected
    if source == 'rp':
        rp_connected = connected
        rp.set_status(detail)
        return
    if source == 'pico':
        if not connected:
            pico._flush_packet()
        pico.connected = connected
        pico.capable = False
        pico.status = detail
        pico.last_error = '' if connected else detail
        pico.log('[USB] ' + detail)
    else:
        aircraft.set_status(detail)


def ingest(source, line):
    if source == 'pico':
        pico._parse_line(line)
    elif source == 'rp':
        rp.ingest(line)
    else:
        aircraft.ingest(line)


def snapshot(source):
    if source == 'aircraft':
        state = rp if rp_connected else aircraft
        return json.dumps(state.snapshot(), ensure_ascii=False)
    if pico.pending_packet and time.monotonic() - pico.packet_at > 0.6:
        pico._flush_packet()
    return json.dumps(pico.snapshot(), ensure_ascii=False)


def command(body):
    try:
        payload = json.loads(body)
        response = pico.send_command(payload['command'], payload.get('args', []))
        response['frame'] = pico.queue.get_nowait()
    except (KeyError, ValueError, TypeError, RuntimeError, OverflowError) as error:
        response = {'error': str(error)}
    return json.dumps(response, ensure_ascii=False)
