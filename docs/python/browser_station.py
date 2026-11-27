"""Browser entry points for the same telemetry and command handling as the local server."""

import json
import time

from aircraft_state import State
from pico_bridge import PicoBridge


aircraft = State(False)
aircraft.set_status('Esperando datos ADS-B por LoRa o USB')
rp = State(False)
rp_connected = False
stm_adsb = State(False)
stm_adsb_mode = False
stm_adsb_source = None
pico = PicoBridge(None, on_aircraft=aircraft.ingest)


def link_status(source, connected, detail):
    global rp_connected, stm_adsb_mode, stm_adsb_source
    if source == 'rp':
        rp_connected = connected
        rp.set_status(detail)
        return
    if source == 'adsb' and not connected:
        stm_adsb_mode = False
        stm_adsb_source = None
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
    global stm_adsb, stm_adsb_mode, stm_adsb_source
    if source == 'adsb':
        if line == 'ADSB_MODE,ON':
            stm_adsb = State(False)
            stm_adsb.set_status('Conectado por STM32 UART; selecciona un decodificador')
            stm_adsb_mode = True
            stm_adsb_source = None
            return
        if line == 'ADSB_MODE,OFF':
            stm_adsb_mode = False
            stm_adsb_source = None
            return
        if stm_adsb_mode:
            if line in ('ADSB_SOURCE,OK,READSB', 'ADSB_SOURCE,OK,MORE1090'):
                decoder = line.split(',')[-1]
                if stm_adsb_source != decoder:
                    stm_adsb = State(False)
                stm_adsb_source = decoder
                stm_adsb.set_status('Conectado por STM32 UART / ' + decoder)
            elif line.startswith('ADSB_SOURCE,ERR,'):
                stm_adsb.set_status('Conectado por STM32 UART / ' + (stm_adsb_source or 'sin fuente'),
                                    line.split(',', 2)[2])
            elif line.startswith('$ADSB,2,'):
                stm_adsb.ingest(line)
            return
    if source == 'pico':
        pico._parse_line(line)
    elif source == 'rp':
        rp.ingest(line)
    else:
        aircraft.ingest(line)


def snapshot(source):
    if source == 'aircraft':
        state = stm_adsb if stm_adsb_mode else rp if rp_connected else aircraft
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
