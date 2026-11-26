# Firmware - lectura pasiva

+X: PA0 ADC1_IN5; -X: PA1 ADC1_IN6; +Y: PC2 ADC1_IN3; -Y: PC3 ADC1_IN4. Configurar entradas analógicas sin pull-up/pull-down. No habilitar hasta montar R1-R4, verificar sensores y rango de tensión. Elegir tiempo de adquisición según impedancia y capacitancia reales; no se fija un tiempo de muestreo sin esos datos. La respuesta puede ocupar solo una parte de la escala 0..VREF+ y requerir calibración por cara.

UART J7 conserva USART1, PA9 TX y PA10 RX (AF7). Baudrate y pinmux de Orange Pi deben coincidir con su configuración real. No cambiar USART2 ni I2C/SPI/LoRa/PWM de la placa inferior.
