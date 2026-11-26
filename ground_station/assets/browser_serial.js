'use strict';
(() => {
  const notice = document.getElementById('usb-notice');
  const buttons = {
    pico: document.getElementById('usb-pico'),
    adsb: document.getElementById('usb-adsb'),
  };
  const labels = { pico: 'Pico LoRa', adsb: 'ST-LINK ADS-B' };
  const sessions = {};
  const serverTab = document.getElementById('server-tab');
  const prepare = document.getElementById('server-prepare');
  const launch = document.getElementById('server-launch');
  const command = document.getElementById('server-command');
  const serverStatus = document.getElementById('server-status');
  let serverPrepared = false;
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
        const response = await fetch('./python/' + name, { cache: 'no-store' });
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
    if (session.closing) return false;
    session.closing = true;
    let closed = false;
    buttons[source].disabled = true;
    clearTimeout(session.hello);
    try {
      if (session.reader) await session.reader.cancel().catch(() => {});
      await session.reading;
      session.writer?.releaseLock();
      await session.port.close();
      closed = true;
    } catch (error) {
      message = error.message;
    } finally {
      backend.link_status(source, false, message);
      delete sessions[source];
      buttons[source].textContent = 'Conectar ' + labels[source];
      buttons[source].disabled = source === 'pico' && serverPrepared;
      notice.textContent = labels[source] + ' · ' + message;
      if (source === 'pico' && !serverPrepared) lockServerTab();
    }
    return closed;
  }

  function lockServerTab() {
    serverTab.disabled = true;
    if (serverTab.getAttribute('aria-selected') === 'true') {
      document.querySelector('[data-target="tab-radar"]').click();
    }
  }

  document.getElementById('server-setup').addEventListener('submit', async (event) => {
    event.preventDefault();
    const port = document.getElementById('server-com').value.trim().toUpperCase();
    const ip = document.getElementById('server-ip').value.trim();
    const parts = ip.split('.');
    const [a, b] = parts.map(Number);
    const privateIp = parts.length === 4 &&
      parts.every(part => /^(0|[1-9]\d{0,2})$/.test(part) && Number(part) <= 255) &&
      (a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168));
    if (!/^COM[1-9]\d{0,4}$/.test(port) || !privateIp) {
      serverStatus.textContent = 'Introduce un puerto COM y una dirección IPv4 privada válidos.';
      return;
    }
    const session = sessions.pico;
    if (!session || session.closing || serverPrepared) return;
    serverPrepared = true;
    prepare.disabled = true;
    if (!await close('pico', session, 'USB liberado para el servidor LAN')) {
      serverPrepared = false;
      prepare.disabled = false;
      buttons.pico.disabled = false;
      serverStatus.textContent = 'No se pudo liberar el Pico. Vuelve a conectarlo antes de reintentar.';
      return;
    }
    command.textContent = `py ground_station/radar_stm32.py --pico-port ${port} --lan-ip ${ip}`;
    document.getElementById('server-open').href = `http://${ip}:8765/`;
    launch.hidden = false;
    serverStatus.textContent = 'USB liberado. El servidor puede abrir el puerto.';
  });

  document.getElementById('server-copy').addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(command.textContent);
      serverStatus.textContent = 'Comando copiado.';
    } catch {
      serverStatus.textContent = 'Selecciona y copia el comando de arriba.';
    }
  });

  document.getElementById('server-cancel').addEventListener('click', () => {
    serverPrepared = false;
    prepare.disabled = false;
    buttons.pico.disabled = false;
    launch.hidden = true;
    serverStatus.textContent = '';
    lockServerTab();
    buttons.pico.focus();
  });

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
        if (source === 'pico') serverTab.disabled = false;
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
