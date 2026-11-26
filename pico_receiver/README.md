# Prueba P2P STM32 → Raspberry Pi Pico

Se necesita un segundo Waveshare Pico-LoRa-SX1262 de banda compatible con
868 MHz, conectado al Pico/Pico W con MicroPython. Este receptor es una
adaptación propia, no el demo oficial Waveshare ni una pila LoRaWAN.

## Pines

| Señal SX1262 | Pico GPIO | Contacto físico Pico | STM32 transmisora |
|---|---|---|---|
| NSS | GP3 | 5 | PA4 |
| BUSY | GP2 | 4 | PB0 |
| RESET | GP15 | 20 | PC0 |
| DIO1 | GP20 | 26 | PB1 |
| SCK | GP10 | 14 | PA5 |
| MOSI | GP11 | 15 | PA7 |
| MISO | GP12 | 16 | PA6 |
| 3V3 | 3V3 OUT | 36 | Alimentación de su propio módulo |
| GND | GND | 38 | Masa local de su propio módulo |

Cada microcontrolador usa su propio módulo de radio. No se conectan los GPIO
del Pico a los de la STM32. Si el módulo se monta directamente en el conector
Pico, estas conexiones ya corresponden a su distribución estándar.
Mantener la misma orientación del conector. Usar antenas adecuadas en ambos.

## Parámetros compartidos

868100000 Hz; SF7; BW125 kHz; CR4/5; preámbulo 8; cabecera explícita;
CRC activado; IQ normal; sync word SX126x 0x3444; TCXO DIO3 1.7 V / 5 ms;
switch RF mediante DIO2. La STM32 solicita 14 dBm.

## Uso

1. En Thonny, seleccionar el intérprete MicroPython (Raspberry Pi Pico) y su USB.
2. Abrir `pico_receiver/main.py`, guardarlo en el Pico como `main.py` y ejecutarlo.
   No requiere instalar bibliotecas externas. Si el Pico no tiene MicroPython,
   instalar antes el UF2 correspondiente a su modelo desde micropython.org.
3. Esperar `[PICO] RX listo...`.
4. Cargar `Debug/OBC_MPU6050_FreeRTOS_BT.elf` en la STM32 y reiniciarla.
5. La prueba `P2P_PERIODIC_V7` ejecuta el diagnóstico y un primer envío. Si pasa,
   repite `TEST_LORA` cada 10 segundos (más el tiempo de envío). Ante un fallo
   detiene la repetición hasta reset. No inicia IMU, FreeRTOS ni comandos UART.
6. Ver en el Pico `[RX] TEST_LORA | len=9 RSSI=... SNR=...`. Esto es evidencia
   de recepción. `TX_DONE` en la STM32 por sí solo no prueba el enlace.

Los mensajes del Pico salen por USB; la STM32 mantiene USART2 a 115200 8N1.
El Pico solo recibe, no devuelve ACK; esta prueba no valida el sentido inverso.
El código Pico fue revisado y comprobado sintácticamente en host, sin acceso al
hardware receptor. Si falla, enviar toda su salida desde el arranque.

## Fuentes

- Comandos y trama SPI: https://files.waveshare.com/upload/e/e1/DS_SX1261-2_V1.2.pdf
- API SPI/Pin MicroPython: https://docs.micropython.org/en/latest/rp2/quickref.html
- Pines/TCXO del ejemplo enlazado para el módulo:
  https://github.com/siuwahzhong/lorawan-library-for-pico/blob/ws_dev/src/boards/rp2040/sx126x-board.c
  y `src/include/pico/board-config.h` del mismo repositorio.

## Verificación local

```sh
python3 -c 'import ast,pathlib; ast.parse(pathlib.Path("pico_receiver/main.py").read_text()); print("Pico syntax OK")'
make -C Debug -j4 all
```
