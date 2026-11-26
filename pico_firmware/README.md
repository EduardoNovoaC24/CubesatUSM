# STM32L476 ↔ Pico: P2P_CMD_V1

## EPS Cindy Moon por ESP32 y UART

El perfil STM32 `EPS_UART_TELEMETRY=1` se entrega en
`Debug/telemetria_integrada/telemetria_eps_uart.bin`. Incluye la misma
telemetría ADCS y usa **USART3 en PC5 / CN10-6** para recibir a
115200 8N1. Conecta el TX de la ESP32 (**GPIO1/TX0**) a PC5 y comparte GND (**CN10-20**).
Si las placas tienen fuentes separadas, deja separadas sus salidas 3V3.
Este perfil y el perfil ADC directo `SOLAR_TELEMETRY=1` son excluyentes.

El sketch `esp32_firmware/CindyMoonEPS/CindyMoonEPS.ino` lee solo el
INA219 de paneles por SDA GPIO21 y SCL GPIO19 a 100 kHz. Detecta un
único dispositivo entre `0x40` y `0x4F` y muestra la dirección por TX0.
Comprueba el valor del shunt: el código presume R100 (0,1 Ω). La ESP32
no acciona el cargador ni las salidas del EPS en este perfil.
Envía por UART `EPSI,seq,validez,solar_mV,solar_uA,bateria_mV`, donde
el bit 0 valida el INA219 y el bit 1 valida la batería.
El STM32 invalida el dato cuando pasan más de 3,5 s sin una trama
válida y reenvía `EPSI` en sus turnos EPS. La Pico actual lo acepta
como sufijo de la trama de cuaternión y no requiere actualización.

La tensión de batería queda sin datos hasta confirmar una salida de
medición o un pin ADC1 libre y el divisor real con el equipo EPS.
La opción de ADC en el sketch ESP32 está deshabilitada de fábrica
(`EPS_BATTERY_ADC_PIN=-1`). Una vez confirmado el circuito, define el
pin y `EPS_BATTERY_R_TOP_OHM` / `EPS_BATTERY_R_BOTTOM_OHM`.
No conectes una batería directamente a un ADC. La trama entrega
tensión de batería si se habilita esa medición; no mide su corriente ni
su estado de carga.

## Variante STM32 para medir paneles solares

La configuración habitual conserva `SOLAR_TELEMETRY=0`. La variante
`Debug/telemetria_integrada/telemetria_solar.bin` se compila con
`SOLAR_TELEMETRY=1` y añade cuatro mediciones de voltaje.
La Pico existente recibe `Q|EPS` por LoRa en SF7 sin cambiar de
firmware. Se intenta enviar una muestra cada 10 s, sujeto al presupuesto
de tiempo en aire y a los ACK. La web muestra las tres ramas y la
batería en **Paneles solares**. La medición de voltaje no indica por sí
sola que la batería esté cargando.

| Medición | Entrada |
|---|---|
| Pareja 1 | PC4 / CN10-34 |
| Pareja 2 | PC5 / CN10-6 |
| Pareja 3 | PA0 / CN7-28 |
| Batería | PA1 / CN7-30 |
| Retorno de las ramas | GND / CN10-20 |
| Retorno de batería | GND / CN7-20 |

Los dos GND de la Nucleo tienen la misma referencia. Úsalos únicamente
si el equipo EPS confirma que ambos retornos pueden unirse. En esta
variante, PA0 y PA1 se reasignan y las caras ópticas 1 y 2 quedan sin
lectura. PC2 y PC3 mantienen las caras 3 y 4. En el firmware normal
siguen las cuatro entradas ópticas.

Usa un divisor por medición. En cada uno, conecta **R superior** entre
el positivo del punto de medida y su ADC, y **R inferior** entre el ADC
y GND. No conectes fuentes directas al ADC ni dejes las entradas
energizadas con la Nucleo apagada. Si los tres puntos de medida ya están
unidos en el mismo bus, tendrán prácticamente el mismo voltaje. Para
distinguir cada rama usa puntos de prueba separados por el EPS antes de
combinar las ramas, si existen.

El BIN de referencia usa **330 kΩ arriba y 68 kΩ abajo** y
referencia ADC nominal de **3,3 V**. A 7,2 V el ADC recibe unos 1,23 V
y el divisor consume unos 18 µA. A 16 V el ADC recibe unos 2,73 V y
el divisor consume unos 40 µA. Un capacitor de **100 nF** en cada
entrada ADC respecto a GND ayuda a estabilizar la lectura. Confirma
los voltajes máximos y el nivel en cada divisor con el multímetro
antes de conectar la Nucleo. Si algún punto supera los **16 V**, cambia
el divisor y los símbolos `SOLAR_R_TOP_OHM` y `SOLAR_R_BOTTOM_OHM`
antes de medir. Para calibrar la estimación, compara con un multímetro
en los mismos bornes y ajusta `SOLAR_ADC_REF_MV` a la referencia medida.

## Banco de rueda de reacción

El firmware de banco usa **PA8/D7 (CN9 pin 8)** para la señal PWM de 50 Hz del
ESC y **PC7/D9 (CN5 pin 2)** para una señal de tacómetro de 3,3 V, con
un pulso por vuelta. Une las tierras de señal. Usa una entrada compatible
con 3,3 V o un adaptador para el tacómetro. Mantén aislada la salida BEC
si la Nucleo ya se alimenta por USB o EPS.

Verifica el neutro y la pérdida de señal en el manual del modelo exacto,
con la rueda protegida y con un corte de alimentación accesible. El
perfil `BIDIR` presume 1500 µs en neutro y el perfil `AIR` presume
1000 µs como mando mínimo y solo avance. La placa arranca sin emitir
pulsos y sin perfil seleccionado.

1. Por USB serie de la STM32 a 115200, escribe `WHEEL PROFILE BIDIR`
   si el manual del ESC confirma neutro de 1500 µs.
2. Con la rueda detenida, envía `WHEEL ENABLE`. Tienes 30 s para pulsar
   **Armar** en la pestaña **Rueda de reacción**. El firmware mantiene
   el neutro 3 s y luego espera hasta 20 s una consigna.
3. Aplica un nivel firmado de 1 a 15. Cada unidad equivale a 5 µs desde
   el neutro. La salida cambia hasta 5 µs cada 50 ms. La prueba vence
   a los 8 s desde la primera consigna y cambiar de nivel no renueva ese plazo.
4. Usa **Detener** o `WHEEL STOP` por USB. El firmware vuelve al neutro,
   lo mantiene 5 s y retira el PWM. Verifica que la rueda esté físicamente
   detenida antes de habilitar otra prueba o invertir el sentido.

### Prueba ADCS

En modo banco, calibra el giróscopo con el CubeSat quieto mediante
**Calibrar giroscopio** y espera `BIAS_APPLIED`. Para el magnetómetro,
inicia la calibración con la rueda desarmada, gira el CubeSat en los
tres ejes para cubrir todos los octantes y finaliza. Activa **Fusión
9DOF** cuando la pestaña indique que el magnetómetro está calibrado
y tiene datos. Se usa Fusion AHRS de Seb Madgwick.

Con la rueda desarmada, usa **Aplicar eje** en la web o
`WHEEL AXIS Z+` por USB para configurar el eje y la polaridad del ESC.
Confirma el signo con una prueba lenta. El eje y el perfil se restablecen
al reiniciar. Selecciona el perfil bidireccional, habilita la prueba y arma.

- **Medir tiempo de frenado** ajusta la rueda en ambos sentidos para
  corregir el giro residual. Confirma el resultado tras 2 s dentro de
  +/-0,5 grados/s y registra el tiempo hasta la entrada en esa banda.
- **Ejecutar giro** acepta un objetivo relativo entre -180 y +180 grados.
  Usa el giro integrado del cuerpo, reduce la velocidad al acercarse al
  objetivo y confirma el resultado tras 2 s con hasta 1 grado de error y
  0,5 grados/s de giro. En crucero ajusta el nivel con el tacómetro, si
  hay pulsos recientes.
- **Seguir luz en Z** asigna a la cara 1 el azimut inicial elegido.
  Las caras 1 a 4 deben apuntar a +X, +Y, -X y -Y. Usa la opción de
  invertir la escala si el ADC disminuye al aumentar la iluminación.
  La STM32 alinea la cara 1 con la luz y envia su azimut en la referencia
  elegida. El azimut es relativo y su precisión depende del montaje y
  la calibración del giróscopo y de los fotodetectores.

Cada maniobra tiene un límite de 60 s y se interrumpe si faltan datos de
IMU o retorno `LINK` por 25 s. El seguimiento requiere luz direccional.
Tras confirmar el frenado o el giro, se mantiene la consigna durante
5 s para observar el resultado antes de volver al neutro. Comprueba el
giro residual al descargar la rueda en el banco. El CSV incluye el modo,
el tiempo y los ángulos. Actualiza el firmware del STM32 y el del Pico
para recibir esos campos.

Las cuatro entradas de luz del CN7 se muestrean aunque la rueda esté
desarmada. **CN7-28 / PA0** es la cara 1, **CN7-30 / PA1** la cara 2,
**CN7-35 / PC2** la cara 3 y **CN7-37 / PC3** la cara 4. Conecta las señales
analógicas acondicionadas de 0 a 3,3 V y comparte GND. Una fotoresistencia
desnuda requiere un divisor; un fotodiodo, un circuito de lectura adecuado.
La web muestra cuentas ADC de 0 a 4095, distingue lecturas ADC inválidas y
permite invertir la escala visual si el voltaje disminuye con la luz. Una
entrada analógica flotante puede entregar números. La escala es relativa
y todavía no calcula lux ni azimut.

La telemetría periódica SF7 usa 28 bytes binarios (`0xA2`): cuaternión,
PWM efectivo ordenado, nivel, perfil, estado y RPM del tacómetro, más
eje, signo, tasa del cuerpo, tiempo, resultado ADCS y las cuatro
lecturas de luz de 12 bits. La máscara de validez ocupa los cuatro bits
bajos del byte de estado de la trama de 28 bytes.
Sin pulsos válidos la web muestra RPM sin datos. La Pico convierte el
formato a texto solo en USB. Los ACK de los comandos conservan su formato.
La trama ADCS ocupa 66,8 ms al aire con SF7/BW125 y CR 4/5.

## Archivos y carga

- STM32: `Debug/OBC_MPU6050_FreeRTOS_BT.elf`, compilado en este workspace.
- Pico: abrir `Pico_LoRa_P2P/Pico_LoRa_P2P.ino` en Arduino IDE. Copiar también
  `p2p_protocol.h` y `p2p_protocol.c` en la misma carpeta del sketch.
- Usar Arduino-Pico (Earle Philhower), la placa real del usuario (Pico 2 W en
  la captura) y RadioLib. Conservar el puerto USB correcto y 115200 baudios.
- La GUI no se ha modificado. Cerrar el monitor serie Arduino antes de conectar
  la GUI al mismo puerto. No usar el receptor MicroPython anterior para comandos.

## Conexiones y radio

Cada placa tiene su propio SX1262; no unir GPIO entre microcontroladores.

| Señal | STM32L476 | Pico GPIO / contacto físico |
|---|---|---|
| NSS | PA4 | GP3 / 5 |
| BUSY | PB0 | GP2 / 4 |
| RESET | PC0 | GP15 / 20 |
| DIO1 | PB1 | GP20 / 26 |
| SCK | PA5 | GP10 / 14 |
| MOSI | PA7 | GP11 / 15 |
| MISO | PA6 | GP12 / 16 |
| Alimentación radio | 3.3 V local | 3V3 OUT / 36 |
| GND | GND local común | GND / 38 |

868.1 MHz, SF7/BW125/CR4/5, preámbulo 8, cabecera explícita, sync público
0x3444, CRC ON, IQ normal, DIO2 RF switch, DIO3 TCXO 1.7 V (margen de arranque STM32: 20 ms).
Se conservan 14 dBm en STM32 y 10 dBm en Pico. Para pruebas muy cercanas puede
reducirse `RADIO_TX_POWER_DBM` en STM32 y el argumento de potencia de `radio.begin`
en Pico, reconstruyendo ambos. La cercanía no requiere alargar RX.

## Secuencia de prueba física pendiente

1. Cargar STM32. Debe indicar `BIDIRECTIONAL_P2P_V9`, pasar registros/XOSC e
   iniciar `TEL/CMD/ACK/EVT`. El motor sigue desarmado; ARM está bloqueado.
2. Cargar el sketch Pico completo. Al escribir `HELLO` debe responder
   `PICO_READY,P2P_CMD_V1` solo si la radio está inicializada.
3. Verificar `[RX] Payload: "TEL,..."`, RSSI, SNR y frecuencia en USB.
4. Enviar `CMD,41,PING` desde USB. Esperar `PICO_TX,41,QUEUED`,
   `PICO_TX,41,SENT` y posteriormente un `[RX] Payload: "ACK,41,OK,..."`.
   SENT no confirma recepción STM32. Sin ACK en 12 s el estado es desconocido.
5. Probar `CMD,42,GET_STATUS`, `CMD,43,SET_BETA,0.075`,
   `CMD,44,SET_MODE,6DOF` y `CMD,45,SET_RATE,1` con IDs distintos.
6. Repetir un comando con el mismo ID y contenido: se reenvía la última respuesta
   cacheada sin ejecutar otra vez, dentro de los últimos 32 IDs y hasta reset.
   Reutilizar un ID con otro contenido devuelve ID_CONFLICT. El Pico no reintenta
   automáticamente comandos; el timeout de la GUI no demuestra fallo ni éxito.

Los logs de arriba son criterios/ejemplos, NO resultados físicos de este firmware.
Solo la versión anterior tuvo recepción física confirmada por el usuario.

## Telemetría y límites

`TEL,seq,t_ms,ax,ay,az,gx,gy,gz,mx,my,mz,qw,qx,qy,qz,mag_ok`

- ax/ay/az: mg; gx/gy/gz: centi-grados/s; magnetómetro: décimas de µT;
  quaternion: escala 10000. seq es el contador circular de muestras publicadas
  a 50 Hz, no el contador de paquetes transmitidos: saltos de seq son normales.
- mag_ok exige muestra válida y calibración magnética aplicada. Antes de calibrar
  permanece 0 y el filtro usa 6DOF. Los crudos magnéticos no están calibrados;
  el filtro aplica offsets y escalas en unidades físicas por separado.
- Si falla la IMU se envía QERR,MPU9250; snapshot >250 ms, QERR,STALE.
- Se conserva el filtro Fusion. SET_BETA controla su ganancia AHRS, **no** el
  parámetro matemáticamente equivalente de un Madgwick clásico.
- SET_RATE acepta 1–10 Hz, inicial 2 Hz **solicitados**. La frecuencia efectiva
  es menor por ventanas RX (mínimo 150 ms entre TX) y presupuesto de tiempo en aire:
  1% de reposición, capacidad de ráfaga 1 s, 200 ms reservados para respuestas.
  TEL/ACK/EVT comparten presupuesto. Un ACK puede superar los 12 s si se agota;
  la GUI debe mostrar desconocido. No se promete 2 Hz continuos ni certificación
  normativa: banda, antena, potencia y reglas locales requieren revisión física.
- IRQ se consulta por SPI en STM32; solo RADIOTEST accede a SX1262 después del
  arranque. El sensor aplica comandos y calibraciones mediante colas; no dentro
  de ISR. El Pico desasocia IRQ RX durante TX y rearma recepción después.
- Tres recuperaciones fallidas de radio detienen la tarea. IWDG ~32 s nominales
  reinicia si la radio deja de progresar o la IMU lleva >5 s sin datos. En debug
  debe contemplarse el watchdog al detener la CPU.

## Calibración y persistencia

| Comando | Procedimiento / resultado |
|---|---|
| CAL_GYRO | Placa quieta, 200 muestras (~4 s), gyro <3 °/s, norma accel cercana a 1 g y varianza baja. STARTED seguido de EVT DONE/ERR. |
| CAL_ACCEL | Placa nivelada con Z hacia arriba o abajo, 200 muestras; corrige bias, no escala ni calibración de seis caras. |
| MAG_START | Girar en 3D durante al menos 200 muestras; trabajo identificado por su ID de MAG_START. |
| MAG_STOP | Valida 8 octantes, rango por eje y residual de elipsoide; offset + soft-iron diagonal solamente. ACK del STOP y EVT del ID original START. No aplica si la cobertura es insuficiente. |
| SET_MODE,9DOF | Exige calibración magnética y datos recientes; de lo contrario MAG_UNAVAILABLE. |
| RESET_FILTER | Reinicia AHRS y bias adaptativo; conserva offsets calibrados guardados en configuración. |
| SAVE_CONFIG | Escribe solo a petición, omite configuración idéntica y limita cambios a uno cada 60 s por arranque; error FLASH_ERROR si no puede guardar. |
| LOAD_CONFIG | Carga y valida CRC/versión/rangos antes de aplicar. No hay carga automática al arrancar. |

Calibraciones se ejecutan en la tarea SENSOR con las lecturas existentes. Movimiento,
fallo de IMU o captura >120 s producen EVT ERR sin aplicar resultados falsos.
MAG_STOP termina el trabajo iniciado por MAG_START; no confundir ACK STARTED con DONE.

Flash reservada: últimos 4 KiB, 0x080FF000–0x080FFFFF, dos páginas alternas de
2 KiB con versión/CRC y commit escrito al final. Ambas configuraciones de linker
reservan la región. Persistencia solo habilitada en L476 de 1 MiB, dual-bank,
sin bank swap; otras geometrías devuelven FLASH_ERROR. Un borrado masivo al
flashear puede eliminar configuraciones. La atomicidad lógica fue simulada;
el corte físico de alimentación todavía no se ha probado.

## Archivos modificados y validación

| Archivo | Cambio |
|---|---|
| Core/Src/main.c | TEL, dueño único radio TX/RX, colas, deduplicación, aplicación de control, watchdog |
| Core/Src/radio_test.c; Core/Inc/radio_test.h | RX continuo, CRC/cabecera, salud/rearme, diagnóstico conservado |
| Core/Inc/main.h | Modo bidireccional con diagnóstico, opción Q heredada |
| Core/Inc/p2p_protocol.h; Core/Src/p2p_protocol.c | Parser compartido, cache de IDs, CRC32, tiempo en aire/presupuesto |
| Core/Inc/p2p_control.h; Core/Src/p2p_control.c | Configuración y calibraciones no bloqueantes |
| Core/Src/p2p_flash.c; STM32L476RGTX_*.ld | Diario Flash y reserva de región |
| makefile.defs | Integra objetos nuevos sin editar los makefiles generados |
| Pico_LoRa_P2P/* | Sketch Arduino-Pico/RadioLib y parser compartido |
| tests/*p2p*, tests/test_radio_state.c, tests/test_pico_bridge.cpp | Pruebas host y dobles de hardware/API |

```sh
export PATH=/opt/st/stm32cubeide_1.19.0/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_1.0.0.202410170706/tools/bin:$PATH
make -C Debug -j4 all
bash tests/run_p2p_checks.sh
python3 tests/test_gui_compat.py /ruta/obc_telemetry_gui_lora_p2p_3tabs.py
```

| Prueba | Resultado local |
|---|---|
| STM32 ARM build/link | Correcto; advertencia heredada build_lorawan_abp_uplink sin uso |
| Parser: límites, NaN/Inf, whitelist, IDs duplicados/conflicto | Simulación host aprobada |
| Calibración quieta/movimiento, cobertura magnética, configuración | Simulación host aprobada |
| Flash: CRC, desgaste, escritura interrumpida y copia previa | Simulación host aprobada |
| STM32 RX/TX: CRC/cabecera, timeout, errores, rearme | Modelo de comandos SX1262 aprobado |
| Pico: HELLO, cola USB, TX/RX IRQ, CRC, ACK ausente, sin reintentos | Mock C++ de API aprobado |
| GUI original TEL/Q/ACK/EVT y metadatos | Parser real extraído por AST, solo lectura |
| Compilación Pico con Arduino-Pico/RadioLib reales | Pendiente: este equipo solo tiene core AVR instalado |
| Enlace bidireccional real, sensores/calibración/Flash en placa | Pendiente; no hay acceso a las placas |
