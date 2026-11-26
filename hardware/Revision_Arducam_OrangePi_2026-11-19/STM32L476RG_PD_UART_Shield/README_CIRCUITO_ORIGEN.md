# Placa superior PD + UART - revisión 0.2-P

Se retiraron los cuatro conectores AFE J8-J11. Cada fotodiodo externo tiene ahora una resistencia de carga R1-R4 entre la señal ADC y GND, preparada para lectura pasiva sin polarización externa. El valor de cada resistencia está marcado TBD: debe elegirse y montarse antes del uso, a partir de medidas del sensor real. No hay amplificadores, divisores LDR ni fotodiodos montados en esta PCB.

Se conservan cuatro conectores JST XA polarizados con bloqueo: J3 (+X), J4 (-X), J5 (+Y), J6 (-Y). Pin 1=A provisional hacia ADC; pin 2=K provisional hacia GND. Comprobar la polaridad del sensor antes de cablear. Ver PINOUT.md.

J7 se trasladó desde (43,24) a (32.5,51.5) mm: sus tres pines quedan centrados horizontalmente en x=35 mm. La parte superior queda libre de componentes nuevos altos en un rectángulo preliminar de 48x46 mm (x=11..59; y=1..47), dibujado en Dwgs.User. La reserva representa solo el cuerpo de la Orange Pi Zero LTS: no valida puertos, cables, agujeros ni altura. No se inventaron agujeros de fijación. Ver STACK_INTEGRATION.md.

PCB de 70x82.5 mm y dos capas. La placa inferior con GY91, LoRa y ESC se conserva intacta. Los cuatro ADC y el UART mantienen los pines asignados. Los conectores Morpho requieren contactos pasantes adecuados en la placa intermedia.

Proyecto: STM32L476RG_PD_UART_Shield.kicad_pro. Esquemático, PCB y bibliotecas locales incluidos. Los informes reales se encuentran en reports. No se liberan Gerbers definitivos mientras falten comprobaciones eléctricas y mecánicas.
