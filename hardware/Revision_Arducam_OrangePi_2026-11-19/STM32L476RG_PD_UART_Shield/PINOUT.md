# Conexiones actuales

UART conservado: J7.8 TX/PG6 Orange Pi -> STM32 PA10 RX / CN10.33; J7.10 RX/PG7 Orange Pi <- STM32 PA9 TX / CN10.21; J7.6 GND comun. Logica 3.3 V; 115200 baudios.

| J8 | Camara | Pin fisico J7 / Orange Pi |
|---|---|---|
| 1 | CS | 24 |
| 2 | MOSI | 19 |
| 3 | MISO | 21 |
| 4 | SCLK | 23 |
| 5 | GND | 6 |
| 6 | VCC 3.3V | 1 |
| 7 | SDA | 3 |
| 8 | SCL | 5 |

J7 tiene 10 contactos activos y 16 NC. Numeracion fisica de conector, no numeracion de biblioteca GPIO de Linux. Los buses de camara son los de Orange Pi; no comparten los buses I2C/SPI de STM32.

| Cara | Conector | ADC / anodo provisional | Catodo provisional | Resistencia a GND |
|---|---|---|---|---|
| +X | J3 | PA0 / CN7.28 / ADC1_IN5 | GND | R1 TBD |
| -X | J4 | PA1 / CN7.30 / ADC1_IN6 | GND | R2 TBD |
| +Y | J5 | PC2 / CN7.35 / ADC1_IN3 | GND | R3 TBD |
| -Y | J6 | PC3 / CN7.37 / ADC1_IN4 | GND | R4 TBD |

J3-J6 son JST XA polarizados con retencion para sensores externos. No hay footprints de fotodiodos ni AFE.
