# Ensamblaje de la revisión de dos capas

J1 y J2: sockets hembra 2x19 hacia los pines Morpho de la Nucleo, montados debajo del shield. Los terminales atraviesan agujeros metalizados y se pueden soldar desde arriba. Elegir altura y longitud de terminal de modo que el shield no toque USB, pulsadores, jumpers ni componentes de la Nucleo. No forzar tornillos para compensar desalineación.

J3: socket 1x8 para el GY-91; orden local pin 1 a 8: VIN, 3V3, GND, SCL, SDA, SA0, NCS, CSB. VIN no se usa. La foto del ejemplar debe contrastarse con esta orientación antes de insertar el módulo.

J4: dos tiras macho 1x20, paso 2,54 mm y separación 17,78 mm, que reciben las hembras existentes del Waveshare. En vista superior, pin 1 arriba a la izquierda, pin 20 abajo a la izquierda, pin 40 arriba a la derecha y pin 21 abajo a la derecha. Conexión RF orientada hacia abajo. Alimentación 3V3 al pin 36; VSYS 39 y VBUS 40 sin conectar; sin batería.

J5: pin 1 = señal PA8 / MOTOR_PWM; pin 2 = GND. Se conecta al cable de señal del ESC, no al motor. El tercer conductor de BEC del ESC, si existe, no se conecta al shield y debe mantenerse aislado. No alimentar motor ni ESC desde 3V3, 5V ni el USB de la Nucleo a través de esta PCB.

ESC informado: ZMR Bidireccional 30A. Preparar un adaptador de dos conductores que lleve las funciones correctas a J5; no asumir que el enchufe original de tres posiciones se conecta directamente al header de dos posiciones. La ficha comercial consultada no especifica el umbral de entrada de 3,3 V; debe comprobarse con documentación o medida del ejemplar.

El motor 2212 2200KV se conecta por sus tres fases al ESC externo; la batería se conecta a ese ESC según su manual. El ESC informado es ZMR Bidireccional 30A; falta confirmar con documentación de esa versión su umbral de señal de 3,3 V y sus parámetros exactos de pulsos. No existe un relé en la señal.

Esta revisión no lleva puentes de cable. Todos los contactos y seis vías deben estar metalizados. Una placa artesanal de dos caras sin metalización no reproduce la conectividad verificada.
