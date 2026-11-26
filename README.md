# Firmware Pico LoRa P2P

El Pico sirve como puente USB entre **CubeSat - Ground Station** y el STM32 por SX1262.

1. Abre `pico_firmware/Pico_LoRa_P2P/Pico_LoRa_P2P.ino` con Arduino IDE.
2. Selecciona el core Arduino-Pico y la placa correspondiente. Instala RadioLib y mantén los archivos `p2p_protocol.*` junto al sketch.
3. Compila, carga y conecta el USB de datos al PC. El puerto trabaja a 115200 baudios.
4. Abre la [estación web](https://eduardonovoac24.github.io/CubesatUSM/), espera la confirmación del Pico y prueba `PING`.

El [README de Pico](pico_firmware/README.md) detalla el cableado, los parámetros de radio compartidos y las tramas TEL/CMD/ACK/EVT. `pico_receiver/` contiene el receptor anterior en MicroPython.
