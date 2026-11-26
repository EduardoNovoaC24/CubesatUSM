# Lectura pasiva con resistencia - validación pendiente

Se sustituyeron los sockets de módulos AFE por cuatro resistencias de carga independientes. Topología por canal:

```text
J_PD pin 1 (A provisional) ----+---- ADC STM32
                             |
                         R_LOAD TBD
                             |
J_PD pin 2 (K provisional) ----+---- GND
```

El fotodiodo se monta remotamente entre ambos terminales del JST. Se propone operación fotovoltaica sin alimentación ni bias externo. No es un divisor para LDR. El valor TBD no es 0 ohmios ni una instrucción de dejar el canal sin resistencia: hay que determinar y montar una resistencia finita antes de utilizar el canal.

El resistor convierte la corriente en tensión; V≈I_foto*R solamente es una aproximación en la región adecuada. La unión del fotodiodo introduce no linealidad al aumentar la tensión: no se garantiza señal proporcional a iluminación ni uso de toda la escala ADC. Una R mayor también puede aumentar ruido y tiempo de establecimiento. La capacitancia del sensor, cable y ADC influye en la respuesta.

Antes de conectar a STM32: identificar A/K reales; medir con la resistencia colocada desde oscuridad a la iluminación máxima esperada; confirmar que la tensión permanece entre GND y VREF+ (nominal 3.3 V), también en arranque/apagado; determinar impedancia de fuente, tiempo de muestreo y precisión aceptable. No se añaden valores de protección o filtro sin datos. La conexión directa no protege contra una polaridad invertida o perturbaciones del cable.

Fuente técnica: [Hamamatsu, Si photodiodes](https://www.hamamatsu.com/content/dam/hamamatsu-photonics/sites/documents/99_SALES_LIBRARY/ssd/si_pd_kspd9001e.pdf). La topología no supone validado el sensor desconocido ni permite fijar R sin pruebas.
