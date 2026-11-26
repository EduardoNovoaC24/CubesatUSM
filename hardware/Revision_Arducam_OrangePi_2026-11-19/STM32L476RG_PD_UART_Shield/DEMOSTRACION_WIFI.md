# Demostracion prevista por Wi-Fi

Arducam Mini OV2640 -> SPI/I2C -> Orange Pi Zero LTS -> Wi-Fi local -> navegador del equipo de demostracion. La camara no incorpora Wi-Fi. UART con STM32 sigue transportando telemetria/control.

La Orange Pi Zero LTS tiene Wi-Fi XR819 IEEE 802.11b/g/n segun su ficha de fabricante. Conectar la Orange Pi a una red local disponible; la viabilidad y rendimiento real dependen del sistema operativo, controlador y entorno. No se han ensayado alcance, latencia o cuadros por segundo.

Trabajo de software pendiente: habilitar buses y permisos; implementar/adaptar controlador de la Arducam SPI para Orange Pi; inicializar OV2640, leer JPEG del FIFO y servir capturas o MJPEG por HTTP en la red local. Una Arducam SPI no aparece automaticamente como una webcam V4L2: no basta con abrir /dev/video0. Los ejemplos Raspberry Pi basados en BCM requieren adaptacion al Allwinner H2. Esta entrega no incluye un servidor operativo ni una prueba de imagen.

Comprobar primero la alimentacion 3.3 V y los pull-ups del modulo; probar una captura JPEG antes de añadir servidor web. No se garantiza video fluido a 2 MP.

Referencias: https://pdf.direnc.net/upload/orange-pi-zero-lts-512m-datasheet.pdf ; https://docs.arducam.com/Arduino-SPI-camera/Legacy-SPI-camera/2MP-Plus/ ; https://cdn.arducam.com/downloads/shields/ArduCAM_Mini_2MP_Camera_Shield_DS.pdf ; https://github.com/ArduCAM/RaspberryPi/blob/master/SPI_Camera/README.md
