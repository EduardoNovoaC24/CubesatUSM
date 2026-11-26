# Montaje de Orange Pi Zero LTS sobre shield

1. H5-H8 son los agujeros exclusivos de Orange Pi: patron 42x40 mm, confirmado por el usuario; taladro 3.2 mm NPTH para M3.
2. La referencia del cuerpo ocupa x=21.085..69.085 e y=7.695..53.695 mm. Las coordenadas corresponden a la shield ampliada de 90.17x95.89 mm, no al antiguo contorno de 70x82.5.
3. Colocar cuatro separadores M3 hembra-hembra y fijarlos a la shield por debajo con cuatro tornillos. Sujetar Orange Pi arriba con otros cuatro tornillos. Elegir longitud que no invada componentes ni exceda la rosca util. Puede utilizarse herraje aislante compatible; no se presupone continuidad electrica por los soportes.
4. La envolvente prevista por soporte es de 7 mm de diametro, comprobada separada del cobre y conectores actuales. Si el herraje real es mayor, revisar nuevamente. La altura no esta fijada: depende de los pines y piezas de la cara inferior de Orange Pi y del conjunto real.
5. Comprobar acceso al RJ45, USB, microSD y el cable UART. J7 conserva RX STM32 / TX STM32 / GND; no alimenta Orange Pi. La fijacion mecanica no sustituye una alimentacion adecuada.
6. H1-H4 son los agujeros grandes de 5 mm para estructura CubeSat y no deben confundirse con los M3. Sus posiciones siguen siendo provisionales segun la revision mecanica anterior.

La placa inferior con LoRa/GY91/ESC no se ha modificado. El patron de Orange Pi se basa en la confirmacion del usuario; no se logro descargar el DXF oficial durante esta actualizacion. Revisar el plano 1:1 en seco sobre la placa fisica antes de perforar o encargar el montaje. No se han realizado ensayos fisicos de rigidez o vibracion.
