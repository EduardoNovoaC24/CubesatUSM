# ADS-B: Orange Pi → STM32L476RG

## Inspección y cambios

Se reutiliza el proyecto existente. USART1 estaba inicializada a 9600, sin consumidores
ni transmisiones, reservada históricamente al HC-06. Ahora pertenece al Orange Pi:
**115200, 8 bits, sin paridad, 1 stop, sin control de flujo, TX/RX**.

El firmware usa FreeRTOS 10.3.1, heap_4 de 32000 bytes, CMSIS-RTOS **v2** y
llamadas nativas. Las tareas reales son `StartDefaultTask`/COMMAND,
`Task_SensorFusion`, `Task_RadioTest`, `Task_Logger` y `Task_Safety`.
No existen tareas llamadas Task_Sensors, Task_Telemetry o Task_HealthMonitor.
`freertos.c` no contiene la creación de tareas; esta ocurre en `main.c`.

| Recurso | Uso conservado / nuevo |
|---|---|
| USART1 PA9/PA10 | Orange Pi; IRQ prioridad 5; sin DMA |
| USART2 PA2/PA3 | ST-LINK VCP, diagnóstico, cuaterniones y comandos ADCS, 115200 |
| I2C1 PB8/PB9 | GY-91, calibraciones y fusión existentes |
| SPI1 PA5/PA6/PA7 | SX1262; NSS PA4, reset PC0, BUSY PB0, DIO1 PB1/EXTI1 |
| TIM1_CH1 PA8 | PWM/ADCS existente, sin cambios |
| TIM6 / SysTick | Tiempo HAL / RTOS existente, sin cambios |

No se detectaron conflictos entre PA9/PA10 y los otros periféricos configurados.
No se asignan canales DMA. USART1 prioridad 5 permite las API FromISR de FreeRTOS
(`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5`). No se modifican EXTI1,
reloj de 80 MHz, parámetros de radio ni la supervisión de seguridad del IMU.
La tarea de radio inicializa y alimenta un IWDG por registros; al omitir esa
tarea en el modo temporal tampoco se inicia ese watchdog por software.
`Task_Safety` conserva la supervisión del IMU y la lógica de desarmado.

Archivos de implementación:

- `Core/Inc/adsb_protocol.h`, `Core/Src/adsb_protocol.c`: formato, framing,
  validación, CRC y codificador compacto independiente de HAL/RTOS.
- `Core/Inc/adsb_receiver.h`, `Core/Src/adsb_receiver.c`: tarea, ISR HAL,
  recepción, snapshots, contadores y eventos.
- `Core/Src/main.c`: baud USART1 y creación de ADS-B después de las colas;
  diagnósticos enviados mediante `uart2_write`, que utiliza el logger existente.
- `Core/Src/stm32l4xx_hal_msp.c`: habilita/deshabilita NVIC USART1.
- `Core/Src/stm32l4xx_it.c`, `Core/Inc/stm32l4xx_it.h`: handler USART1.
- `OBC_MPU6050_FreeRTOS_BT.ioc`: baud e IRQ consistentes con HAL.
- `makefile.defs`: incluye los nuevos objetos en los makefiles existentes.
  CubeIDE descubre los `.c` al regenerar los makefiles.
- `tests/test_adsb*.c`, `tests/adsb_mock/`, `tests/run_adsb_checks.sh`:
  pruebas de protocolo y receptor simulado.
- `README.md` y esta guía: documentación actualizada.

## Cableado

Con ambos equipos apagados:

| Orange Pi UART1 | Nucleo-L476RG |
|---|---|
| TX | PA10 / Arduino D2 (RX) |
| RX | PA9 / Arduino D8 (TX) |
| GND | GND |

Usar niveles **TTL de 3,3 V** y tierra común. Desconectar el HC-06 de PA9/PA10.
Identificar los pines físicos UART1 según la placa Orange Pi concreta; no confundir
UART TTL con RS-232 ni conectar 5 V a las señales. ST-LINK USB queda conectado al PC.

## Arquitectura y concurrencia

USART1 recibe un byte por interrupción con `HAL_UART_Receive_IT`. El callback
solo copia a una cola estática de 512 bytes y rearma la recepción. La tarea
`Task_ADSB_Receiver` procesa hasta 256 bytes por iteración y cede CPU 1 ms;
prioridad nativa 24, equivalente a osPriorityNormal. SENSOR/LOGGER/SAFETY
mantienen sus prioridades superiores. El ACK tiene espera acotada a 20 ms.
La tarea no accede a I2C, SPI ni al controlador ADCS.

La tarea usa pila estática de 768 palabras (3072 bytes), TCB y dos colas
estáticas; no requiere nuevas asignaciones en el heap FreeRTOS. El tamaño
definitivo de `.bss` incluye también el estado newlib del TCB. La cola RX
cubre aproximadamente 44 ms de tráfico continuo a 115200/8N1: no garantiza
recepción si otra actividad impide atender USART1 durante demasiado tiempo.

Los errores UART se recuperan en contexto de tarea mediante abort/rearm.
Ante pérdida de bytes se vacía la cola y descarta hasta el siguiente LF;
una trama parcial sin nuevos bytes durante 1 s también se descarta.
Una línea supera el límite o contiene NUL/control inválido: descartar hasta LF.
No se concatenan los extremos de una trama dañada para crear datos válidos.
Se cuenta una sola vez cada trama descartada, aunque el LF llegue después.

Snapshots protegidos por secciones críticas breves, que también protegen los
contadores compartidos con ISR. No hay parsing, logs ni transmisión en ISR.
Los logs pasan por la cola existente; se limitan a aproximadamente un evento
por segundo (más arranque/timeout) y pueden perderse si esa cola está llena.
Compilar con `ADSB_DEBUG_ENABLED=0` deshabilita los logs ADS-B, no el ACK.

`ADSB_GetStatus()` permite múltiples lectores. `ADSB_UpdateQueue()` devuelve
una cola de **un solo consumidor** con el último evento completo; sobrescribe
eventos antiguos. Puede consultarse con `xQueueReceive` para esperar nuevos datos
o un timeout. No enviar, resetear ni borrar esa cola desde otras tareas.

El estado contiene la **última aeronave recibida**, no una tabla de aeronaves:
una trama válida sustituye completamente la anterior, sin mezclar campos entre
aeronaves. `valid_messages`/`messages_received` son contadores ADS-B globales;
`handshakes`, `invalid_messages`, `uart_errors`, `dropped_bytes` y `ack_errors`
se mantienen por separado. Los contadores uint32_t pueden dar la vuelta.

Tras 5 s sin un saludo o trama válida, `connected=false` y se registra timeout
una vez hasta reconexión. Los bytes basura no renuevan ese plazo.
La aeronave queda `data_valid=false` tras 5 s sin una trama ADS-B válida,
incluso si siguen llegando saludos. Un timeout conserva el último snapshot
para diagnóstico. Los flags de campos describen esa muestra; siempre comprobar
`data_valid` antes de usarla. Los cálculos de tiempo admiten wrap de HAL_GetTick.

## Protocolo exacto para el productor Python

ASCII, una trama por LF (`\n`); se acepta CRLF (`\r\n`). Máximo **127 bytes antes
de LF**, incluyendo CR si se envía. No enviar JSON directamente por UART.

Prueba inicial, sensible a mayúsculas:

```text
HOLA_L476\r\n
ACK_L476\r\n
```

La segunda línea es la respuesta de STM32. No se responde ACK a cada trama ADS-B.

```text
$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810\n
```

Exactamente nueve campos: `$ADSB`, versión `1`, ICAO, callsign, altitud ft,
velocidad kt, rumbo grados, latitud, longitud. Mantener todas las comas incluso
si faltan campos. ICAO obligatorio: seis hexadecimales; se normaliza a mayúsculas.
Callsign: 0–8 caracteres A–Z, 0–9 o espacio, sin comas. El productor debe
normalizarlo a mayúsculas y quitar espacios de relleno si lo desea.

Campos numéricos ausentes se representan mediante **cadena vacía**, nunca `None`,
`null`, `nan`, `inf`, `NA` o cero inventado. Altitud entera -2000..100000 ft,
velocidad entera 0..2000 kt, rumbo entero 0..359. El productor convierte valores
fraccionarios de readsb a esos enteros antes de transmitir; por ejemplo, redondear
rumbo y aplicar módulo 360. Latitud -90..90, longitud -180..180, decimal con punto
y hasta seis decimales; sin exponentes, signo `+` ni espacios en números.

Ejemplos con campos ausentes:

```text
$ADSB,1,E8032E,SKU176,12575,253,32,,
$ADSB,1,E8032E,,,,,,
```

`valid_fields` indica altitud (bit 0), velocidad (bit 1), rumbo (bit 2), posición
(bit 3). Si falta una coordenada, `position_valid=false` y ambas se almacenan
como cero; si una coordenada presente es inválida se rechaza la trama entera.
Una posición real 0,0 sí es válida. RSSI no está en v1: `rssi_db10=0` significa
**no medido**. No hay timestamp de origen en v1: el productor debe filtrar
aeronaves/muestras obsoletas de readsb; `last_update_ms` mide recepción local.

CRC opcional compatible con v1:

```text
$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810*HHHH\n
```

`HHHH`: cuatro dígitos hexadecimales CRC-16/CCITT-FALSE, polinomio 0x1021,
inicial 0xFFFF, sin reflexión ni XOR final. Se calcula sobre todos los bytes
desde **`$` incluido** hasta antes de `*`; no incluye CR/LF.
Tramas sin CRC siguen aceptándose; un CRC presente debe ser correcto.
El CRC detecta corrupción, no autentica al emisor.

Productor Python: `payload.encode('ascii')`; para CRC,
`f'{payload}*{binascii.crc_hqx(payload.encode("ascii"), 0xFFFF):04X}\n'`.
Una implementación posterior debe leer el JSON de readsb, elegir aeronaves,
comprobar antigüedad y convertir sus campos antes de construir esta trama.

## Salida completa USART2 para radar_stm32.py

Cada trama ADS-B válida recibida por USART1 actualiza el snapshot y genera
una línea independiente en USART2/ST-LINK, **115200/8N1**:

```text
$ADSB,1,E8032E,SKU176,12575,253,32,-33.472000,-70.810000\r\n
```

`\r\n` representa los dos bytes de terminación, no caracteres impresos.
La salida tiene siempre nueve campos y no incluye CRC, aunque la entrada lo
tuviera. ICAO y callsign identifican la aeronave de esa trama. Velocidad, rumbo,
altitud y posición provienen de la misma muestra validada. No se limita la
frecuencia de las líneas de datos al segundo de los diagnósticos `[ADSB]`.
El saludo HOLA/ACK permanece igual y no genera una trama de aeronave.

Si faltan altitud, velocidad o rumbo, su campo queda vacío. Si falta una o ambas
coordenadas, ambas salen vacías, por ejemplo:

```text
$ADSB,1,E8032E,SKU176,12575,253,32,,\r\n
```

La GUI puede actualizar los datos identificativos sin inventar una posición.
Una posición recibida y válida 0,0 se emite como `0.000000,0.000000`.
Los números de coordenadas se redondean a seis decimales mediante microgrados
y formato entero: no se necesita habilitar `_printf_float` en newlib-nano.

`ADSB_FormatUART()` en `adsb_protocol.c/.h` genera la línea en un buffer local.
`adsb_receiver.c/.h` la entrega al nuevo callback `data_sink`, independiente del
callback de depuración, también cuando `ADSB_DEBUG_ENABLED=0`. `main.c` conecta
ese callback con `uart2_enqueue()`: copia el mensaje entero a `logQueueHandle`.
`Task_Logger` es el único transmisor USART2 durante la ejecución normal del
scheduler. Los textos `[ADSB]`, cuaterniones y otros logs comparten esa cola y
se transmiten por mensajes completos, sin intercalar bytes entre tareas.
Las escrituras directas anteriores al scheduler y el hook fatal de overflow
de pila son excepciones existentes; no participan en el flujo normal de datos.

El envío a la cola es sin espera, desde la tarea ADS-B; nunca desde la IRQ.
`pc_frames_queued` cuenta tramas aceptadas por la cola y `pc_frames_dropped`
cuenta fallos de formato/cola llena. El buffer se copia antes de retornar y no
se comparte un buffer global mutable entre productores. La cola sigue teniendo
16 entradas de 192 bytes; cada trama ADS-B ocupa una entrada. `Task_Logger`
mantiene su transmisión HAL con timeout acotado de 100 ms. El contador de
encoladas no acredita recepción por la GUI ni detecta fallos eléctricos USART2.

Ambas UART operan a 115200: con tráfico de entrada cercano al máximo, la
expansión a seis decimales y los logs/telemetría adicionales pueden superar la
capacidad de salida. No hay almacenamiento ilimitado ni garantía sin pérdidas
bajo saturación. Ajustar la tasa del productor Orange Pi al caudal disponible
y revisar `pc_frames_dropped` y `dropped_bytes` en el depurador. La adquisición
y las tareas de seguridad continúan aunque se llene la cola de salida.

Para cargar esta actualización: `Project > Build Project` y después `Run > Run`
o `Run > Debug` con ST-LINK. Cerrar el monitor serie antes de abrir
`radar_stm32.py`, para que solo una aplicación lea el VCP del PC. No se cambió
la GUI: su archivo no está en este proyecto; el contrato suministrado por el
usuario es la referencia de compatibilidad.

Prueba manual, desde Orange Pi con USART1 ya configurada como se indica abajo:

```bash
printf '$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810\n' > /dev/ttyS1
printf '$ADSB,1,ABCDEF,OTHER,1000,120,270,1.234567,12.345678\n' > /dev/ttyS1
printf '$ADSB,1,E8032E,SKU176,12575,253,32,,\n' > /dev/ttyS1
```

Usar estas tramas únicamente como pruebas sintéticas de protocolo: no son una
fuente de aeronaves reales. Comprobar en el PC las dos líneas con seis decimales
y la tercera con coordenadas vacías. Después volver al productor de readsb para
mostrar los datos reales del Orange Pi.

Las pruebas `test_adsb.c` y `test_adsb_receiver.c` cubren la salida exacta, CRLF,
precisión, límites y signos de coordenadas, campos ausentes, múltiples ICAO,
una secuencia de 1000 tramas y rechazo del callback por cola llena. Se ejecutan
con diagnóstico habilitado y deshabilitado mediante `run_adsb_checks.sh`.
La cola de transmisión real, sus latencias y el mapa de la GUI requieren prueba
en placa; la secuencia host valida lógica y orden, no el caudal físico UART.

## Acceso desde telemetría y selección para LoRa

No se añaden transmisiones ni se cambian parámetros SX1262. Ejemplo para un
consumidor futuro, en contexto de tarea:

```c
ADSB_Status_t status;
uint8_t payload[23];
ADSB_GetStatus(&status);
size_t length = ADSB_EncodeCompact(&status.aircraft, "E8032E", payload, sizeof(payload));
/* length == 0: inválida/obsoleta o ICAO distinta. NULL selecciona la última.
 * Integrar después con la política explícita de transmisión de la aplicación. */
```

Codificación de 23 bytes, sin padding: byte 0 versión=1; byte 1 máscara de
campos; 2..4 ICAO en orden hexadecimal; 5..8 altitud int32; 9..10 velocidad
uint16; 11..12 rumbo uint16; 13..16 latitud int32 en microgrados;
17..20 longitud int32 en microgrados; 21..22 reservados=0. Todos los campos
multibyte numéricos son little-endian, enteros negativos en complemento a dos.
Coordenadas se truncan hacia cero al convertir; error inferior a un microgrado.
Callsign/RSSI no se incluyen. La selección filtra el snapshot más reciente;
no guarda por separado un historial de la aeronave seleccionada.

## Compilar, cargar y validar

1. STM32CubeIDE: abrir este proyecto, seleccionar Debug, `Project > Build Project`.
   Verificar que `adsb_protocol.c` y `adsb_receiver.c` no estén excluidos del build.
2. `.ioc` ya actualizado. No es necesario regenerar para compilar. Si se regenera,
   revisar los cambios antes de aceptar: el proyecto contiene personalizaciones
   previas fuera de bloques USER CODE. La nueva tarea se crea en USER CODE.
3. Conectar ST-LINK USB y usar `Run > Debug` o `Run > Run` para cargar el ELF.
4. Monitor serie PC: puerto ST-LINK VCP, 115200/8N1, sin control de flujo.
   Deben aparecer `[ADSB] USART1 inicializado` y `[ADSB] Esperando mensajes...`.

**Modo temporal sin LoRa:** `FORCE_UART_ONLY=1` en `Core/Src/main.c` omite el
diagnóstico SX1262 y la creación de `Task_RadioTest`. ADS-B, sensores, consola y
`Task_Safety` arrancan aunque el módulo esté ausente o su cableado falle.
No hay transmisiones ni reintentos de radio. El log de arranque muestra
`[UART] ADS-B/IMU activos; SX1262 omitido por configuracion`.

Para restaurar LoRa después de revisar las conexiones, cambiar
`FORCE_UART_ONLY` a `0U`, recompilar y cargar. En ese modo se conservan
`SX1262_DIAGNOSTIC_MODE=1` y `RADIO_QUATERNION_MODE=1`: un fallo del diagnóstico
detiene el arranque antes del scheduler, como antes. `I2C_SCAN_ONLY` es otro
modo aislado que impide llegar al scheduler si se activa.

En Orange Pi, con UART1 habilitada y libre de consola/getty y otros productores:

```bash
ls -l /dev/ttyS1
stty -F /dev/ttyS1 115200 raw -echo -ixon -ixoff -crtscts cs8 -parenb -cstopb clocal
```

Si el usuario no tiene acceso al dispositivo, ejecutar estos comandos en una
sesión con permisos para el puerto. La habilitación UART/pinmux depende de la
imagen Armbian y debe verificarse localmente; no se cambia la configuración Linux
desde este firmware.

Terminal A Orange Pi (iniciar antes de transmitir):

```bash
timeout 20s cat /dev/ttyS1
```

Terminal B Orange Pi:

```bash
printf 'HOLA_L476\r\n' > /dev/ttyS1
printf '$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810\n' > /dev/ttyS1
```

Terminal A debe mostrar `ACK_L476`. USART2 debe mostrar el saludo recibido y,
si la trama se envía al menos 1 s después del saludo, el diagnóstico ICAO/ALT.
El límite de logs no impide procesar las tramas enviadas inmediatamente.

Prueba CRC con Python estándar, después de configurar el puerto con stty:

```bash
python3 - <<'PY'
import binascii
import os
payload = '$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810'
frame = f'{payload}*{binascii.crc_hqx(payload.encode("ascii"), 0xFFFF):04X}\n'
fd = os.open('/dev/ttyS1', os.O_WRONLY | os.O_NOCTTY)
os.write(fd, frame.encode('ascii'))
os.close(fd)
PY
```

Errores y recuperación:

```bash
printf 'INVALIDO\n' > /dev/ttyS1
printf '$ADSB,1,E8032E,A,1,1,1,91,0\n' > /dev/ttyS1
printf '$ADSB,1,E8032E,SKU' > /dev/ttyS1
sleep 2
printf '176,12575,253,32,-33.472,-70.810\n' > /dev/ttyS1
python3 - <<'PY'
with open('/dev/ttyS1', 'wb', buffering=0) as port:
    port.write(b'X' * 200 + b'\nHOLA_L476\n')
PY
```

Tras cada error, reenviar una trama completa y comprobar recuperación.
Dejar de transmitir o desconectar TX durante más de 5 s: debe aparecer timeout
una vez. Reconectar y enviar saludo: debe volver el ACK. Durante las pruebas,
comprobar cuaterniones, comandos `ADCS STATUS`, actividad de sensores y radio
existente. No usar ARM para probar ADS-B. Inspeccionar `ADSB_GetStatus()` y el
watermark de pila ADSB con el depurador; validar la capacidad con la tasa real
del productor y verificar que `dropped_bytes`/`uart_errors` no aumenten.

Verificaciones locales reproducibles:

```bash
bash tests/run_adsb_checks.sh
bash tests/run_p2p_checks.sh
export PATH=/opt/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_1.0.0.202410170706/tools/bin:$PATH
make -C Debug -j4 all
```

Las pruebas host simulan HAL y las colas; cubren el protocolo, ACK, timeout
(incluido wrap del reloj), desbordamiento, error UART y trama parcial. No
demuestran latencias reales de IRQ ni la ejecución simultánea de sensores/radio.
La validación eléctrica, ST-LINK, Orange Pi y rendimiento en placa requiere hardware.

Resultado local de esta integración: build ARM GNU 13.3.1 y ambas suites host
completadas sin errores. Tamaños ELF: text 117764, data 752, bss 53000 bytes.
La compilación conserva la advertencia previa `build_lorawan_abp_uplink` sin uso
en el modo P2P actual. No se cargó el firmware ni se comprobó en placa.
El informe `.su` indica 456 bytes para el frame local de Task_ADSB_Receiver y
304 para ADSB_Parse; estas cifras no incluyen todas las llamadas anidadas ni
la biblioteca C, por lo que no sustituyen medir el watermark real de la pila.

Actualización temporal sin LoRa: recompilación y suite ADS-B correctas con
`FORCE_UART_ONLY=1`. ELF: text 97184, data 752, bss 45560 bytes. La tabla de
símbolos conserva las tareas ADS-B, sensores, logger y seguridad; no incluye
Task_RadioTest, radio_diagnostic_run ni p2p_watchdog_init. Sigue pendiente la
carga y comprobación física de este modo.

Actualización de salida para radar Python: build ARM y ambas suites host correctas.
ELF actual: text 98040, data 752, bss 45584 bytes, con modo sin LoRa activo.
Solo permanece la advertencia previa de función LoRaWAN sin uso. El frame local
de Task_ADSB_Receiver ocupa ahora 592 bytes según `.su`; la pila configurada
sigue siendo 3072 bytes y su watermark debe medirse con tráfico real en placa.
