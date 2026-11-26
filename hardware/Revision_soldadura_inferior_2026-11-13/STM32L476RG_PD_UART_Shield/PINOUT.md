# Conexiones - revisión 0.2-P

| Cara | Nombre funcional | JST | Pin 1 (A provisional) / ADC | Pin 2 (K provisional) | Carga a GND |
|---|---|---|---|---|---|
| +X | J_PD_X_POS | J3 | PA0, CN7.28, ADC1_IN5 | GND | R1 |
| -X | J_PD_X_NEG | J4 | PA1, CN7.30, ADC1_IN6 | GND | R2 |
| +Y | J_PD_Y_POS | J5 | PC2, CN7.35, ADC1_IN3 | GND | R3 |
| -Y | J_PD_Y_NEG | J6 | PC3, CN7.37, ADC1_IN4 | GND | R4 |

Cada R tiene pin 1 conectado a su ADC y pin 2 a GND, en paralelo con el fotodiodo remoto. No hay sockets AFE J8-J11. R1-R4 usan huella axial THT DIN0207 de paso 7.62 mm; valor pendiente, sin resistencia predeterminada. Sin montarlas no se considera listo el canal.

| UART J7 | Señal STM32 | Orange Pi según conexión indicada por el usuario |
|---|---|---|
| 1 | PA10 / D2 / USART1_RX / CN10.33 | Pin físico 8, TX |
| 2 | PA9 / D8 / USART1_TX / CN10.21 | Pin físico 10, RX |
| 3 | GND | Pin físico 6, GND |

UART lógico 3.3 V, no RS-232; J7 no suministra alimentación a Orange Pi. Verificar revisión física y configuración de UART de Orange Pi. Funciones STM32 verificadas con UM1724 y [datasheet ST](https://www.st.com/resource/en/datasheet/stm32l476rg.pdf). Los cuatro pines ADC estaban libres en la placa inferior revisada; la disponibilidad del firmware se verifica por separado.
