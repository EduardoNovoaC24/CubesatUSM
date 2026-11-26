# Conexiones - J7 GPIO26 directo

J7: zocalo hembra 2x13, paso 2.54 mm, 26 contactos fisicos en correspondencia con la Orange Pi Zero LTS del STEP suministrado.

| J7 / pin fisico Orange Pi | Funcion | STM32 |
|---|---|---|
| 6 | GND comun | GND |
| 8 | UART1_TX / PG6 Orange Pi | PA10 / D2 / USART1_RX / CN10.33 |
| 10 | UART1_RX / PG7 Orange Pi | PA9 / D8 / USART1_TX / CN10.21 |
| Otros 23 pines | Sin conexion en la shield | NC |

Los pines de 3.3 V y 5 V tampoco se conectan. J7 no alimenta la Orange Pi. UART logico de 3.3 V; no RS-232. Verificar la configuracion del UART en el sistema operativo.

Vista superior de la shield: pin 1 cuadrado en (29.845,52.444563) mm; pin 2 a 2.54 mm hacia arriba; pin 3 a 2.54 mm hacia la derecha. GPIO26_PINOUT.csv enumera todas las coordenadas y funciones.

| Cara | JST | Pin 1 A provisional / ADC | Pin 2 K provisional | Carga a GND |
|---|---|---|---|---|
| +X | J3 | PA0 / CN7.28 / ADC1_IN5 | GND | R1 |
| -X | J4 | PA1 / CN7.30 / ADC1_IN6 | GND | R2 |
| +Y | J5 | PC2 / CN7.35 / ADC1_IN3 | GND | R3 |
| -Y | J6 | PC3 / CN7.37 / ADC1_IN4 | GND | R4 |

Los valores de R1-R4 y la polaridad de los sensores continuan pendientes. No hay sockets AFE ni footprints de fotodiodos.
