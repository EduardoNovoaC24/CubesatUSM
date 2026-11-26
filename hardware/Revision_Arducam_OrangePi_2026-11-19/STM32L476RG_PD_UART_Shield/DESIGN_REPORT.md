# Revision Arducam - 19 noviembre 2026

Se agrega J8 para Arducam Mini SPI/I2C de 8 pines, controlada y alimentada a 3.3 V por Orange Pi. J7 habilita 7 contactos adicionales: 1/3/5/19/21/23/24. UART 6/8/10 se conserva; 16 contactos quedan NC. No se alteran las redes de fotodiodos, los conectores Morpho, la placa inferior ni los taladros y posiciones de los componentes existentes. Verificacion independiente en reports/CAMERA_CHECK.json.

143 segmentos, 18 vias metalizadas 1.0/0.4 mm y 78.3% de longitud en B.Cu. Las 10 conexiones activas de J7 y las 8 de J8 salen por B.Cu; los cruces utilizan F.Cu y vias. El resto de las pistas heredadas se conserva.

Se eleva un tramo GND existente a F.Cu con dos vias para permitir el cruce de buses. Las restantes vias permiten conservar salida inferior de los pads nuevos. No hay puentes de hilo.

Las cifras de ERC/DRC de esta revision se encuentran en reports/ERC_CAMERA.rpt y reports/DRC_CAMERA.rpt; no sustituirlas por informes historicos GPIO26. Esas comprobaciones no validan alimentacion real, software ni montaje.

Pendientes: presupuesto de corriente de 3.3 V de Orange Pi con la camara conectada, presencia y nivel de los pull-ups I2C del modulo real, puesta en marcha Linux, ensayo de captura/Wi-Fi, montaje fisico y verificacion dimensional. No alimentar externamente J8.6 mientras este unido al regulador de Orange Pi. No se ha validado retroalimentacion entre fuentes. Los pines 2/4 de 5 V y el 17 de 3.3 V quedan NC. J7 no alimenta la Orange Pi desde la Nucleo; Orange Pi necesita su fuente propia.

R1-R4 siguen TBD; comprobar corriente, polaridad A/K y rango ADC de los fotodiodos antes de usarlos. Se conservan las incertidumbres anteriores de sensores. No se han realizado pruebas electricas o de vibracion ni se liberan Gerbers para fabricacion.

Comprobacion KiCad 10.0.5: ERC 0 errores / 0 advertencias. DRC con paridad: 0 errores / 8 advertencias heredadas de diferencias con biblioteca / 0 pads sin conectar / 0 problemas de paridad. No se ocultaron las advertencias. Los informes enumeran los chequeos omitidos de la configuracion heredada.
