# Integración mecánica

Placa inferior GY91/LoRa/ESC sin cambios. La nueva revisión superior retira J8-J11 y usa resistencias axiales R1-R4. Cuatro puertos de sensor en el borde inferior. J7 UART está centrado horizontalmente, debajo de la reserva para Orange Pi.

Reserva preliminar del cuerpo Orange Pi Zero LTS: 48x46 mm, x=11..59 e y=1..47 mm, marcada en Dwgs.User. Fuente: [ficha del fabricante archivada por distribuidor, página 4](https://pdf.direnc.net/upload/orange-pi-zero-lts-512m-datasheet.pdf). Esta caja no incluye conectores, voladizos, cableado, disipadores ni tolerancias. Confirmar dimensiones y revisión de la placa real antes de fijar soportes.

No hay agujeros nuevos porque no se dispone de un plano mecánico verificado de la unidad real. La reserva de espacio no constituye una fijación. Elegir soportes aislantes y confirmar altura respecto de headers Morpho, LoRa/GY91 inferiores, antena y carcasa 3U. La Orange Pi requiere alimentación propia adecuada; J7 solo lleva UART y GND.

La placa intermedia debe usar headers Morpho con continuidad vertical y longitud suficiente; un zócalo corto sin terminal pasante no transmite los pines a esta placa. No se ha verificado el conjunto físico ni su cualificación espacial.
