# Exportaciones de prototipo de dos capas

`fabrication_draft` contiene Gerber F.Cu, B.Cu, máscaras superior/inferior, serigrafía superior y Edge.Cuts. Son capas convencionales con las mismas coordenadas: no espejarlas para enviarlas a fabricación.

PTH.drl contiene 132 perforaciones metalizadas: 126 de 1,0 mm y seis de 0,4 mm. NPTH.drl está vacío. No solicitar fabricación sin metalización; las vías y los contactos entre capas dependen de ella.

F_Cu_1TO1.pdf y B_Cu_1TO1.pdf son referencias de inspección a escala 1:1. Ambos están SIN espejo, en vista superior proyectada; B.Cu se muestra como si se mirara a través del sustrato. No representan dos vistas enfrentadas. Imprimir a tamaño real, sin ajustar a página. El contorno debe medir 70 x 82,5 mm. Son PDF de revisión; para fabricar profesionalmente utilizar Gerber más taladros.

Pistas de 0,6 mm; clearance nominal 0,3 mm; vías 1,0/0,4 mm. Las bibliotecas de huellas son locales en Shield2.pretty; no contienen las transiciones NPTH especiales de la revisión antigua.

Antes de fabricar: cerrar la selección de headers y probar altura/acceso del conjunto. La compatibilidad lógica de J5 con el ZMR Bidireccional 30A requiere confirmar su umbral de entrada. Conservar las comprobaciones pendientes de alimentación y módulos señaladas en README.md; un DRC limpio no las verifica.
