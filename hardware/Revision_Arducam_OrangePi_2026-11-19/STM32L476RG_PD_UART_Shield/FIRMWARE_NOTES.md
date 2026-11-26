# Firmware y Linux

STM32: USART1 AF7, PA10 RX y PA9 TX, 115200 baudios. Los cuatro ADC y perifericos de la shield inferior conservan sus asignaciones. Esta revision no añade trabajo de captura a STM32.

Orange Pi: habilitar SPI e I2C del GPIO26 en la distribucion instalada y comprobar que los nodos /dev correspondientes existen. No asumir nombres ni numeros de bus. Adaptar acceso a SPI/I2C mediante interfaces Linux compatibles con H2; los ejemplos que acceden a registros BCM de Raspberry Pi no son intercambiables. Configurar CS inactivo alto; verificar nivel al arrancar. Comenzar con SPI lento, p.ej. 1 MHz, y respetar el limite del modulo (documentacion Mini: hasta 8 MHz).

OV2640 SCCB: direccion de 7 bits normalmente 0x30; el fabricante tambien la expresa como 0x60 escritura / 0x61 lectura en formato de 8 bits. No confundir formatos. Comprobar el modulo real y su identificacion antes de capturar. No se presupone capacidad FIFO de la version Plus para una Mini sin referencia exacta.

Antes de usar I2C medir/verificar pull-ups a 3.3 V. No se han añadido valores de resistencias o capacitores basados en fotografias. PA0/PA1/PC2/PC3 no deben usarse hasta validar R1-R4 y sensores.
