"""Shared ADS-B parsing and radar state."""

import math
import re
import threading
import time

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
