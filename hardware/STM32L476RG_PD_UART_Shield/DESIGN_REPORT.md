# Informe - revisión 0.2-P

Se eliminaron J8-J11 y se incorporaron R1-R4, una carga resistiva por fotodiodo. Cada puerto JST tiene pin 1 al ADC y pin 2 a GND; cada resistencia está en paralelo entre esos puntos. No hay bias externo ni amplificador. Valores R_LOAD TBD y A/K provisionales hasta verificar el sensor real.

J7 se reubicó a (32.5,51.5) mm, centrando los tres contactos en x=35 mm. Reserva del cuerpo de Orange Pi Zero LTS 48x46 mm en Dwgs.User, sin componentes altos nuevos dentro de esa caja. Sin agujeros de soporte inventados. La posición y la reserva son preliminares y no validan montaje, altura, tolerancias ni salida de cables.

Los cuatro ADC mantienen PA0, PA1, PC2 y PC3; UART conserva PA9/PA10. La placa inferior GY91/LoRa/ESC no fue modificada, comprobado mediante SHA256. Ver PINOUT.md, CONDITIONING_PENDING.md y STACK_INTEGRATION.md.

## Verificaciones ejecutadas

- KiCad 10.0.5 ERC: 0 errores y 0 advertencias.
- DRC con severidad completa y paridad esquemático/PCB: 0 infracciones, 0 conexiones pendientes y 0 problemas de paridad.
- Reglas explícitas: separación mínima de cobre 0.30 mm, pistas de al menos 0.60 mm.
- 11 componentes, 95 pads, 55 segmentos de pista, dos capas, sin vías adicionales. Pistas horizontales, verticales y a 45 grados dentro de tolerancia de serialización de 1 nm.
- Comprobación de conectividad independiente: cuatro cadenas JST-resistencia-ADC, retornos GND y UART correctos; sin sockets AFE. Detalle en reports/CONNECTIVITY_CHECK.json.
- Esquemático PDF, PDF de ambas capas y vista de PCB revisados visualmente.

Los informes reales enumeran también comprobaciones omitidas por configuración; véanse reports/ERC.rpt y reports/DRC.rpt. ERC/DRC no comprueban la sensibilidad ni el rango del sensor desconocido. No se ejecutaron medidas, simulación analógica, validación de tiempos ADC ni prueba del apilado físico. La topología no garantiza usar todo el rango de 0-3.3 V. No se entregan Gerbers definitivos. Los PDF de cobre son vistas de inspección 1:1.
