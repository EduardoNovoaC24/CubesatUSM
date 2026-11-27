'use strict';
(() => {
  const notice = document.getElementById('usb-notice');
  const buttons = {
    pico: document.getElementById('usb-pico'),
    adsb: document.getElementById('usb-adsb'),
    rp: document.getElementById('usb-rp'),
  };
  const adsbMode = document.getElementById('usb-adsb-mode');
  const adsbSource = document.getElementById('usb-adsb-source');
  const adsbApply = document.getElementById('usb-adsb-apply');
  const adsbFeedback = document.getElementById('usb-adsb-feedback');
  const raspberryHello = document.getElementById('usb-raspberry-hello');
  const raspberryReply = document.getElementById('usb-raspberry-reply');
  let helloTimer;
  function finishHello(message) {
    clearTimeout(helloTimer);
    helloTimer = undefined;
    raspberryHello.disabled = !adsbOnly;
    raspberryReply.textContent = message;
  }
  let adsbOnly = false;
  let adsbPending;
  function clearAdsbPending() {
    if (adsbPending) clearTimeout(adsbPending.timer);
    adsbPending = undefined;
    adsbApply.disabled = !adsbOnly;
  }
  const labels = { pico: 'Pico LoRa', adsb: 'ST-LINK ADS-B', rp: 'Modo RP' };
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
          const record = line.trim();
          if (record) backend.ingest(source, record);
          if (source === 'adsb') {
            if (record === 'ADSB_MODE,ON' || record === 'ADSB_MODE,OFF') {
              adsbOnly = record === 'ADSB_MODE,ON';
              adsbMode.textContent = adsbOnly ? 'Salir de Solo ADS-B' : 'Solo mensajes ADS-B';
              clearAdsbPending();
              adsbSource.disabled = !adsbOnly;
              finishHello('');
              adsbFeedback.textContent = adsbOnly ? 'Solo ADS-B activo. Selecciona una fuente.' : '';
            } else if (record === 'HELLO_SENT,UART4' && helloTimer !== undefined) {
              raspberryReply.textContent = 'STM32 envio el saludo por UART4. Esperando respuesta.';
            } else if (record === 'HELLO_STM32' && helloTimer !== undefined) {
              finishHello('Hola STM32. UART confirmada en ambos sentidos.');
            } else if (record === 'HELLO_ERROR,UART_TX' && helloTimer !== undefined) {
              finishHello('La STM32 no pudo enviar el saludo por UART4.');
            } else if (record.startsWith('ADSB_SOURCE,')) {
              const [, result, decoderName] = record.split(',');
              const pendingSource = adsbPending?.source;
              if (pendingSource && decoderName !== pendingSource &&
                  !decoderName?.startsWith(pendingSource + '_') && decoderName !== 'UART_TX') continue;
              if (pendingSource) clearAdsbPending();
              if (result === 'OK') {
                adsbSource.value = decoderName;
                adsbFeedback.textContent = 'Fuente confirmada: ' + decoderName;
              } else if (decoderName?.endsWith('_NO_SBS')) {
                adsbFeedback.textContent = 'El puerto de la nueva fuente abre, pero no llegan mensajes SBS. Se conserva la fuente anterior.';
              } else {
                adsbFeedback.textContent = 'La Raspberry no pudo activar la fuente: ' + decoderName;
              }
            }
          }
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
      if (source === 'adsb' && adsbOnly) {
        await session.writer?.write(encoder.encode('ADSB OFF\r\n')).catch(() => {});
      }
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
      if (source === 'adsb') {
        adsbOnly = false;
        clearAdsbPending();
        finishHello('');
        adsbMode.disabled = adsbApply.disabled = adsbSource.disabled = true;
        adsbMode.textContent = 'Solo mensajes ADS-B';
        adsbFeedback.textContent = '';
      }
      buttons[source].textContent = source === 'rp' ? 'Modo RP · conectar UART' : 'Conectar ' + labels[source];
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

  async function sendAdsb(command) {
    const session = sessions.adsb;
    if (!session?.writer || session.closing) return;
    if (adsbPending) return;
    const mode = command === 'ADSB ONLY' || command === 'ADSB OFF';
    const request = {source: mode ? undefined : command.slice(5)};
    adsbPending = request;
    adsbApply.disabled = true;
    adsbFeedback.textContent = mode ? 'Esperando confirmacion de la STM32.' :
      'Esperando datos SBS de ' + request.source + '.';
    request.timer = setTimeout(() => {
      if (adsbPending !== request) return;
      clearAdsbPending();
      adsbFeedback.textContent = mode ? 'Sin respuesta de la STM32 por USB.' :
        'Sin confirmacion de la Raspberry. Revisa el puente UART y el puerto SBS.';
    }, mode ? 4000 : 12000);
    try {
      await session.writer.write(encoder.encode(command + '\r\n'));
    } catch (error) {
      if (adsbPending === request) {
        clearAdsbPending();
        adsbFeedback.textContent = error.message;
      }
    }
  }
  raspberryHello.onclick = async () => {
    const session = sessions.adsb;
    if (!adsbOnly || !session?.writer || session.closing || helloTimer !== undefined) return;
    raspberryHello.disabled = true;
    if (!adsbPending) adsbFeedback.textContent = '';
    raspberryReply.textContent = 'Esperando Hola STM32 desde la Raspberry.';
    const timer = setTimeout(() => finishHello('Sin respuesta de la Raspberry por UART4.'), 5000);
    helloTimer = timer;
    try {
      await session.writer.write(encoder.encode('HELLO RASPBERRY\r\n'));
    } catch (error) {
      if (helloTimer === timer) finishHello(error.message);
    }
  };
  adsbMode.onclick = () => void sendAdsb(adsbOnly ? 'ADSB OFF' : 'ADSB ONLY');
  adsbApply.onclick = () => {
    if (adsbOnly) void sendAdsb('ADSB ' + adsbSource.value);
  };

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
        if (source === 'pico' || source === 'adsb') session.writer = port.writable.getWriter();
        backend.link_status(source, true, labels[source] + ' conectado');
        session.reading = read(source, session);
        void session.reading.then((message) => close(source, session, message));
        if (source === 'pico') {
          await session.writer.write(encoder.encode('HELLO\n'));
          if (session.closing) return;
          session.hello = setTimeout(() => {
            if (!session.closing) void session.writer.write(encoder.encode('HELLO\n')).catch(() => {});
          }, 1100);
        }
        button.textContent = 'Desconectar ' + labels[source];
        notice.textContent = source === 'rp'
          ? 'Modo RP activo. El radar usa la UART de la Raspberry Pi.'
          : labels[source] + ' conectado a 115200 baudios.';
        if (source === 'pico') serverTab.disabled = false;
        if (source === 'adsb') adsbMode.disabled = false;
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
