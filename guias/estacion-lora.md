# CubeSat - Ground Station

## Desde cualquier PC con Chrome

Abre [CubeSat - Ground Station](https://eduardonovoac24.github.io/CubesatUSM/) en Chrome de escritorio. Conecta el Pico LoRa por USB, pulsa **Conectar Pico LoRa** y selecciona su puerto. La página conserva las pestañas de radar ADS-B, telemetría, calibración y control, gráficos, misión y paneles solares de la estación local.

El radar recibe las tramas ADS-B que lleguen por LoRa. Si tienes el STM32 conectado por ST-LINK, usa **Conectar ST-LINK ADS-B** para leer también ese puerto.

El selector **Galaxia** cambia entre azul y morado, rojo y verde. Las transiciones y la formación del cubo respetan la preferencia de movimiento reducido del sistema.

La web estática usa Web Serial y ejecuta en el navegador los mismos analizadores Python del panel local. La primera conexión carga Pyodide desde su CDN. Para regenerar `docs/`, ejecuta `python ground_station/build_pages.py`. GitHub Pages publica `/docs` de la rama `main`.

## Panel local con gráficos y radar

La carpeta `ground_station/` conserva el servidor y el panel original para gráficos, radar ADS-B y telemetría detallada. Sustituye los puertos de ejemplo por los que aparecen en tu PC.

```powershell
py -m pip install pyserial
py ground_station/radar_stm32.py --list-ports
py ground_station/radar_stm32.py --port COM15 --pico-port COM8
```

Abre <http://localhost:8765>. Puedes omitir `--port` si solo usarás el Pico. Cierra el panel local antes de abrir ese mismo puerto desde Chrome.

## Compartir una radio en la misma red

En el PC que tiene el Pico por USB, abre la web y pulsa **Conectar Pico LoRa**. Al abrir el puerto se habilita **Modo servidor**. Introduce el COM y la IPv4 del adaptador de red (puedes consultarla con `ipconfig`), pulsa **Liberar USB y preparar comando**, y copia el comando a PowerShell desde la carpeta del repositorio. Cuando la terminal confirme el arranque, pulsa **Abrir panel LAN**.

También puedes iniciar el servidor directamente desde la terminal si el Pico está libre. Sustituye el COM y la IPv4 del ejemplo por los valores de tu PC.

```powershell
py ground_station/radar_stm32.py --pico-port COM8 --lan-ip 192.168.1.23
```

Ambos abren la URL que imprime la terminal, por ejemplo `http://192.168.1.23:8765`. Si Windows pregunta por el firewall, autoriza Python en redes privadas. La telemetría está disponible en la red local. Para enviar comandos, cada operador escribe su nombre y la clave temporal que aparece en la terminal del anfitrión.

Prueben primero con `PING` y comprueben que ambos ven el mismo `ACK`. El primer comando que modifica el equipo reserva el turno durante 60 segundos de inactividad. `MAG_START` lo reserva durante 125 segundos para cubrir la calibración. El operador puede soltarlo desde el panel. Consultas de estado y `WHEEL STOP` no necesitan el turno, pero sí la clave de mandos.
