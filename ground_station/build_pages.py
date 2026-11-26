"""Generate the static site from the original Ground Station panel."""

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
<script src="./assets/browser_serial.js"></script>
"""
page = (ROOT / 'templates' / 'index.html').read_text(encoding='utf-8')
page = page.replace('"/assets/', '"./assets/')
page = page.replace('</head>', '<link rel="stylesheet" href="./assets/galaxy.css">\n</head>')
page = page.replace('</header>', '</header>\n' + toolbar, 1)
page = page.replace('</body>', '<script defer src="./assets/galaxy.js"></script>\n</body>')
page = page.replace('Comprueba COM y 115200 baudios.', 'Conecta el Pico LoRa o el puerto ST-LINK.')
PAGES.mkdir(exist_ok=True)
(PAGES / 'index.html').write_text(page, encoding='utf-8')
shutil.copytree(ROOT / 'assets', PAGES / 'assets', dirs_exist_ok=True)
(PAGES / 'python').mkdir(exist_ok=True)
for name in ('aircraft_state.py', 'pico_bridge.py', 'browser_station.py'):
    shutil.copyfile(ROOT / name, PAGES / 'python' / name)
