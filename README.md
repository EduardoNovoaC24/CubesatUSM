# CubeSat USM

Proyecto CubeSat del equipo USM. Incluye el OBC STM32 con FreeRTOS, la radio LoRa P2P, el módulo EPS con ESP32, la estación terrestre y los diseños KiCad.

## Ramas

| Rama | Contenido y guía |
|---|---|
| [firmware-stm32](../../tree/firmware-stm32) | OBC, ADS-B, IMU, control de actitud y radio SX1262 |
| [firmware-pico](../../tree/firmware-pico) | Puente Raspberry Pi Pico + SX1262 |
| [firmware-esp32](../../tree/firmware-esp32) | Telemetría EPS con INA219 |
| [estacion-lora](../../tree/estacion-lora) | CubeSat - Ground Station y estación local Python |
| [hardware](../../tree/hardware) | PCB, esquema, montaje, BOM y revisiones |
| [integracion-adsb](../../tree/integracion-adsb) | Orange Pi, USART1 y radar local |

Las ramas parten de la misma versión integrada. El README de cada rama indica dónde están sus archivos y cómo utilizarlos.

## Usar la estación LoRa

1. Abre [CubeSat - Ground Station](https://eduardonovoac24.github.io/CubesatUSM/) en Chrome de escritorio.
2. Conecta el Pico LoRa con un cable USB de datos y cierra cualquier monitor serie que use ese puerto.
3. Pulsa **Conectar Pico LoRa**, selecciona su puerto y espera a que estén disponibles los comandos.
4. Envía `PING` en **Calibración y control** y comprueba que llegue un `ACK` del STM32.

La conexión P2P utiliza el permiso USB de Chrome. Las credenciales LoRaWAN se configuran únicamente en el firmware local. Consulta la [guía del STM32](guias/firmware-stm32.md).

La [guía de la estación terrestre](guias/estacion-lora.md) documenta el mismo panel con conexión USB en la web o con el servidor Python local.

## Reproducir el proyecto

Compila desde los archivos fuente con STM32CubeIDE o Arduino IDE según la rama. Los binarios históricos citados en [las notas anteriores](HISTORIAL_FIRMWARE.md) corresponden a compilaciones locales. Las dependencias incluidas mantienen sus avisos de licencia. Consulta [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
