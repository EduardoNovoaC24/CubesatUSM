# Shield Orange Pi - fijacion mecanica - 19 noviembre 2026

Se añadieron cuatro agujeros H5-H8, NPTH de 3.2 mm para fijar Orange Pi Zero LTS mediante separadores M3. Patron entre centros de 42 x 40 mm confirmado por el usuario en esta conversacion. Centros a 3 mm de cada borde del cuerpo reservado de 48 x 46 mm. Coordenadas en ORANGEPI_MONTAJE.csv; MONTAJE_COORDENADAS.csv incluye los ocho agujeros.

H5: (24.085,10.695); H6: (66.085,10.695); H7: (24.085,50.695); H8: (66.085,50.695), en mm, origen esquina superior izquierda de la shield. Estos agujeros no son los cuatro grandes del montaje CubeSat: H1-H4 siguen siendo de 5 mm y su posicion conserva la condicion provisional anterior.

Utilizar cuatro separadores M3 fijados por ambos extremos, sin apoyar Orange Pi directamente sobre la shield. Se reserva una envolvente de herraje de 7 mm de diametro; elegir tornillos, arandelas y separadores que quepan. La altura del separador y longitud de tornillos dependen de los componentes y conectores reales. Comprobar USB, Ethernet, tarjeta microSD, pines inferiores y acceso a J7. No se ha ensayado el montaje fisico ni se presupone altura disponible.

El trazado, las dos vias, UART, conectores de fotodiodos y esquema electrico son identicos a la revision anterior. Solo se modifica esta shield superior; placa LoRa/GY91/ESC sin cambios.

## Verificacion ejecutada

DRC con paridad: 0 errores, 0 conexiones pendientes y 0 problemas de paridad. Persisten 9 advertencias previas de diferencias entre huellas y biblioteca. Informe real: reports/DRC_ORANGEPI.rpt. No hay nuevas infracciones por agujeros o patios. Comprobacion independiente: envolvente de herraje de 7 mm separada del cobre por al menos 2.005 mm en el caso mas cercano, usando calculo conservador de pistas y pads. Detalle en reports/MOUNTING_CHECK.json.

CAPAS_Y_SOLDADURA.pdf: pagina 1 plano de montaje a escala 1:1; pagina 2 B.Cu vista inferior espejada; pagina 3 F.Cu vista superior. Imprimir al 100%, sin ajustar. No espejar de nuevo la pagina inferior. PCB_LAYOUT.png muestra la vista superior y los nuevos agujeros.

Para soldar los componentes solo por debajo, sus taladros y las vias deben estar metalizados; los ocho agujeros mecanicos son NPTH. Siguen pendientes el valor de R1-R4, las validaciones electricas de sensores y la mecanica CubeSat previamente indicada. No es liberacion definitiva de fabricacion. Los informes anteriores incluidos son historial, no resultados de esta revision.
