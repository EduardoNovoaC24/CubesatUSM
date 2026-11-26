# Orange Pi Zero LTS - acoplamiento directo GPIO26 - 19 noviembre 2026

J7 ya no es un JST UART de tres polos. Es un zocalo hembra THT de 2x13 contactos, paso 2.54 mm, situado en la posicion del GPIO26 del STEP suministrado. Se mantienen activos solo pin 6 GND, pin 8 TX Orange Pi hacia RX STM32 y pin 10 RX Orange Pi desde TX STM32. Los otros 23 contactos quedan sin conexion, incluidos 3.3 V y 5 V. La Orange Pi necesita alimentacion independiente.

## Geometria

Se extrajeron del STEP los 26 centros, el pad cuadrado y los cuatro agujeros M3. Los 26 contactos de J7 coinciden con la proyeccion del modelo, sin escalar el paso. Pin 1 de J7: X=29.845 mm, Y=52.444563 mm, referido al origen superior izquierdo de la shield. Los impares avanzan hacia +X en la fila inferior; los pares estan 2.54 mm hacia arriba. El pin 1 se marca con pad cuadrado.

El modelo tiene separacion entre centros M3 de 42.110914 x 40.110918 mm, ligeramente distinta del nominal 42x40 anterior. H5-H8 se desplazaron unos 0.055 mm por eje para usar la misma referencia geometrica que J7. Mantienen taladro NPTH de 3.2 mm y envolvente de herraje prevista de 7 mm. H1-H4, de 5 mm para la estructura CubeSat, se conservan en sus posiciones anteriores provisionales. Ver ORANGEPI_MONTAJE.csv y MONTAJE_COORDENADAS.csv.

La cara de componentes del STEP esta orientada hacia -Z; se aplico una rotacion coherente para obtener la vista superior de la placa montada. La transformacion y comprobacion de todos los centros estan en reports/STEP_ALIGNMENT.json. El STEP es una referencia CAD suministrada por el usuario y no sustituye una comprobacion dimensional de su unidad fisica.

## Acoplamiento

J7 se instala como hembra en la cara superior de la shield. Los pines macho de Orange Pi deben sobresalir por su cara inferior, hacia la shield, manteniendo la cara de componentes de Orange Pi arriba. Si el cabezal actual tiene los pines largos hacia arriba, no enchufara directamente en esta disposicion: se necesita la orientacion de cabezal correspondiente. No invertir la placa para compensarlo sin revisar el pinout.

Elegir la altura de los cuatro separadores M3 para que coincida con la distancia entre placas del conector realmente acoplado. No fijar una altura arbitraria ni forzar la placa con los tornillos. Revisar pines auxiliares de 13 contactos, USB, Ethernet, microSD y herraje. Se conservaron esos conectores solo como elementos del modelo de referencia; no se añaden conectores electricos para ellos en esta shield.

## Trazado y comprobaciones

Se retrazaron TX/RX y el ramal GND hacia J7. Tres segmentos GND se desplazaron localmente para dejar entrada por B.Cu al pad de PA10. Los circuitos de fotodiodos y asignaciones STM32 se conservan. Total: 85 segmentos, 4 vias metalizadas de 1.0/0.4 mm, 84.0% de longitud de pistas en B.Cu. Las tres conexiones de J7 salen de sus pads por B.Cu.

ERC: 0 errores y 0 advertencias. DRC con paridad: 0 errores, 8 advertencias anteriores de diferencias con biblioteca, 0 conexiones pendientes y 0 problemas de paridad. Informes actuales: reports/ERC_GPIO26.rpt y reports/DRC_GPIO26.rpt. Verificacion de pinout, NC y capas: reports/GPIO26_CHECK.json. Los informes de versiones previas estan en reports/history_pre_gpio26.

CAPAS_Y_SOLDADURA.pdf contiene plano 1:1 de montaje, esquematico actualizado y vistas B.Cu inferior espejada y F.Cu superior. Imprimir el plano al 100%, sin ajustar, y contrastarlo con la placa fisica antes de fabricar. No espejar nuevamente la vista inferior.

No se han efectuado pruebas fisicas ni se ha elegido un modelo comercial concreto de socket con altura de acoplamiento. Siguen pendientes las validaciones electricas previas, los valores R1-R4 y el patron CubeSat H1-H4. No es liberacion definitiva de fabricacion. Para soldar solo por debajo se necesitan agujeros metalizados de componentes y vias; los agujeros mecanicos son NPTH.
