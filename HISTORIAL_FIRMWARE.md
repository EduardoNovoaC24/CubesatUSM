> Actualización: firmware `BIDIRECTIONAL_P2P_V9`, TEL/CMD/ACK/EVT.
> Ver [guía de carga y pruebas](pico_firmware/README.md) y
> [sketch Pico para trasladar](pico_firmware/Pico_LoRa_P2P.zip).
> Las descripciones históricas siguientes pueden referirse a modos anteriores.

> USART1 ahora comunica con Orange Pi para ADS-B a **115200/8N1**.
> Ver [conexión, protocolo, compilación y pruebas ADS-B](ADSB_ORANGEPI.md).
> Modo temporal: `FORCE_UART_ONLY=1` omite el SX1262 para probar Orange Pi e IMU
> sin LoRa. Cambiarlo a `0U`, recompilar y cargar para restaurar la radio.
> Las tramas ADS-B válidas también salen completas por USART2 como
> `$ADSB,1,ICAO,FLIGHT,ALT,SPEED,HEADING,LAT,LON` con CRLF y seis decimales
> para el radar Python. Los campos ausentes se mantienen vacíos.

# OBC_MPU6050_FreeRTOS_BT

Firmware para un nodo de telemetría embebido (OBC simplificado) del CubeSat USM, basado en
STM32L476RGT6 (Nucleo-L476RG) y FreeRTOS (CMSIS-RTOSv2). Adquiere orientación desde el
MPU6500 de un módulo GY-91 por I2C, la funde con el algoritmo AHRS
(librería [Fusion](https://github.com/xioTechnologies/Fusion) de xioTechnologies) y transmite
el cuaternión resultante por LoRaWAN (modo ABP) hacia The Things Network (TTN), usando un
transceptor SX1262.

## Arquitectura de firmware

El firmware corre seis tareas de aplicación. Lectura, fusión y control permanecen en la misma
tarea para que el actuador use siempre la muestra de actitud más reciente.

| Tarea | Prioridad | Periodo | Responsabilidad |
|---|---|---|---|
| `COMMAND` (`StartDefaultTask`) | Normal | Por caracteres UART | Recibe y valida comandos ADCS desde USART2 |
| `Task_SensorFusion` | AboveNormal | 20 ms (~50 Hz) | Lee el MPU6500, corrige bias, calcula el cuaternión, procesa comandos y actualiza el ADCS |
| `Task_RadioTest` | Normal | 10000 ms (una vez al inicio: diagnóstico SPI/XOSC) | Lee el snapshot, arma el uplink LoRaWAN (ABP) y transmite por el SX1262 |
| `Task_Logger` | AboveNormal | Por eventos | Vacía la cola de logs hacia USART2 sin bloquear el lazo de sensores |
| `Task_Safety` | High | 50 ms | Vigila el heartbeat del IMU y solicita `DISARM` si pasan más de 250 ms sin datos válidos |
| `Task_ADSB_Receiver` | Normal | Recepción por IRQ / espera acotada | Recibe USART1, valida ADS-B, responde al saludo y publica el último estado |

Sincronización:
- `telemetryMutexHandle` protege `telemetry_snapshot` (escrito por `Task_SensorFusion`, leído por `Task_RadioTest`).
- `logQueueHandle` desacopla los mensajes de diagnóstico de la transmisión UART bloqueante.
- `adcsCommandQueueHandle` garantiza que solo `Task_SensorFusion` modifique el controlador ADCS.

Comandos disponibles a 115200 baud por USART2:

```text
ADCS HELP
ADCS ARM
ADCS DISARM
ADCS MANUAL -1.0..1.0
ADCS AUTO
ADCS AXIS X|Y|Z
ADCS GAINS kp kd
ADCS TARGET w x y z
ADCS STATUS
```

`ADCS AUTO` solo se acepta después de `ADCS ARM` y de configurar explícitamente un
cuaternión mediante `ADCS TARGET`; no se activa automáticamente al arrancar.

El pin DIO1 del SX1262 está configurado como interrupción externa (EXTI1) a nivel de GPIO,
pero **no hay un `HAL_GPIO_EXTI_Callback` implementado**: el fin de transmisión se detecta
actualmente por *polling* del registro de IRQ vía SPI (`radio_send_payload()` en `radio_test.c`),
no por la interrupción DIO1.

## Sensores

| Sensor | Bus / dirección | Notas |
|---|---|---|
| MPU6500 (GY-91) | I2C1, `0x68` | Acelerómetro + giroscopio, `WHO_AM_I=0x70`; corrección dinámica de bias con `FusionBias`. |
| BMP280 (GY-91) | I2C1, `0x76` | Detectado (`CHIP_ID=0x58`), todavía no integrado en la telemetría. |
| AK8963 (GY-91) | I2C interno, `0x0C` | No funciona en el módulo utilizado; el sistema opera en 6-DOF y yaw relativo. |

El GY-91 usa I2C1 (`PB8`=SCL, `PB9`=SDA).

## Radio / LoRaWAN

- Transceptor: SX1262 (Semtech), driver vendorizado en `Drivers/SX126x/` + HAL SPI propio.
- Protocolo: LoRaWAN 1.0, activación **ABP** (DevAddr + NwkSKey/AppSKey embebidos en firmware),
  FPort 44, cifrado AES-128 y MIC (CMAC) implementados desde cero en `lorawan_crypto.c`.
- Parámetros de radio (`radio_test.h`): AU915 FSB2 canal 8 (916.8 MHz), SF7, BW 125 kHz, CR 4/5,
  preámbulo de 8 símbolos, potencia TX 14 dBm, *sync word* público `0x34`.
- Payload de aplicación: texto ASCII con el cuaternión escalado ×10000
  (`"Q,+w,+x,+y,+z"`, opcionalmente extendido con el vector de heading del magnetómetro).
- Servidor de red: [The Things Network](https://www.thethingsnetwork.org/) (TTN).

## Mapa de pines

| Periférico | Pines | Función |
|---|---|---|
| I2C1 | PB8 / PB9 | SCL / SDA — GY-91 |
| SPI1 | PA5 / PA6 / PA7 | SCK / MISO / MOSI — SX1262 |
| SX1262 NSS | PA4 | Chip select (software) |
| SX1262 NRESET | PC0 | Reset |
| SX1262 BUSY | PB0 | Entrada, sin pull |
| SX1262 DIO1 | PB1 | EXTI1 (configurada, sin callback registrado) |
| USART1 | PA9 / PA10 | TX / RX, 115200 baud — Orange Pi ADS-B y prueba HOLA_L476/ACK_L476 |
| USART2 | PA2 / PA3 | TX / RX, 115200 baud — log de depuración por USB (ST-LINK VCP) |
| TIM1 CH1 | PA8 | PWM 50 Hz — señal del servo/ESC; alimentación externa y tierra común |

## Estructura del proyecto

```
Core/
  Inc/        Headers de la aplicación (main.h, mpu9250.h, adcs_control.h, Fusion*.h, lorawan_crypto.h, radio_test.h)
  Src/        main.c (tareas), mpu9250.c, adcs_control.c, Fusion*.c, lorawan_crypto.c, radio_test.c
  Startup/    Arranque del Cortex-M4 (startup_stm32l476xx.s)
Drivers/
  CMSIS/                  Soporte de core ARM y device STM32L4xx
  STM32L4xx_HAL_Driver/   HAL de ST
  SX126x/                 Driver vendorizado del transceptor SX1262
  Radiolib/               Referencia adicional de radio
Middlewares/Third_Party/FreeRTOS/   FreeRTOS + CMSIS-RTOSv2
OBC_MPU6050_FreeRTOS_BT.ioc          Configuración de STM32CubeMX
STM32L476RGTX_FLASH.ld / _RAM.ld     Scripts de enlazado (flash / RAM)
```

## Compilar y flashear

Proyecto de STM32CubeIDE (contiene `.project` / `.cproject`).

1. Abrir STM32CubeIDE → `File > Open Projects from File System...` → seleccionar esta carpeta.
2. Compilar con `Project > Build Project` (configuración `Debug`, genera el ejecutable en `Debug/`).
3. Flashear con un ST-LINK sobre la Nucleo-L476RG (`Run > Debug` o `Run > Run`).
4. Abrir un puerto serie sobre el VCP del ST-LINK a **115200 baud, 8N1** para ver el log de
   `Task_SensorFusion` / `Task_RadioTest` por UART2.

El archivo `.ioc` puede reabrirse en STM32CubeMX si se necesita regenerar la configuración de
periféricos (los bloques `USER CODE` de `main.c` se preservan).

## Estado conocido / limitaciones

- El fin de transmisión LoRaWAN se detecta por *polling* SPI, no por la interrupción DIO1
  (configurada pero sin callback).
- La fusión AHRS opera actualmente sin magnetómetro (`FusionAhrsUpdateNoMagnetometer`), por lo
  que yaw es relativo y acumula deriva.
- El actuador arranca desarmado. El Futaba S3003 solo prueba PWM e interfaz; una rueda brushless
  real necesita ESC bidireccional y realimentación de RPM/corriente.
- El módulo `FusionCompass` de la librería Fusion está presente pero no se usa.
- UART1 recibe ADS-B del Orange Pi; comandos ADCS y logs permanecen en UART2.
- El payload LoRaWAN es texto ASCII; migrar a un formato binario reduciría el tiempo en aire.
