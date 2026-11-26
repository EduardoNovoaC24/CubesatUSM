#ifndef SX126X_HAL_STM32_H
#define SX126X_HAL_STM32_H

#include "main.h"

typedef struct
{
    SPI_HandleTypeDef *hspi;
} sx126x_stm32_ctx_t;

/* Único radio en el proyecto: instancia global que se pasa como "context"
 * a toda la API sx126x_*(). */
extern sx126x_stm32_ctx_t sx126x_ctx;

void sx126x_hal_stm32_attach(SPI_HandleTypeDef *hspi);

#endif /* SX126X_HAL_STM32_H */
