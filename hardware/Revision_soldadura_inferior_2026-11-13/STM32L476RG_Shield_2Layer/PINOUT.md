# Redes de la revisión 2.0-P

J1 representa CN7 y J2 CN10 de la Nucleo. Numeración física del conector, no del encapsulado del MCU. Las redes son directas: no hay sufijos de puente ni cables JP.

| Red | STM32 y Morpho | Destino |
|---|---|---|
| I2C1_SCL | PB8 / CN10.3 | J3.4 |
| I2C1_SDA | PB9 / CN10.5 | J3.5 |
| LORA_SCK | PA5 / CN10.11 | J4.14 |
| LORA_MISO | PA6 / CN10.13 | J4.16 |
| LORA_MOSI | PA7 / CN10.15 | J4.15 |
| LORA_NSS | PA4 / CN7.32 | J4.5 |
| LORA_RESET | PC0 / CN7.38 | J4.20 |
| LORA_BUSY | PB0 / CN7.34 | J4.4 |
| LORA_DIO1 | PB1 / CN10.24 | J4.26 |
| MOTOR_PWM | PA8 / CN10.23 | J5.1, señal al ESC |
| 3V3 | CN7.16 | J3.2 y J4.36 |
| GND | CN7.20 y CN10.20 | J3.3, J4.18, J4.38 y J5.2 |

SB51 cerrado y SB52 abierto confirmados por el usuario, habilitando PC0 en CN7.38 según UM1724. PA5 también comparte LD2/SB21; conservar esa condición en las comprobaciones de SPI.

PINOUT.csv contiene todos los contactos, incluidos los que permanecen NC. No conectar VIN del GY-91, VSYS/VBUS del Waveshare, su LoRa_ANT/GPIO22 ni una salida BEC del ESC a estas redes.
