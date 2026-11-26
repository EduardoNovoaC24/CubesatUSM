# Integracion mecanica de GPIO26

- J7 es el conector completo hembra de 26 contactos. Solo transmite UART y un GND; los otros contactos se dejan aislados.
- La Orange Pi se monta encima, con sus componentes hacia arriba y el cabezal macho de acoplamiento hacia abajo. Un cabezal con pines largos hacia arriba requiere cambiar la disposicion del cabezal; no se corrige invirtiendo electricamente la numeracion.
- Mantener pin 1 cuadrado de ambas placas en correspondencia. El plano del PDF muestra la numeracion completa en vista superior; la pagina de B.Cu esta espejada para verla desde abajo.
- H5-H8 usan el patron real del STEP, 42.110914 x 40.110918 mm, y no el nominal anterior 42x40. Todos son NPTH de 3.2 mm para M3. La envolvente de herraje prevista es de 7 mm; comprobar la pieza real elegida.
- La altura de los cuatro separadores M3 debe corresponder a la altura real del conector macho-hembra acoplado y dejar espacio a pines, piezas y cableado. No se ha elegido ni ensayado esa altura.
- No se incluyen sockets para el conector auxiliar de 13 pines ni para el UART de depuracion de tres pines del modelo. Comprobar que sus pines no toquen esta shield.
- H1-H4 se mantienen como taladros de 5 mm de la estructura CubeSat, con la posicion provisional de la revision anterior. La placa inferior con LoRa/GY91/ESC no cambia.

El STEP se conserva en references. El patron de centros y la numeracion por pad cuadrado se verificaron geometricamente contra ese archivo; se requiere una prueba en seco con la placa fisica antes de fabricar. Se trata de concordancia con un modelo CAD, no de un ensayo fisico de encaje o vibracion.
