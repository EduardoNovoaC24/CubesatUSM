# Montaje

J7 conserva el zocalo hembra 2x13, paso 2.54 mm, alineado con el STEP suministrado. Pin 1 en (29.845,52.444563) mm; impares hacia +X, pares 2.54 mm hacia arriba. Los 26 contactos y H5-H8 conservan exactamente la revision anterior. Taladros M3 NPTH 3.2 mm con separacion 42.110914 x 40.110918 mm; altura de separadores segun el conector real. La Orange Pi debe tener pines macho hacia abajo para este acoplamiento. Comprobar orientacion, alturas y holguras en el conjunto fisico.

Contorno 90.17 x 95.89 mm. H1-H4 de 5 mm conservan posiciones provisionales anteriores; no se ha cerrado su validacion dimensional. El STEP suministrado es una referencia CAD, no una medicion de la unidad real.

J8 se monta por arriba y se suelda por abajo, como el resto de componentes THT. Es un cabezal 1x8 P2.54 mm con taladro 1 mm; su pin 1 es cuadrado. Conectar la Arducam externa con arnes corto, comprobando cada nombre de señal: no asumir que un cable plano invierte o conserva el orden. No se ha definido una fijacion de lente ni de la placa de camara. Sujetar el cable para que no tire del cabezal. J8 no tiene bloqueo; los JST XA de fotodiodos conservan su retencion.

J1/J2: Morpho 2x19. J3-J6: JST XA 2 polos. R1-R4: axial P7.62 mm, valores pendientes. Taladros electricos y vias metalizados; H1-H8 NPTH. El PCB de doble capa requiere metalizacion aunque se suelde desde abajo.

Pendientes: presupuesto de corriente de 3.3 V de Orange Pi con la camara conectada, presencia y nivel de los pull-ups I2C del modulo real, puesta en marcha Linux, ensayo de captura/Wi-Fi, montaje fisico y verificacion dimensional. No alimentar externamente J8.6 mientras este unido al regulador de Orange Pi. No se ha validado retroalimentacion entre fuentes. Los pines 2/4 de 5 V y el 17 de 3.3 V quedan NC. J7 no alimenta la Orange Pi desde la Nucleo; Orange Pi necesita su fuente propia.

R1-R4 siguen TBD; comprobar corriente, polaridad A/K y rango ADC de los fotodiodos antes de usarlos. Se conservan las incertidumbres anteriores de sensores. No se han realizado pruebas electricas o de vibracion ni se liberan Gerbers para fabricacion.
