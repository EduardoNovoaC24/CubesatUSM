# Prueba ADS-B por cable

Conexion de UART4 a 115200 baudios, 8N1, con niveles de 3,3 V:

| Raspberry Pi, si UART esta en el conector GPIO | STM32 |
| --- | --- |
| GPIO14 TX, pin fisico 8 | PC11 RX |
| GPIO15 RX, pin fisico 10 | PC10 TX |
| GND, pin fisico 6 | GND |

Une solo las tres senales. En Raspberry Pi 5 verifica si /dev/serial0 apunta al
conector de depuracion. Para usar GPIO14/GPIO15 selecciona la UART habilitada
para esos pines. Habilita la UART y libera la consola de Linux de ese puerto.

## Arranque

Copia `raspberry_adsb_uart.py`, `raspberry_adsb_switch.py` y
`cubesat-adsb-uart.service` a la Raspberry. Por ejemplo, desde el PC y desde
la raíz del proyecto (reemplaza `usuario@raspberrypi.local` por tu acceso SSH):

    scp docs/raspberry_adsb_uart.py docs/raspberry_adsb_switch.py docs/cubesat-adsb-uart.service usuario@raspberrypi.local:~/

En la Raspberry, desde el directorio donde copiaste los tres archivos:

    sudo apt install python3-serial
    sudo install -d /opt/cubesat-adsb
    sudo install -m 0644 raspberry_adsb_uart.py raspberry_adsb_switch.py /opt/cubesat-adsb/
    sudo install -m 0644 cubesat-adsb-uart.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable --now cubesat-adsb-uart.service
    journalctl -u cubesat-adsb-uart.service -f

El servicio inicia un solo puente al arrancar y se reconecta si readsb demora
en publicar. Si los pines 8 y 10 usan otro dispositivo, ejecuta
`sudo systemctl edit cubesat-adsb-uart.service` y agrega:

    [Service]
    Environment=ADSB_UART=/dev/ttyAMA0

Usa el dispositivo que corresponda a tus pines, luego reinicia el servicio
con `sudo systemctl restart cubesat-adsb-uart.service`. Deja habilitada la UART
en Raspberry Pi OS y desactiva la consola serial en esa UART. Evita ejecutar
un segundo puente en el mismo puerto.

El puente espera SBS de readsb en 127.0.0.1:30003. Puedes usar --readsb-port para
cambiarlo. Para more1090 espera otro flujo SBS, por defecto 127.0.0.1:31003.

more1090 entrega AVR crudo y necesita IQ a 8 MSps. Para compararlo en el mapa,
entrega su salida AVR a una instancia independiente de readsb en modo red.
Por ejemplo, un adaptador local puede escuchar AVR en 31001 y entregar SBS en
31003:

    readsb --net-only --net --net-bind-address 127.0.0.1 --net-ri-port 31001 --net-sbs-port 31003

Envia la salida AVR de la instancia more1090 a 127.0.0.1:31001 con tu pipeline
de captura. El puente selecciona el flujo que se le indique. Los dos
productores deben estar disponibles para conmutar sin detener la prueba.
Si solo tienes un SDR, usa una grabacion para comparar los dos decodificadores
con la misma entrada o configura la captura de uno a la vez.

## Estacion web

Con el STM32 actualizado, las tramas ADS-B v2 se reenvían automáticamente
desde UART4 por el SX1262. El Pico LoRa conectado al PC las entrega al radar
de la estación web. Conecta el Pico y abre **Radar ADS-B**. Los mensajes
pueden llegar espaciados por el presupuesto de aire y las órdenes y la
telemetría de la rueda tienen prioridad. Solo las aeronaves con posición
aparecen en el mapa.

Para diagnosticar por cable sin depender de LoRa:

Desde la raiz del proyecto sirve la version local:

    python -m http.server 8766 --bind 127.0.0.1 --directory docs

En Chrome o Edge abre http://localhost:8766. Usa Conectar ST-LINK ADS-B,
elige el COM de la STM32 y pulsa Solo mensajes ADS-B. Selecciona readsb o
more1090 y pulsa Cambiar fuente. La pagina confirma el cambio cuando el
puente Raspberry consigue conectarse al flujo seleccionado. Si falla, conserva
el flujo anterior cuando sigue disponible.

El comando de entrada es ADSB ONLY, y de salida ADSB OFF. Solo admite
ADSB READSB y ADSB MORE1090 para cambiar de fuente. Los datos son tramas
ADSB version 2. El radar separa las capturas al cambiar de fuente. La prueba
transporta los datos por cable aunque LoRa no este conectado.
