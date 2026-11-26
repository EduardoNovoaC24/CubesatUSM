# Firmware ESP32 EPS

`esp32_firmware/CindyMoonEPS/CindyMoonEPS.ino` mide voltaje y corriente solar con un INA219 y envía tramas `EPSI` al STM32.

| Señal | Conexión |
|---|---|
| SDA INA219 | ESP32 GPIO21 |
| SCL INA219 | ESP32 GPIO19 |
| EPSI UART TX | ESP32 GPIO1 a STM32 PC5 (USART3 RX) |
| Tierra | GND común |

Abre el sketch en Arduino IDE con el core ESP32 y selecciona tu placa. Verifica que `EPS_SOLAR_SHUNT_UOHM` coincida con el shunt instalado. El ADC de batería está deshabilitado por defecto y precisa un GPIO ADC1 y el divisor real cuando se habilite.

El enlace UART usa 115200, 8N1. La rama contiene el cambio para realizar una conversión INA219 por muestra y leer ambos registros tras la indicación de conversión completa. Esta revisión requiere validación en el ESP32 físico.

## Diagnóstico I²C

`esp32_firmware/Ina219Probe/Ina219Probe.ino` es el sketch de banco que consulta la dirección `0x40` y muestra si responde en el monitor serie. En este sketch, SDA es GPIO21 y SCL es GPIO22. Para transmitir tramas `EPSI` al STM32, usa `CindyMoonEPS.ino` con el cableado de la tabla.
