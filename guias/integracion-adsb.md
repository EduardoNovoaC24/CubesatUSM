# Integración ADS-B

Orange Pi entrega las tramas ADS-B al STM32 por USART1 a 115200, 8N1. El proyecto valida las tramas y puede reenviarlas por USART2 al radar de la estación terrestre.

1. Sigue el [cableado y protocolo ADS-B](../ADSB_ORANGEPI.md).
2. Identifica el puerto ST-LINK con `py ground_station/radar_stm32.py --list-ports`.
3. Abre la [estación local](estacion-lora.md) en la pestaña Radar ADS-B.

Los diseños del shield y la revisión Orange Pi están en `hardware/`. Las tramas con latitud y longitud permiten mostrar los marcadores en el mapa.
