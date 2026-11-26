const byId = (id) => document.getElementById(id);
const connect = byId('connect');
const disconnect = byId('disconnect');
const status = byId('status');
const logView = byId('logs');
const command = byId('command');
const argument = byId('argument');
const fields = byId('fields');
const commandStatus = byId('command-status');
const encoder = new TextEncoder();
const argumentCommands = new Set(['SET_MODE', 'SET_BETA', 'SET_RATE', 'RELAY', 'WHEEL']);
const logs = [];
let session;
let nextId = 1 + crypto.getRandomValues(new Uint16Array(1))[0] % 65535;

function log(line) {
  logs.push(line);
  if (logs.length > 150) logs.shift();
  logView.textContent = logs.join('\n');
  logView.scrollTop = logView.scrollHeight;
}

function receive(link, line) {
  log(line);
  if (line === 'PICO_READY,P2P_CMD_V1' || line === 'PICO_NOT_READY') {
    link.ready = line === 'PICO_READY,P2P_CMD_V1';
    fields.disabled = !link.ready;
    status.textContent = link.ready ? 'Pico conectado · radio lista' : 'Pico conectado · radio sin inicializar';
  }
  const payload = /^\[RX\] Payload: "(.*)"$/.exec(line)?.[1]
    ?? /^\[RX\] (.*?) \| len=/.exec(line)?.[1] ?? line;
  if (/^(TEL,|Q[,;])/.test(payload)) byId('telemetry').textContent = payload;
  if (/^(ACK|EVT|PICO_TX|PICO_ACK_TIMEOUT),/.test(payload)) commandStatus.textContent = payload;
}

async function read(link) {
  const decoder = new TextDecoder();
  let partial = '';
  try {
    while (!link.closing) {
      const { value, done } = await link.reader.read();
      if (done) break;
      const lines = (partial + decoder.decode(value, { stream: true })).split('\n');
      partial = lines.pop().slice(-2048);
      for (const line of lines) {
        if (line.trim()) receive(link, line.trim());
      }
    }
    return 'Pico desconectado.';
  } catch (error) {
    return 'Se interrumpió la lectura: ' + error.message;
  } finally {
    link.reader.releaseLock();
  }
}

async function write(link, line) {
  const writer = link.port.writable.getWriter();
  try {
    await writer.write(encoder.encode(line + '\n'));
  } finally {
    writer.releaseLock();
  }
}

async function close(link, message) {
  if (link.closing) return;
  link.closing = true;
  disconnect.disabled = true;
  fields.disabled = true;
  clearTimeout(link.helloTimer);
  try {
    if (link.reader) await link.reader.cancel().catch(() => {});
    await link.reading;
    await link.port.close();
  } catch (error) {
    log('[PC] ' + error.message);
  } finally {
    session = undefined;
    connect.disabled = false;
    status.textContent = message;
  }
}

connect.addEventListener('click', async () => {
  connect.disabled = true;
  let port;
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
  } catch (error) {
    connect.disabled = false;
    status.textContent = error.name === 'NotFoundError' ? 'No se seleccionó un puerto.' : error.message;
    return;
  }
  const link = { port, reader: port.readable.getReader(), ready: false };
  session = link;
  disconnect.disabled = false;
  status.textContent = 'Esperando confirmación del Pico…';
  link.reading = read(link);
  void link.reading.then((message) => close(link, message));
  try {
    await write(link, 'HELLO');
    if (!link.closing) link.helloTimer = setTimeout(async () => {
      if (!link.ready && !link.closing) {
        try {
          await write(link, 'HELLO');
        } catch (error) {
          log('[PC] ' + error.message);
        }
      }
    }, 1500);
  } catch (error) {
    await close(link, error.message);
  }
});

disconnect.addEventListener('click', () => {
  if (session) void close(session, 'Pico desconectado.');
});

command.addEventListener('change', () => {
  argument.disabled = !argumentCommands.has(command.value);
  argument.value = '';
});

byId('controls').addEventListener('submit', async (event) => {
  event.preventDefault();
  const link = session;
  if (!link?.ready || link.closing) return;
  const name = command.value;
  const value = argument.value.trim();
  const hasArgument = argumentCommands.has(name);
  if (hasArgument && !/^[A-Za-z0-9._+-]{1,32}$/.test(value)) {
    commandStatus.textContent = 'Introduce un argumento válido.';
    return;
  }
  const id = nextId;
  nextId = nextId % 65535 + 1;
  const frame = ['CMD', id, name, ...(hasArgument ? [value] : [])].join(',');
  commandStatus.textContent = 'Enviando #' + id + ' al Pico…';
  try {
    await write(link, frame);
    log('[PC] ' + frame);
  } catch (error) {
    commandStatus.textContent = 'No se pudo enviar: ' + error.message;
  }
});

if (!('serial' in navigator)) {
  connect.disabled = true;
  status.textContent = 'Abre esta página por HTTPS en Chrome de escritorio para usar el puerto serie.';
}
