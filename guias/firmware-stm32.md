# Firmware STM32

OBC para STM32L476RG. Integra FreeRTOS, el MPU9250, Fusion AHRS, el control ADCS, SX1262 P2P, ADS-B por USART1 y telemetría EPS por USART3.

## Compilar

1. Importa la carpeta raíz como proyecto existente de STM32CubeIDE.
2. Revisa la configuración y el pinout de tu placa en `Core/Inc/main.h`, el archivo `.ioc` y [los diseños de hardware](../hardware/).
3. Compila la configuración `Debug` y programa el STM32 mediante ST-LINK.
4. Para conectar el enlace de radio, utiliza la [guía Pico P2P](../pico_firmware/README.md). Para ADS-B, ve [ADSB_ORANGEPI.md](../ADSB_ORANGEPI.md).

En un checkout de esta rama, las rutas principales son `Core/`, `Drivers/`, `Middlewares/` y `tests/`.

## Sesión LoRaWAN local

Si el equipo necesita el modo LoRaWAN ABP, copia `Core/Inc/lorawan_session.example.h` como `Core/Inc/lorawan_session.local.h`, incorpora el DevAddr y las claves de sesión del equipo, y define `CUBESAT_LORAWAN_LOCAL_CONFIG` en los símbolos del compilador. El archivo local está ignorado por Git. La radio LoRa P2P puede utilizarse sin esa configuración.
