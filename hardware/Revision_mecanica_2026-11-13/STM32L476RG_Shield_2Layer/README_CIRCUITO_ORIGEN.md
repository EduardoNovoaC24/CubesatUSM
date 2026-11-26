# STM32L476RG Shield - revisión de dos capas

Actualización: 5 de noviembre de 2026, revisión 2.1-P. Esta revisión reemplaza la restricción PC/104 y la fabricación artesanal de una cara. El proyecto anterior permanece intacto en la carpeta hermana STM32L476RG_PC104_Shield.

El trazado actual tiene 118 tramos horizontales, verticales y diagonales de 45 grados, en lugar de los 1504 segmentos pequeños anteriores. Se conservan 70 x 82,5 mm, pistas de 0,6 mm y seis vías. ERC/DRC y paridad esquemática actualizados sin infracciones ni conexiones pendientes. Copia anterior en backups/2026-11-05_before_straightening.

Abrir STM32L476RG_Shield_2Layer.kicad_pro. Incluye esquemático, PCB y bibliotecas locales Shield2. No depende de los puentes ni de las reglas especiales NPTH del proyecto anterior.

## Distribución

Contorno rectangular nominal de 70 x 82,5 mm: misma envolvente exterior que la NUCLEO-L476RG completa, incluido ST-LINK. No se pretende reproducir radios de esquinas, recortes o taladros no acotados. No hay agujeros de fijación inventados.

J1 y J2 son los Morpho hacia la Nucleo, debajo del shield. Posiciones según UM1724: columnas CN7 en x=3,25 y 5,79 mm; CN10 en x=64,21 y 66,75 mm; primera fila y=31,70 mm y 19 filas a paso 2,54 mm. Columnas exteriores separadas 63,50 mm. Numeración visible desde arriba: impares a la izquierda de cada conector, pares a la derecha.

J3 GY-91: header vertical en x=12 mm, pin 1 en y=33 mm; cuerpo preliminar conservador 14 x 25 mm a su derecha. J4 Waveshare: dos filas de 20 contactos, paso 2,54 mm, separación 17,78 mm, pin 1 en (34;29,5) mm; cuerpo nominal 21 x 52 mm, conexión RF hacia el borde inferior. J5, abajo a la izquierda: salida de control al ESC y GND, sin alimentación de potencia.

La zona superior queda sin módulos para facilitar acceso a USB/ST-LINK de la Nucleo que está debajo. La altura de los headers inferiores y el paso físico del cable USB deben comprobarse con los conectores elegidos; el contorno igual no acredita por sí solo esas alturas. Los courtyards de módulos representan envolventes preliminares, no una medición de los ejemplares.

## Fabricación nueva

Dos capas reales: F.Cu y B.Cu. Pistas de 0,6 mm y separación nominal mínima de 0,3 mm. Pads Morpho 2 x 2 mm; GY-91 y Waveshare 3 x 2 mm. Pads de J5 2 x 3 mm. Taladros candidatos de conectores: 1,0 mm. Agujeros metalizados convencionales; vías de 1,0 mm con taladro de 0,4 mm. Se requieren taladros/vías metalizados para que el trazado sea válido. No es la fabricación manual NPTH de la revisión anterior.

Se retiran JP1-JP5: las conexiones, incluidos GND y 3V3, se realizan con cobre en ambas capas y vías. No se incluyen puentes físicos de cable.

## Motor 2212 2200KV y relé

El motor brushless necesita un ESC externo para conmutar sus tres fases. No conectar sus fases a J5 ni a pines STM32. Un relé no reemplaza al ESC ni es un adaptador adecuado de una señal de control por pulsos.

J5.1 = PA8 / MOTOR_PWM (lógica de 3,3 V). J5.2 = GND. La batería alimenta al ESC y el ESC al motor; esa corriente no pasa por este shield. Mantener GND de referencia compartido por la interfaz que lo requiera. No unir al shield el conductor de salida BEC del cable de control del ESC; aquí no se usa para alimentar la Nucleo.

El usuario identificó el ESC como ZMR Bidireccional 30A. No se encontró una especificación primaria de su umbral lógico de entrada. Por tanto, no se certifican compatibilidad del umbral de 3,3 V, protocolo/rango de pulsos, corriente, tensión de batería, inversión de giro ni frenado. J5 es una interfaz provisional hasta revisar ese modelo. Si el ESC requiere otro nivel, se deberá adaptar la lógica; no añadir un relé por esa causa.

Referencia primaria de arquitectura ESC-batería-motor y cable BEC (ejemplo del fabricante, NO identificación de tu ESC): https://www.hobbywing.com/en/uploads/file/20221015/7e7628e06047f99a5b65b58cc5c4f84d.pdf .

Se conservan las decisiones previas: GY-91 por I2C PB8/PB9 a 3V3, Waveshare a 3V3 en pin 36 sin batería/VSYS/VBUS, PC0 con SB51 cerrado y SB52 abierto confirmado por el usuario. Las incertidumbres internas del GY-91 y la comprobación de consumo USB no se declaran resueltas; no impiden esta revisión CAD autorizada.
