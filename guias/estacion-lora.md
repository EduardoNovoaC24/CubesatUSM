# CubeSat - Ground Station

## Desde cualquier PC con Chrome

Abre [CubeSat - Ground Station](https://eduardonovoac24.github.io/CubesatUSM/) en Chrome de escritorio. Conecta el Pico LoRa por USB y sigue la pestaña **Conexión** para seleccionar el puerto. Usa **Telemetría** para ver la última trama, **Telecomandos** para enviar solicitudes al STM32 y **Registro** para distinguir mensajes del Pico de respuestas ACK y EVT del STM32.

El selector **Galaxia** cambia entre azul y morado, rojo y verde. Las transiciones y la formación del cubo respetan la preferencia de movimiento reducido del sistema.

La web estática vive en `docs/` y usa Web Serial. Sirve esa carpeta desde HTTPS o localhost si necesitas probar una copia. GitHub Pages se puede configurar desde la rama `main`, carpeta `/docs`.

## Panel local con gráficos y radar

La carpeta `ground_station/` conserva el servidor y el panel original para gráficos, radar ADS-B y telemetría detallada. Sustituye los puertos de ejemplo por los que aparecen en tu PC.

```powershell
py -m pip install pyserial
py ground_station/radar_stm32.py --list-ports
py ground_station/radar_stm32.py --port COM15 --pico-port COM8
```

Abre <http://localhost:8765>. Puedes omitir `--port` si solo usarás el Pico. Cierra el panel local antes de abrir ese mismo puerto desde Chrome.
