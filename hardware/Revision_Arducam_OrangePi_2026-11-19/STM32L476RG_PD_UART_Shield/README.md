# Shield superior - Arducam Mini controlada por Orange Pi

Revision 19 noviembre 2026. Se agrega J8 de 8 contactos para una camara Arducam Mini OV2640 externa mediante cable corto. Orange Pi usa SPI para imagenes y I2C para configuracion; puede transmitir las imagenes por Wi-Fi. La STM32 mantiene USART1 a 115200 y los ADC de fotodiodos. La placa inferior no se modifica. Esta revision contiene hardware; no incluye una aplicacion de captura/transmision validada.

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

J8 es un cabezal THT 1x8 de paso 2.54 mm, no una huella de camara. Pin 1 cuadrado en (43,60) mm; pin 2 hacia -X, pin 8 en (25.22,60) mm. Verificar el orden impreso en el modulo antes de cablear. La alimentacion prevista es 3.3 V desde Orange Pi J7.1; no se unen sus 3.3 V a los de STM32.

J7 conserva el zocalo hembra 2x13, paso 2.54 mm, alineado con el STEP suministrado. Pin 1 en (29.845,52.444563) mm; impares hacia +X, pares 2.54 mm hacia arriba. Los 26 contactos y H5-H8 conservan exactamente la revision anterior. Taladros M3 NPTH 3.2 mm con separacion 42.110914 x 40.110918 mm; altura de separadores segun el conector real. La Orange Pi debe tener pines macho hacia abajo para este acoplamiento. Comprobar orientacion, alturas y holguras en el conjunto fisico.

Contorno 90.17 x 95.89 mm. H1-H4 de 5 mm conservan posiciones provisionales anteriores; no se ha cerrado su validacion dimensional. El STEP suministrado es una referencia CAD, no una medicion de la unidad real.

143 segmentos, 18 vias metalizadas 1.0/0.4 mm y 78.3% de longitud en B.Cu. Las 10 conexiones activas de J7 y las 8 de J8 salen por B.Cu; los cruces utilizan F.Cu y vias. El resto de las pistas heredadas se conserva.

Informes actuales: reports/ERC_CAMERA.rpt, DRC_CAMERA.rpt y CAMERA_CHECK.json. CAPAS_Y_SOLDADURA.pdf incluye montaje, esquema y capas. PINOUT.csv y GPIO26_PINOUT.csv enumeran las conexiones actuales. DEMOSTRACION_WIFI.md describe la puesta en marcha pendiente.

Pendientes: presupuesto de corriente de 3.3 V de Orange Pi con la camara conectada, presencia y nivel de los pull-ups I2C del modulo real, puesta en marcha Linux, ensayo de captura/Wi-Fi, montaje fisico y verificacion dimensional. No alimentar externamente J8.6 mientras este unido al regulador de Orange Pi. No se ha validado retroalimentacion entre fuentes. Los pines 2/4 de 5 V y el 17 de 3.3 V quedan NC. J7 no alimenta la Orange Pi desde la Nucleo; Orange Pi necesita su fuente propia.

R1-R4 siguen TBD; comprobar corriente, polaridad A/K y rango ADC de los fotodiodos antes de usarlos. Se conservan las incertidumbres anteriores de sensores. No se han realizado pruebas electricas o de vibracion ni se liberan Gerbers para fabricacion.

Comprobacion KiCad 10.0.5: ERC 0 errores / 0 advertencias. DRC con paridad: 0 errores / 8 advertencias heredadas de diferencias con biblioteca / 0 pads sin conectar / 0 problemas de paridad. No se ocultaron las advertencias. Los informes enumeran los chequeos omitidos de la configuracion heredada.
