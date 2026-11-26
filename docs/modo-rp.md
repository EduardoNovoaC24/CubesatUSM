# Modo RP para ADS-B

La Raspberry Pi lee las tramas SBS de readsb por TCP y transmite las observaciones
por UART a 115200 baudios, 8N1. Cada observacion incluye los campos disponibles de
ICAO, indicativo, altitud, velocidad, rumbo, posicion, velocidad vertical y squawk.

1. En la Raspberry Pi, habilita la UART de hardware y deshabilita la consola
   serie con sudo raspi-config. Comprueba que el dispositivo Linux corresponda
   al conector que estas usando. En Raspberry Pi 5, el puerto primario puede
   corresponder al conector de depuracion dedicado.
2. Conecta el TX de esa UART al RX de un adaptador USB-UART con niveles logicos
   de 3,3 V. Une GND con GND. Deja VCC sin conectar y conecta el adaptador al PC.
3. Instala y ejecuta el script en la Raspberry. Se asume que readsb ya entrega
   SBS en 127.0.0.1:30003.

       sudo apt install python3-serial
       python3 raspberry_adsb_uart.py --uart /dev/serial0

   Para comprobar el flujo SBS por consola, ejecuta
   python3 raspberry_adsb_uart.py --stdout. Los parametros --host y --port
   permiten usar readsb desde otro equipo.

4. Abre CubeSat Ground Station en Chrome o Edge de escritorio, pulsa
   **Modo RP**, y selecciona el puerto COM del adaptador. El radar y el
   registro usan esa fuente hasta que desconectes Modo RP. El Pico puede
   mantenerse conectado para la telemetria y los mandos de la STM32.

Para el servidor local de la estacion usa
py radar_stm32.py --rp-port COM7 desde ground_station, reemplazando COM7
por tu adaptador. Puedes agregar --pico-port COM8 con el puerto del Pico.

Guia oficial de UART de Raspberry Pi:
https://www.raspberrypi.com/documentation/computers/configuration.html#configuring-uarts
