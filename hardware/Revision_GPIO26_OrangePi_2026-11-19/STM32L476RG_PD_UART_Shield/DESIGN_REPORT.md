# Revision GPIO26 - 19 noviembre 2026

J7 pasa de UART separado de tres contactos a zocalo GPIO26 2x13 de acoplamiento directo. Solo se conectan los contactos 6 GND, 8 TX Orange Pi hacia RX STM32 y 10 RX Orange Pi desde TX STM32. Otros 23 contactos NC, sin alimentacion compartida.

Geometria: los 26 centros coinciden con el STEP proporcionado, sin cambiar el paso de 2.54 mm. Se identifico el pad cuadrado y se tuvo en cuenta que la cara de componentes del modelo esta orientada hacia -Z. La transformacion de proyeccion se documenta en reports/STEP_ALIGNMENT.json. H5-H8 se ajustaron aproximadamente 0.055 mm por eje para reproducir el patron del modelo. H1-H4 y componentes restantes conservan sus posiciones.

Se retrazaron UART y un ramal GND, con desplazamiento local de tres segmentos GND para abrir acceso a PA10. Total 85 segmentos y 4 vias; 84.0% de la longitud en B.Cu. Las tres salidas de J7 estan en B.Cu. Se conservan esquema y pinout funcional de los fotodiodos; solo cambia la interfaz UART. La placa inferior no se modifica.

ERC ejecutado: 0 errores y 0 advertencias. DRC ejecutado con paridad: 0 errores, 8 advertencias de diferencias con biblioteca, 0 conexiones pendientes y 0 problemas de paridad. No se excluyeron esas advertencias para declarar limpio el resultado. Los informes enumeran tambien los chequeos omitidos por la configuracion heredada. Informes actuales y comprobaciones independientes en reports.

No se han elegido ni probado el socket comercial, su altura de acoplamiento o los separadores finales. El modelo contiene cabezales y conectores cuya disposicion fisica debe revisarse antes del montaje. No se han medido la unidad real ni realizado ensayos electricos, mecanicos o de vibracion. R1-R4 y validaciones previas de sensores siguen pendientes. El patron CubeSat de los agujeros grandes conserva la condicion provisional. No se liberan Gerbers definitivos.
