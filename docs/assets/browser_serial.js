'use strict';
(() => {
  const notice = document.getElementById('usb-notice');
  const buttons = {
    pico: document.getElementById('usb-pico'),
    adsb: document.getElementById('usb-adsb'),
  };
  const labels = { pico: 'Pico LoRa', adsb: 'ST-LINK ADS-B' };
  const sessions = {};
  let backend;
  let loading;
  const encoder = new TextEncoder();
  const modules = ['aircraft_state.py', 'pico_bridge.py', 'browser_station.py'];
  const indexURL = 'https://cdn.jsdelivr.net/pyodide/v0.27.7/full/';

  function loadBackend() {
    if (loading) return loading;
    loading = (async () => {
      if (!window.loadPyodide) {
        await new Promise((resolve, reject) => {
          const script = document.createElement('script');
          script.src = indexURL + 'pyodide.js';
          script.onload = resolve;
          script.onerror = () => {
            script.remove();
            reject(new Error('No se pudo descargar el motor de telemetría.'));
          };
          document.head.append(script);
        });
      }
      const python = await window.loadPyodide({ indexURL });
      await Promise.all(modules.map(async (name) => {
        const response = await fetch('./python/' + name);
        if (!response.ok) throw new Error('No se pudo cargar ' + name);
        python.FS.writeFile('/home/pyodide/' + name, await response.text());
      }));
      python.FS.chdir('/home/pyodide');
      backend = python.pyimport('browser_station');
      return backend;
    })().catch((error) => {
      loading = undefined;
      throw error;
    });
    return loading;
  }

  function json(value, status = 200) {
    return new Response(JSON.stringify(value), {
      status, headers: { 'Content-Type': 'application/json' },
    });
  }

  window.stationRequest = async (path, options = {}) => {
    if (path === '/api/aircraft') {
      return json(backend ? JSON.parse(backend.snapshot('aircraft')) :
        { status: 'Esperando conexión USB', total: 0, invalid: 0, aircraft: [] });
    }
    if (path === '/api/pico') {
      return json(backend ? JSON.parse(backend.snapshot('pico')) :
        { connected: false, capable: false, packets: 0, samples: 0, history: [], commands: [], logs: [] });
    }
    if (path === '/api/pico/command' && options.method === 'POST') {
      const session = sessions.pico;
      if (!session?.writer || session.closing || !backend) return json({ error: 'Conecta el Pico por USB' }, 400);
      const { frame, ...result } = JSON.parse(backend.command(options.body));
      if (!frame) return json(result, 400);
      try {
        await session.writer.write(encoder.encode(frame));
        return json(result, 202);
      } catch {
        await close('pico', session, 'Error al escribir al Pico');
        return json({ error: 'No se pudo escribir al Pico' }, 503);
      }
    }
    return json({ error: 'Ruta desconocida' }, 404);
  };

  async function read(source, session) {
    const decoder = new TextDecoder();
    let pending = '';
    let message = 'Puerto desconectado';
    try {
      while (!session.closing) {
        const { value, done } = await session.reader.read();
        if (done || session.closing) break;
        const lines = (pending + decoder.decode(value, { stream: true })).split('\n');
        pending = lines.pop().slice(-4096);
        for (const line of lines) {
          if (line.trim()) backend.ingest(source, line.trim());
        }
      }
    } catch (error) {
      message = error.message;
    } finally {
      session.reader.releaseLock();
    }
    return message;
  }

  async function close(source, session, message) {
    if (session.closing) return;
    session.closing = true;
    buttons[source].disabled = true;
    clearTimeout(session.hello);
    try {
      if (session.reader) await session.reader.cancel().catch(() => {});
      await session.reading;
      session.writer?.releaseLock();
      await session.port.close();
    } catch (error) {
      message = error.message;
    } finally {
      backend.link_status(source, false, message);
      delete sessions[source];
      buttons[source].textContent = 'Conectar ' + labels[source];
      buttons[source].disabled = false;
      notice.textContent = labels[source] + ' · ' + message;
    }
  }

  for (const [source, button] of Object.entries(buttons)) {
    button.addEventListener('click', async () => {
      const existing = sessions[source];
      if (existing) {
        await close(source, existing, 'Desconectado');
        return;
      }
      button.disabled = true;
      let session;
      try {
        const port = await navigator.serial.requestPort();
        notice.textContent = 'Preparando la conexión USB…';
        await loadBackend();
        await port.open({ baudRate: 115200 });
        session = { port };
        sessions[source] = session;
        session.reader = port.readable.getReader();
        if (source === 'pico') session.writer = port.writable.getWriter();
        backend.link_status(source, true, labels[source] + ' conectado');
        session.reading = read(source, session);
        void session.reading.then((message) => close(source, session, message));
        if (session.writer) {
          await session.writer.write(encoder.encode('HELLO\n'));
          if (session.closing) return;
          session.hello = setTimeout(() => {
            if (!session.closing) void session.writer.write(encoder.encode('HELLO\n')).catch(() => {});
          }, 1100);
        }
        button.textContent = 'Desconectar ' + labels[source];
        notice.textContent = labels[source] + ' conectado a 115200 baudios.';
      } catch (error) {
        if (session) await close(source, session, error.message);
        else notice.textContent = error.name === 'NotFoundError' ? 'No se seleccionó un puerto.' : error.message;
      } finally {
        if (!session?.closing) button.disabled = false;
      }
    });
    if (!('serial' in navigator)) button.disabled = true;
  }
  if (!('serial' in navigator)) notice.textContent = 'Usa Chrome de escritorio para conectar por USB.';
})();
