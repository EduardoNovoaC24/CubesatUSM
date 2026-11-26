"""Generate the static site from the original Ground Station panel."""

from hashlib import sha256
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parent
PAGES = ROOT.parent / 'docs'

toolbar = """
<div class="usb-toolbar" aria-label="Conexión y aspecto">
  <button id="usb-pico">Conectar Pico LoRa</button>
  <button id="usb-adsb">Conectar ST-LINK ADS-B</button>
  <label for="theme">Galaxia</label>
  <select id="theme">
    <option value="nebula">Nebulosa azul y violeta</option>
    <option value="red">Polvo estelar rojo</option>
    <option value="green">Nebulosa verde</option>
  </select>
  <p id="usb-notice" role="status">En Chrome, conecta el Pico por USB, cierra el monitor serie y selecciona su puerto.</p>
</div>
<canvas id="stars" aria-hidden="true"></canvas>
"""
server_tab = '<button id="server-tab" class="tab-btn" data-target="tab-servidor" aria-selected="false" disabled>Modo servidor</button>'
server_panel = """
<section class="tab-panel cube-panel" id="tab-servidor" aria-label="Modo servidor">
  <div class="panel-head"><div><h2>Modo servidor</h2><p>Comparte este Pico con otros PCs de tu red local.</p></div></div>
  <div class="web-card server-setup">
    <form id="server-setup">
      <label>Puerto COM del Pico <input id="server-com" placeholder="COM8" required maxlength="8"></label>
      <label>IPv4 de este PC <input id="server-ip" placeholder="192.168.1.23" required maxlength="15"></label>
      <button class="cmd-btn" id="server-prepare" type="submit">Liberar USB y preparar comando</button>
    </form>
    <p>Consulta el puerto con <code>py ground_station/radar_stm32.py --list-ports</code> y la IPv4 con <code>ipconfig</code>.</p>
    <div id="server-launch" hidden>
      <p>Ejecuta este comando en PowerShell desde la carpeta del repositorio. Cuando la terminal muestre la URL y la clave temporal, abre el panel y comparte esa dirección.</p>
      <pre id="server-command"></pre>
      <button class="cmd-btn" id="server-copy" type="button">Copiar comando</button>
      <a class="cmd-btn" id="server-open">Abrir panel LAN</a>
      <button class="cmd-btn" id="server-cancel" type="button">Volver al modo USB</button>
    </div>
    <p id="server-status" role="status"></p>
  </div>
</section>
"""
page = (ROOT / 'templates' / 'index.html').read_text(encoding='utf-8')
page = page.replace('"/assets/', '"./assets/')
page = page.replace('</head>', '<link rel="stylesheet" href="./assets/galaxy.css">\n</head>')
page = page.replace('</header>', '</header>\n' + toolbar, 1)
page = page.replace('</nav>', server_tab + '\n</nav>\n' + server_panel, 1)
page = page.replace('<script src="https://unpkg.com/leaflet', '<script src="./assets/browser_serial.js"></script>\n<script src="https://unpkg.com/leaflet', 1)
page = page.replace('</body>', '<script defer src="./assets/galaxy.js"></script>\n</body>')
page = page.replace('Comprueba COM y 115200 baudios.', 'Conecta el Pico LoRa o el puerto ST-LINK.')
for name in ('cubesat_web.css', 'cubesat_web.js', 'galaxy.css', 'galaxy.js', 'browser_serial.js'):
    version = sha256((ROOT / 'assets' / name).read_bytes()).hexdigest()[:10]
    page = page.replace(f'"./assets/{name}"', f'"./assets/{name}?v={version}"')
PAGES.mkdir(exist_ok=True)
(PAGES / 'index.html').write_text(page, encoding='utf-8')
shutil.copytree(ROOT / 'assets', PAGES / 'assets', dirs_exist_ok=True)
(PAGES / 'python').mkdir(exist_ok=True)
for name in ('aircraft_state.py', 'pico_bridge.py', 'browser_station.py'):
    shutil.copyfile(ROOT / name, PAGES / 'python' / name)
