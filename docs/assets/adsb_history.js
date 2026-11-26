'use strict';

(() => {
  const byId = id => document.getElementById(id);
  const flights = new Map();
  const latest = new Map();
  const cursors = new Map();
  const liveLayers = new Map();
  const recordedAt = new Intl.DateTimeFormat('es-CL', {dateStyle: 'short', timeStyle: 'medium'});
  const label = flight => flight.name || flight.icao;
  const position = sample => Number.isFinite(sample.lat) && Number.isFinite(sample.lon);
  const coordinates = sample => [sample.lat, sample.lon];
  const format = (value, unit = '') => value == null ? '—' : value + unit;
  let database, selected, playback, playbackMap, replayLayers, animation, replayFrame;
  let flightListDirty = true;

  const storageError = () => {
    byId('adsb-storage').textContent = 'No se pudo guardar en este navegador. Puedes exportar los vuelos de esta sesión.';
  };
  const loaded = new Promise(resolve => {
    let connection;
    try {
      connection = indexedDB.open('cubesat-adsb', 1);
    } catch {
      storageError();
      resolve();
      return;
    }
    connection.onupgradeneeded = () => connection.result.createObjectStore('flights', {keyPath: 'id'});
    connection.onerror = () => { storageError(); resolve(); };
    connection.onsuccess = () => {
      database = connection.result;
      const request = database.transaction('flights').objectStore('flights').getAll();
      request.onsuccess = () => {
        for (const flight of request.result) {
          flights.set(flight.id, flight);
          const key = flight.stream + ':' + flight.icao;
          if (!latest.has(key) || latest.get(key).end < flight.end) latest.set(key, flight);
          cursors.set(flight.stream, Math.max(cursors.get(flight.stream) ?? 0, flight.samples.at(-1).seq));
        }
        renderList();
        resolve();
      };
      request.onerror = () => { storageError(); resolve(); };
    };
  });

  function persist(records) {
    if (!database || !records.length) return;
    try {
      const transaction = database.transaction('flights', 'readwrite');
      transaction.onerror = transaction.onabort = storageError;
      const store = transaction.objectStore('flights');
      for (const flight of records) store.put(flight);
    } catch {
      storageError();
    }
  }

  function renderList() {
    if (!flightListDirty) return;
    flightListDirty = false;
    const savedOnly = byId('adsb-saved-only').checked;
    const list = byId('adsb-flights');
    const focused = document.activeElement?.dataset?.flight;
    const items = [...flights.values()].filter(f => !f.demo && (!savedOnly || f.saved));
    items.sort((a, b) => b.start - a.start);
    const buttons = items.map(flight => {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'plane-row';
      button.dataset.flight = flight.id;
      button.setAttribute('aria-pressed', String(selected?.id === flight.id));
      button.textContent = [label(flight), flight.icao, recordedAt.format(flight.start * 1000),
        flight.saved ? 'Guardado' : ''].filter(Boolean).join(' · ');
      button.onclick = () => openFlight(flight, true);
      return button;
    });
    list.replaceChildren(...buttons);
    if (!buttons.length) list.textContent = savedOnly ? 'Todavía no has guardado vuelos.' : 'Esperando aeronaves ADS-B.';
    if (focused) buttons.find(button => button.dataset.flight === focused)?.focus();
  }

  function connected(a, b) {
    const seconds = b.t - a.t;
    if (seconds < 0 || seconds > 60 || Math.abs(b.lon - a.lon) > 180) return false;
    const rad = Math.PI / 180;
    const h = Math.sin((b.lat - a.lat) * rad / 2) ** 2 +
      Math.cos(a.lat * rad) * Math.cos(b.lat * rad) * Math.sin((b.lon - a.lon) * rad / 2) ** 2;
    const distance = 12742000 * Math.asin(Math.sqrt(Math.min(1, h)));
    return distance <= Math.max(250, seconds * 620);
  }

  function segments(points) {
    const lines = [];
    let previous;
    for (const sample of points) {
      if (!previous || !connected(previous, sample)) lines.push([]);
      lines.at(-1).push(coordinates(sample));
      previous = sample;
    }
    return lines;
  }

  function updateLive(data, map) {
    if (!map) return;
    const active = new Set();
    for (const aircraft of data.aircraft) {
      if (!position(aircraft)) continue;
      const flight = latest.get(data.stream + ':' + aircraft.icao);
      if (!flight) continue;
      active.add(flight.id);
      let layer = liveLayers.get(flight.id);
      if (!layer) {
        const color = 'hsl(' + (parseInt(flight.icao, 16) % 360) + ' 83% 65%)';
        layer = L.polyline([], {color, weight: 3, opacity: .8, interactive: false}).addTo(map);
        liveLayers.set(flight.id, layer);
      }
      layer.setLatLngs(segments(flight.samples.slice(-1000).filter(position).slice(-250)));
    }
    for (const [id, layer] of liveLayers) {
      if (active.has(id)) continue;
      layer.remove();
      liveLayers.delete(id);
    }
  }

  async function record(data, map) {
    await loaded;
    if (!data.stream || !Array.isArray(data.observations)) return;
    let cursor = cursors.get(data.stream) ?? 0;
    if (!data.demo && data.observations[0]?.seq > cursor + 1) {
      byId('adsb-gap').textContent = 'Hay tramas fuera del intervalo recuperable. El registro continúa desde la próxima muestra disponible.';
    }
    const changed = new Set();
    for (const sample of data.observations) {
      if (sample.seq <= cursor) continue;
      cursor = sample.seq;
      const key = data.stream + ':' + sample.icao;
      let flight = latest.get(key);
      if (!flight || sample.t - flight.end >= 120) {
        flight = {id: data.stream + ':' + sample.seq, stream: data.stream, icao: sample.icao,
          start: sample.t, end: sample.t, name: '', saved: false, demo: data.demo, samples: []};
        latest.set(key, flight);
        if (!data.demo) {
          flights.set(flight.id, flight);
          flightListDirty = true;
        }
      }
      if (sample.flight && sample.flight !== sample.icao && sample.flight !== flight.name) {
        flight.name = sample.flight;
        flightListDirty = true;
      }
      flight.samples.push(sample);
      flight.end = sample.t;
      if (data.demo && flight.samples.length > 512) flight.samples.shift();
      if (!data.demo) changed.add(flight);
    }
    cursors.set(data.stream, cursor);
    persist([...changed]);
    updateLive(data, map);
    if (byId('tab-vuelos').classList.contains('active')) renderList();
  }

  const tab = document.querySelector('[data-target="tab-vuelos"]');
  tab.addEventListener('click', () => {
    renderList();
    requestAnimationFrame(() => playbackMap?.invalidateSize());
  });

  function labelSave() {
    const button = byId('adsb-save');
    button.textContent = selected?.saved ? 'Quitar de guardados' : 'Guardar vuelo';
    button.setAttribute('aria-pressed', String(Boolean(selected?.saved)));
  }

  function openFlight(flight, autoplay) {
    pause();
    selected = flight;
    const samples = flight.samples.slice();
    const points = samples.filter(position);
    byId('adsb-flight-title').textContent = label(flight) + ' · ' + flight.icao;
    byId('adsb-flight-caption').textContent = recordedAt.format(samples[0].t * 1000) + ' — ' +
      recordedAt.format(samples.at(-1).t * 1000) + ' · ' + samples.length + ' mensajes';
    byId('adsb-save').disabled = byId('adsb-export-flight').disabled = false;
    labelSave();
    flightListDirty = true;
    renderList();
    const mapAvailable = Boolean(window.L);
    if (mapAvailable && !playbackMap) {
      playbackMap = L.map('adsb-replay-map').setView([-33.45, -70.66], 9);
      L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
        maxZoom: 19, attribution: '&copy; OpenStreetMap contributors'
      }).addTo(playbackMap);
      replayLayers = L.layerGroup().addTo(playbackMap);
    }
    replayLayers?.clearLayers();
    const start = points[0]?.t ?? samples[0].t;
    const end = points.at(-1)?.t ?? samples.at(-1).t;
    playback = {points, samples, start, end, marker: null, progress: 0};
    byId('adsb-play').disabled = byId('adsb-seek').disabled = points.length < 2 || end <= start || !mapAvailable;
    if (mapAvailable && points.length) {
      playbackMap.invalidateSize();
      playbackMap.fitBounds(L.latLngBounds(points.map(coordinates)).pad(.15), {maxZoom: 13});
      L.polyline(segments(points), {color: '#69d7ef', weight: 4}).addTo(replayLayers);
      playback.marker = L.circleMarker(coordinates(points[0]), {
        color: '#fff', fillColor: '#ffc06a', fillOpacity: 1, radius: 7
      }).addTo(replayLayers);
    }
    byId('adsb-position-note').textContent = !mapAvailable ? 'Mapa no disponible.' :
      points.length === 0 ? 'Este vuelo no envió coordenadas.' :
      points.length === 1 ? 'Solo hay una posición. Se muestra en el mapa.' :
      'Trayectoria entre la primera y la última posición recibidas.';
    seek(0);
    if (autoplay && !byId('adsb-play').disabled) play();
  }

  function sampleIndex(samples, time) {
    let first = 0;
    let last = samples.length;
    while (first < last) {
      const mid = (first + last) >> 1;
      if (samples[mid].t <= time) first = mid + 1;
      else last = mid;
    }
    return Math.max(0, first - 1);
  }

  function seek(progress) {
    playback.progress = progress;
    byId('adsb-seek').value = progress * 1000;
    const {start, end, points, samples, marker} = playback;
    const time = start + progress * (end - start);
    const sample = samples[sampleIndex(samples, time)];
    if (marker) {
      const i = sampleIndex(points, time);
      const a = points[i], b = points[i + 1];
      const fraction = b && b.t > a.t && connected(a, b) ? (time - a.t) / (b.t - a.t) : 0;
      marker.setLatLng(fraction ? [a.lat + (b.lat - a.lat) * fraction, a.lon + (b.lon - a.lon) * fraction] : coordinates(a));
    }
    byId('adsb-replay-data').textContent = [
      recordedAt.format(time * 1000), 'Altitud: ' + format(sample.altitude, ' ft'),
      'Velocidad: ' + format(sample.speed, ' kt'), 'Rumbo: ' + format(sample.heading, '°'),
      'Ascenso: ' + format(sample.vertical_rate, ' ft/min'), 'Squawk: ' + format(sample.squawk),
      'Posición: ' + (position(sample) ? sample.lat.toFixed(5) + ', ' + sample.lon.toFixed(5) : '—')
    ].join(' · ');
  }

  function pause() {
    animation = null;
    cancelAnimationFrame(replayFrame);
    byId('adsb-play').textContent = 'Reproducir';
  }

  function play() {
    if (playback.progress >= 1) seek(0);
    const duration = Math.max(5000, Math.min(60000, (playback.end - playback.start) * 1000));
    animation = performance.now() - playback.progress * duration;
    byId('adsb-play').textContent = 'Pausar';
    const step = timestamp => {
      const progress = Math.max(0, Math.min(1, (timestamp - animation) / duration));
      seek(progress);
      if (progress >= 1) pause();
      else replayFrame = requestAnimationFrame(step);
    };
    replayFrame = requestAnimationFrame(step);
  }

  byId('adsb-play').onclick = () => animation === null ? play() : pause();
  byId('adsb-seek').oninput = event => { pause(); seek(Number(event.target.value) / 1000); };
  byId('adsb-saved-only').onchange = () => { flightListDirty = true; renderList(); };
  byId('adsb-save').onclick = () => {
    if (!selected) return;
    selected.saved = !selected.saved;
    persist([selected]);
    labelSave();
    flightListDirty = true;
    renderList();
  };
  byId('adsb-export-flight').onclick = () => {
    if (!selected) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(selected, null, 2)], {type: 'application/json'}));
    const link = document.createElement('a');
    link.href = url;
    link.download = 'ADSB-' + selected.icao + '-' + Math.floor(selected.start) + '.json';
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };

  window.adsbHistory = {
    record,
    open(aircraft, stream) {
      const flight = latest.get(stream + ':' + aircraft.icao);
      if (!flight || flight.demo) return;
      tab.click();
      openFlight(flight, false);
    }
  };
})();
