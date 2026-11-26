# Revision para soldadura inferior - 13 noviembre 2026

Trazado priorizado en B.Cu (cara inferior): 75.4% de la longitud de pistas. F.Cu resuelve los cruces restantes. 2 vias metalizadas de pad 1.00 mm y taladro 0.40 mm. No se afirma un minimo matematico global: se eligio una opcion practica con menos vias que las alternativas ensayadas.

Todos los componentes mantienen su posicion y pinout. Las seis huellas adicionales sin red de la placa inferior se sustituyeron por conexiones entre capas correctas; se elimino un cambio de capa innecesario. Se conservan contorno 90.17x95.89 mm y cuatro NPTH de 5 mm. La posicion de estos agujeros sigue siendo PROVISIONAL: consultar MECANICA_ACTUALIZADA.md.

## Soldadura

Para soldar los pines de componentes solo desde abajo se requiere PCB de dos capas con agujeros de componentes y vias METALIZADOS. El anillo inferior queda conectado al superior por el metal del taladro. No equivale a una placa artesanal de dos caras sin metalizar, que necesitaria algunas uniones soldadas tambien por arriba. Los cuatro agujeros grandes de montaje son NPTH y no conectan capas.

CAPAS_Y_SOLDADURA.pdf: primera pagina B.Cu vista desde abajo, espejada; segunda F.Cu vista desde arriba, sin espejo. Ambas a escala 1:1, para inspeccion. No volver a espejar la primera pagina. PCB_LAYOUT.png muestra las capas superpuestas desde arriba, azul=B.Cu y rojo=F.Cu. VIAS.csv identifica las vias.

## Verificacion

DRC final: 0 errores eléctricos, 9 advertencias de diferencias con biblioteca, 0 conexiones pendientes y 0 problemas de paridad. Informe real en reports/DRC_SOLDADURA.rpt. Comprobacion de posicion y metricas en reports/ROUTING_CHECK.json. Esquematico sin cambios respecto de la revision mecanica; los informes anteriores se conservan solo como historial. Se comprobaron las vistas exportadas.

Se mantienen pendientes el valor de las resistencias de fotodiodos, las validaciones de modulos y la confirmacion del patron de montaje. No liberado a fabricacion definitiva. No usar antiguos Gerbers ni taladros de otras revisiones.
