# Firmware

J7 GPIO26 conserva USART1 STM32: Orange Pi pin 8 TX/PG6 -> PA10 RX (CN10.33); pin 10 RX/PG7 <- PA9 TX (CN10.21); pin 6 GND. AF7 en STM32, configuracion y baudrate compatibles en ambos equipos. Otros contactos NC. No hay alimentacion por J7.

Fotodiodos: PA0 ADC1_IN5; PA1 ADC1_IN6; PC2 ADC1_IN3; PC3 ADC1_IN4. Mantener deshabilitados hasta determinar cargas R1-R4, polaridad, rango y tiempo de adquisicion. No modificar los perifericos de la placa inferior.
