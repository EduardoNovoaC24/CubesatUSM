## Firmware actual: BIDIRECTIONAL_P2P_V9

La versión actual reemplaza el envío Q unidireccional por TEL y recepción CMD,
con ACK/EVT reales emitidos por la STM32. Mantiene radio, pines y TCXO verificados.
Instrucciones, límites de tasa/tiempo en aire, calibración, Flash, watchdog,
matriz de pruebas y pasos de aceptación física: `pico_firmware/README.md`.
Sketch Arduino para trasladar: `pico_firmware/Pico_LoRa_P2P.zip`.
Build STM32 y simulaciones aprobados; enlace bidireccional físico pendiente.

## Firmware actual: QUATERNION_P2P_V8

`SX1262_DIAGNOSTIC_MODE=1` y `RADIO_QUATERNION_MODE=1` verifican
registros/TCXO/XOSC antes del scheduler y después inician FreeRTOS:
fusión MPU9250 a 50 Hz, logger, seguridad, comandos y tarea de radio.
La radio toma el último snapshot bajo mutex y envía cada 10 s el texto
`Q,%+06d,%+06d,%+06d,%+06d`, orden w,x,y,z escalado por 10000.
Ejemplo: `Q,+10000,+00000,+00000,+00000` representa identidad.
Sin datos: `QERR,NO_DATA`; fallo IMU: `QERR,MPU9250`; snapshot de más
250 ms: `QERR,STALE`. No se transmite TEST_LORA en este modo.

UART conserva diagnóstico y payload enviado cada 10 s, pero desactiva los
flujos T=... y MAGD. El motor permanece desarmado y ARM rechazado por el
modo diagnóstico. La tarea de radio reutiliza la inicialización validada;
no resetea el TCXO al arrancar. Parámetros RF y pines se conservan.

Cargar el ELF principal y verificar QUATERNION_P2P_V8, IMU OK, payload Q,...,
TX_DONE y recepción del mismo texto en Pico. Un receptor que imprime bytes
arbitrarios no requiere cambios. Un filtro exclusivo TEST_LORA sí debe
adaptarse. La prueba inversa RX en STM32 no está implementada.

Verificado: `make -C Debug -j4 all` y pruebas HAL host en modos 0/1.
Pendiente: ejecución de este payload real en las placas.

## Prueba actual: P2P_PERIODIC_V7

La STM32 conserva NSS=PA4, BUSY=PB0, RESET=PC0, DIO1=PB1 y SPI1=PA5/PA6/PA7.
Tras pasar registros, TCXO/XOSC y el primer TX, repite TEST_LORA cada 10 s
más el tiempo de envío. Ante error detiene la prueba hasta reset. No inicia
FreeRTOS, IMU ni comandos UART. Reemplaza la espera después de un único TX
descrita abajo. Receptor MicroPython y conexiones: `pico_receiver/README.md`.

Verificado: build STM32, pruebas HAL host para modos 0/1 y sintaxis Python.
Receptor Pico pendiente de ejecución física. Verificación de recepción:
`[RX] TEST_LORA | len=9 RSSI=... SNR=...`.

## Salida de diagnóstico aislada

Con `SX1262_DIAGNOSTIC_MODE=1` y `FORCE_UART_ONLY=0`, el arranque
termina en `[SXDIAG] Fin de diagnostico; reset para repetir` y espera
sin iniciar FreeRTOS, telemetría IMU ni comandos UART. Para repetir la
prueba se debe reiniciar la placa. Esta descripción reemplaza la continuación
con IMU/UART indicada en secciones anteriores.

## Actualización: prueba TCXO_1V7_V5

El ejemplo enlazado para Pico-LoRa-SX1262 configura DIO3 a **1.7 V**,
no 3.3 V. `SX126xIoTcxoInit()` llama a `SX126xSetDio3AsTcxoCtrl(TCXO_CTRL_1_7V, ...)`
y después calibra con máscara 0x7F. `BOARD_TCXO_WAKEUP_TIME` vale 5 ms
(320 ticks de 15.625 us). Esto confirma el ajuste del ejemplo, no una medición
ni la referencia comercial del TCXO de todas las revisiones.

Fuentes consultadas:
- https://spotpear.com/index/study/detail/id/794.html (enlace GitHub del ejemplo)
- https://github.com/siuwahzhong/lorawan-library-for-pico/blob/ws_dev/src/boards/rp2040/sx126x-board.c
  (blob 0cfbf4e30e0f7553445883bc2306be5c7fee4ffd)
- https://github.com/siuwahzhong/lorawan-library-for-pico/blob/ws_dev/src/include/pico/board-config.h
  (blob 7d8471b651b7c94a29049ed68f76e38eab332d59)

El diagnóstico actual verifica registros, borra errores, configura TCXO,
calibra, solicita STBY_XOSC, registra modo/errores y recupera mediante RESET.
RF permanece bloqueada. El cmd_status inesperado sigue registrándose y puede
impedir el PASS global incluso si la prueba de reloj pasa. Tras el diagnóstico
sí arranca FreeRTOS con IMU/UART. Las descripciones antiguas de aislamiento
sin scheduler y ajuste TCXO 3.3 V de abajo quedan reemplazadas por esta sección.

# Diagnóstico SX1262 / STM32L476RG

## Hallazgos confirmados en el código

- `MX_SPI1_Init()` usaba `SPI_DATASIZE_4BIT`. El driver trabaja con bytes:
  se transmitían palabras de cuatro bits, incompatibles con los comandos SX1262.
  Esto invalida las trazas SPI anteriores; no demuestra por sí solo por qué BUSY
  permanece alto inmediatamente después de RESET, antes de transmitir.
- `radio_test_init()` ignoraba el retorno de `sx126x_reset()`.
- `radio_spi_diag()` agotaba la espera BUSY y aun así bajaba NSS y transmitía.
  Ignoraba retornos HAL y del driver, e imprimía estructuras inicializadas a cero
  como si fueran respuestas. Se eliminó ese acceso SPI directo.
- `Task_RadioTest()` continuaba a XOSC/TX después de REGTEST fallido y continuaba
  a TX después de XOSC fallido. Ahora detiene la tarea de radio ante esos fallos.
- La HAL ejecutaba la fase de datos aun si fallaba la fase de comando; además
  podía ocultar el error HAL detrás de un timeout BUSY posterior. Ahora aborta,
  libera NSS, informa el error del contexto correcto y descarta datos fallidos.
- La preparación TX y varias consultas IRQ ignoraban errores del driver. Ahora
  cada operación debe terminar correctamente antes de continuar.
- NSS se inicializaba LOW. Ahora arranca HIGH, también en `.ioc`.

## Configuración revisada

No existen `gpio.c` ni `spi.c`: las funciones generadas están en `Core/Src/main.c`.
`Core/Inc/main.h` define PA4=NSS, PC0=RESET, PB0=BUSY, PB1=DIO1.
PB0 es entrada digital sin pull; PB1 es entrada EXTI ascendente sin pull.
PA4 y PC0 son salidas push-pull sin pull. `HAL_SPI_MspInit()` configura PA5/6/7
como AF5 para SCK/MISO/MOSI. No se encontraron otros propietarios de SPI1 ni
reconfiguraciones de esos pines. No fue necesario agregar un mutex: en operación
normal la radio tiene una sola tarea propietaria; en diagnóstico no hay tareas.

SPI1 queda master, 8 bits, MSB primero, CPOL LOW, CPHA 1EDGE, NSS software,
sin NSS pulse. PCLK2=80 MHz / 256 = **312.5 kHz**, frecuencia conservadora inicial.
El divisor anterior /64 daba 1.25 MHz; esto no era por sí mismo una infracción.

RESET ya era activo LOW, con pulso de 20 ms, sin esperar BUSY antes del pulso:
no se encontró un error de polaridad, pin o duración en esa implementación.
Se conserva el pulso y se fuerza NSS HIGH antes de resetear.

## Archivos cambiados

- `Core/Src/main.c`: SPI, nivel inicial NSS, arranque diagnóstico antes del
  scheduler y barreras RESET/BUSY/STATUS → REGTEST → XOSC → TX.
- `Core/Inc/main.h`: opción `SX1262_DIAGNOSTIC_MODE`, actualmente activada (1) para investigar el fallo.
- `Core/Src/radio_test.c`: propagación de fallos, estado GPIO real, validación
  de GetStatus, prueba restaurable de registro y comprobaciones de preparación TX.
- `Core/Inc/radio_test.h`: inicialización devuelve `bool`; entrada diagnóstico.
- `Drivers/SX126x/sx126x_hal_stm32.c`: fases SPI full duplex con RX descartado
  para comandos/escrituras, buffers locales separados, abortos y observación RESET.
- `OBC_MPU6050_FreeRTOS_BT.ioc`: persiste 8 bits, divisor y NSS para regeneración.
- `tests/sx1262_mock/main.h`, `tests/test_sx1262_hal.c`: pruebas host de fallos HAL.

No se cambiaron FreeRTOS, el driver ADCS, el control del motor ni los sensores.

## Secuencia aislada y activación

En STM32CubeIDE, C/C++ Build → Settings → MCU GCC Compiler → Preprocessor,
agregar **`SX1262_DIAGNOSTIC_MODE=1`** para la configuración que se vaya a compilar.
Hacer Clean Project y Build Project. El valor por defecto en `Core/Inc/main.h` ya está en 1. Si hay una definición
explícita `SX1262_DIAGNOSTIC_MODE=0` en el compilador, prevalece sobre ese valor.
Para volver al modo normal definir el símbolo en 0 (o cambiar el valor del
header a 0 y quitar cualquier definición externa) y reconstruir.

Tras inicializar periféricos y ejecutar `ADCS_ControlInit()` (desarmado), el modo
se ejecuta antes de crear las tareas. El planificador no arranca: no hay sensor
fusion, comandos UART de armado, REGTEST largo, XOSC ni RF. Este aislamiento
es exclusivo del modo diagnóstico; el arranque de los otros módulos en modo
normal se conserva. El firmware queda esperando un reset después del resultado.

1. Comprueba modos GPIO y tamaño SPI; imprime configuración y niveles.
2. Mantiene RESET LOW 20 ms y luego HIGH. Muestrea BUSY durante 1000 ms con logs
   periódicos y ante cambios observados. UART bloqueante limita la resolución:
   no sustituye una captura de analizador lógico.
3. Exige haber observado BUSY LOW y que siga LOW al terminar. Si falla, no hay SPI.
4. GetStatus: exige retorno correcto, STBY_RC y estado de comando no reservado
   ni de error (2 o 6). Un rechazo conservador exige revisar el byte en el bus;
   no significa automáticamente daño físico. Después solicita standby RC.
5. Lee `0x0740`, cambia un bit del MSB de sincronización, lee y compara,
   restaura el original y verifica la restauración. No toca OCP, PA ni el reloj.
   Si la restauración falla, exige reset físico y mantiene RF bloqueada.

`PASS` se genera únicamente en ejecución tras estas comprobaciones; no prueba RF.
La compilación y las pruebas host no constituyen un PASS de hardware.

## Fuentes y TCXO

Se contrastaron RESET/SPI, registros y comandos con el
[datasheet Semtech SX1261/2 Rev. 1.2](https://files.waveshare.com/upload/e/e1/DS_SX1261-2_V1.2.pdf),
secciones 8, 12 y 13. GetStatus es `C0 00` (estado en el segundo byte);
ReadRegister usa opcode, dirección de 16 bits, NOP y luego dato.
La implementación Semtech existente respeta esos formatos y no se reescribió.

El [esquema oficial Waveshare](https://files.waveshare.com/upload/d/d8/Pico-LoRa-SX1262_Sch.pdf)
muestra Q1 TCXO de 32 MHz con alimentación controlada por DIO3. No identifica
la referencia comercial ni la tensión nominal de Q1. Por tanto **no se pudo
confirmar la tensión requerida solamente con ese esquema**. El ajuste previo
3.3 V se conserva para XOSC normal, pendiente de confirmar con la revisión de
placa y la especificación del TCXO montado; no se ejecuta en diagnóstico.
Configurar TCXO no resuelve por sí solo BUSY bloqueado antes del primer comando.

## Verificación efectivamente ejecutada

Toolchain local STM32CubeIDE: GNU Arm 13.3.rel1. Builds normal y diagnóstico
completados con código de salida 0. Logs: `build-sx1262-normal.log` y
`build-sx1262-diagnostic.log`. Advertencia existente: función
`build_lorawan_abp_uplink` definida pero no usada; no se modificó por estar fuera
 del alcance. En la verificación inicial se compiló el modo normal al final. Posteriormente
se activó diagnóstico por defecto (1) y se reconstruyó el ELF principal para
que la configuración Debug habitual cargue el modo aislado. El arranque imprime
`[BUILD] SX1262 diag=1` y fecha/hora de compilación. También se guardó el ELF diagnóstico separado como
`Debug/OBC_MPU6050_FreeRTOS_BT.diagnostic.elf`.

Comando de build, después de seleccionar el modo:

```sh
export PATH=/opt/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_1.0.0.202410170706/tools/bin:$PATH
make -C Debug -j4 all
```

Pruebas host ejecutadas para ambos valores de la macro:

```sh
for mode in 0 1; do
  cc -std=c11 -Wall -Wextra -Werror -DSX1262_DIAGNOSTIC_MODE=$mode \
    -Itests/sx1262_mock -IDrivers/SX126x \
    tests/test_sx1262_hal.c Drivers/SX126x/sx126x_hal_stm32.c \
    -o tests/test_sx1262_hal
  ./tests/test_sx1262_hal
done
```

Resultado en ambos: `HAL fault injection checks passed (simulation only)`.
Comprueban BUSY alto sin clocks/NSS LOW, reset recuperable aun con BUSY alto,
timeout de reset, fases de lectura separadas, liberación NSS ante fallo HAL,
supresión de la fase de datos cuando falla el comando y descarte de RX ante
BUSY alto posterior. No hubo acceso al hardware, flasheo ni transmisiones RF.

## Ejemplo UART, NO resultado real

Ejemplo abreviado del caso BUSY bloqueado; los números de registro dependen
 del resto de periféricos y no se inventan aquí:

```text
[SXDIAG] POWER: externally verified 3.3V (user report, not measured by firmware)
[SXDIAG] TEST 1 GPIO; TEST 2 RESET (1000ms observation); TEST 3 BUSY
[SXDIAG] GPIO: ... GPIOB->IDR=... MODER=... PUPDR=...; GPIOC->IDR=... ODR=...
[SXDIAG] RESET: t=0ms PB0/BUSY=1 PB1/DIO1=1 PC0/RESET=1
[SXDIAG] RESET: t=100ms PB0/BUSY=1 PB1/DIO1=1 PC0/RESET=1
...
[SXDIAG] BUSY: FAIL after RESET; SPI skipped
[SXDIAG] RESULT: FAIL; SUSPECT: RESET/BUSY or SPI wiring/status; no XOSC/TX attempted
```

## Incertidumbres y mediciones si persiste

Las trazas originales se tomaron con SPI mal configurado y durante BUSY alto:
no permiten concluir desfase, eco MOSI/MISO, cortocircuito ni daño del SX1262.
Una avería es solamente una hipótesis después de descartar software y conexiones.

- Sin alimentación, comprobar continuidad y ausencia de cortos de cada cable:
  PA4→pin5, PA5→pin14, PA6→pin16, PA7→pin15, PC0→pin20, PB0→pin4,
  PB1→pin26; GND común pin38. Los números son posiciones físicas de Pico,
  no números de GPIO STM32. Revisar orientación y puentes de la placa real.
- Medir simultáneamente RESET en PC0 y en el módulo: LOW unos 20 ms, luego
  HIGH cerca de 3.3 V. Confirmar que llega al NRESET real del radio.
- Capturar BUSY en PB0 y en el módulo durante el segundo posterior al reset.
  Comparar PB1/DIO1 para detectar cables cruzados. No forzar BUSY como salida.
- Medir 3.3 V en pin36 y en alimentación/desacoplos del radio durante el reset,
  no solamente sin carga; comprobar masas y caídas con osciloscopio.
- Solo con BUSY LOW: capturar NSS/SCK/MOSI/MISO; verificar SCK inactivo LOW,
  8 pulsos por byte a 312.5 kHz, una sola ventana NSS para `C0 00` y estado
  recibido en el segundo byte. Acortar cables si los flancos muestran ruido.
- Una vez recuperado GetStatus, confirmar referencia/tensión del TCXO antes
  de probar XOSC. No adjudicar el fallo actual al TCXO sin esa evidencia.
