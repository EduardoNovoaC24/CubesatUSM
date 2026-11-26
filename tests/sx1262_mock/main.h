#ifndef SX1262_MOCK_MAIN_H
#define SX1262_MOCK_MAIN_H
#include <stdint.h>
typedef struct { uint32_t IDR, ODR, MODER, PUPDR; } GPIO_TypeDef;
typedef struct { int unused; } SPI_HandleTypeDef;
typedef struct { int unused; } UART_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
extern GPIO_TypeDef port_a, port_b, port_c;
#define SX1262_NSS_GPIO_Port (&port_a)
#define SX1262_NSS_Pin 16U
#define SX1262_BUSY_GPIO_Port (&port_b)
#define SX1262_BUSY_Pin 1U
#define SX1262_DIO1_GPIO_Port (&port_b)
#define SX1262_DIO1_Pin 2U
#define SX1262_NRESET_GPIO_Port (&port_c)
#define SX1262_NRESET_Pin 1U
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *, uint8_t *, uint8_t *, uint16_t, uint32_t);
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *);
#endif
