# Resultado de la revisión 2.0-P y actualización 2.1-P

3 de noviembre de 2026. KiCad 10.0.5. Proyecto nuevo STM32L476RG_Shield_2Layer; se conserva sin modificar el diseño PC/104 de una cara.

## Cambios realizados

Contorno de 70 x 82,5 mm, que coincide con la envolvente de la Nucleo completa. Rectángulo nominal: no se inventan radios, recortes ni perforaciones. Se mantienen los Morpho CN7/CN10 y su numeración original. El GY-91 se dispone a la izquierda del LoRa, que queda longitudinal con RF hacia el borde inferior. J5 queda abajo a la izquierda y se identifica como interfaz al ESC. Envolventes de cuerpos de módulos en F.Fab y courtyards conservadores en las huellas; no sustituyen una prueba de montaje con los módulos físicos.

Se retiraron todos los puentes JP1-JP5 y las redes divididas `_WIRE1`. Se restauraron las 12 redes originales. El trazado completo utiliza ambas capas: 899 segmentos F.Cu y 605 B.Cu, todos de 0,6 mm; seis vías metalizadas de 1,0/0,4 mm. Los segmentos cortos aproximan cambios de dirección alrededor de obstáculos y no implican reducciones del ancho de pista.

Los conectores tienen 126 pads PTH; más seis vías, el total es 132 perforaciones metalizadas. 126 taladros son de 1 mm y seis de 0,4 mm. No hay perforaciones NPTH ni puentes de cable. Los taladros de conector son candidatos hasta confirmar las dimensiones de los terminales seleccionados. Esta revisión requiere un proceso de fabricación con metalización: no basta material de dos caras con agujeros sin metalizar.

## Verificaciones ejecutadas

- Esquemático nativo cargado por KiCad CLI: ERC 0 errores y 0 advertencias.
- PCB nativa cargada por KiCad CLI: DRC con todas las severidades y paridad esquemática, 0 infracciones, 0 conexiones pendientes, 0 errores de paridad.
- Archivo PCB recargado por pcbnew API: 70 x 82,5 mm, cinco conectores, 126 pads PTH, seis vías y pistas exclusivamente F.Cu/B.Cu de 0,6 mm.
- Comparación exacta de todos los contactos y sus redes con la netlist XML exportada por KiCad: coincidentes, incluidos los pines NC sin pistas.
- Sin reglas especiales para omitir fallos de agujeros/pistas y sin las excepciones del modelo NPTH de la revisión anterior. Se utilizan pads/vías PTH convencionales.
- Vistas y tres PDF renderizados e inspeccionados; capas a 1:1 centradas mediante traslación, sin espejo ni cambio de escala. Gerber y reporte de taladros generados.

Evidencias: reports/ERC.rpt, reports/DRC.rpt, reports/FINAL_CHECK.txt y reports/DRILL.rpt. El DRC comprueba conectividad y reglas CAD, no el funcionamiento de los módulos reales ni las alturas de ensamblaje.

## Interfaz del motor

El usuario indicó motor brushless 2212 2200KV. Ese dato no identifica su ESC. La PCB entrega en J5 solamente PA8 a 3,3 V y GND; no se conecta a las tres fases del motor ni las alimenta. No se agregó relé: la conmutación de fases debe realizarla un ESC externo. El modelo de ESC, su umbral de señal y protocolo están pendientes. No se seleccionó arbitrariamente un ESC ni un adaptador de nivel.

Como referencia general del fabricante, la documentación de Hobbywing muestra batería hacia ESC, tres conductores del ESC al motor y un cable de control con posible salida BEC: https://www.hobbywing.com/en/uploads/file/20221015/7e7628e06047f99a5b65b58cc5c4f84d.pdf . No es una validación de un ESC específico del usuario.

## Límites pendientes

Confirmar altura y longitud de los headers, separación del USB/ST-LINK y acceso a tornillos/antena con el montaje real. El metalizado permite soldar desde arriba los terminales de sockets colocados debajo, pero no determina por sí mismo su altura ni longitud. No se declaran resueltos los pendientes del GY-91 ni el consumo máximo del conjunto por USB. La continuidad del trabajo CAD fue autorizada con esas incertidumbres. Los Gerber se identifican como prototipo, no como aprobación final de fabricación.

## Actualización 2.1-P - 5 de noviembre de 2026

El usuario identificó el ESC como ZMR Bidireccional 30A y pidió pistas rectas con esquinas diagonales/rectangulares. Se conservaron dimensiones, conectores, redes, ancho de pista y seis vías. No se agregaron relés ni adaptadores de nivel sin especificación de entrada del ESC.

Se reemplazaron los 1504 segmentos del trazado anterior por 118 segmentos: 75 en F.Cu y 43 en B.Cu. Todos son horizontales, verticales o diagonales exactas de 45 grados; se eliminaron las aproximaciones de curvas mediante pequeños escalones. Permanecen algunos tramos cortos de conexión a pads/vías y cambios de dirección, no arcos ni segmentos con ángulos arbitrarios. La comprobación del archivo recargado verificó esos ángulos y el ancho de 0,6 mm. Se mantiene la metalización convencional, sin cables de puente.

ERC actualizado: 0 errores y 0 advertencias. DRC con paridad esquemática actualizado: 0 infracciones, 0 conexiones pendientes y 0 diferencias con el esquema. Evidencias en reports/DRC_STRAIGHT.rpt, reports/DRC.rpt, reports/ERC.rpt y reports/FINAL_CHECK.txt. Se actualizaron Gerber de cobre, PDF y vista de PCB. Los taladros y su reporte anterior siguen siendo válidos porque no cambió ninguna perforación.

Copia previa: backups/2026-11-05_before_straightening, incluidos PCB, esquema, PDF, imágenes y plan de rutas. La revisión PC/104 de una cara no se modificó.

Se localizó una ficha comercial que coincide con el nombre indicado: https://www.mechatronicstore.cl/controlador-esc-zmr-bidireccional-12a-a-200a/ . El vendedor anuncia PWM de servo y BEC; no publica un umbral VIH que permita aprobar los 3,3 V del STM32. No se ha localizado un esquema/manual primario de esa versión que complete ese dato. El modelo informado ya no está pendiente de identificación, pero su compatibilidad de nivel lógico sigue sin verificarse. El valor de corriente 30A no demuestra compatibilidad con un GPIO ni determina por sí solo batería, configuración de pulsos o consumo del motor.
