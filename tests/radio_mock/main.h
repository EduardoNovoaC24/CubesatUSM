#ifndef RADIO_STATE_MOCK_MAIN_H
#define RADIO_STATE_MOCK_MAIN_H
#include <stdbool.h>
#include <stdint.h>
#define SX1262_DIAGNOSTIC_MODE 1
#define RADIO_QUATERNION_MODE 1
typedef struct { uint32_t IDR, ODR, MODER, PUPDR; } GPIO_TypeDef;
typedef struct { struct { uint32_t DataSize; } Init; } SPI_HandleTypeDef;
typedef struct { int unused; } UART_HandleTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
#define SPI_DATASIZE_8BIT 8U
extern GPIO_TypeDef test_gpio_a,test_gpio_b,test_gpio_c;
#define GPIOA (&test_gpio_a)
#define GPIOB (&test_gpio_b)
#define GPIOC (&test_gpio_c)
#define SX1262_NSS_GPIO_Port GPIOA
#define SX1262_NSS_Pin 16U
#define SX1262_BUSY_GPIO_Port GPIOB
#define SX1262_BUSY_Pin 1U
#define SX1262_DIO1_GPIO_Port GPIOB
#define SX1262_DIO1_Pin 2U
#define SX1262_NRESET_GPIO_Port GPIOC
#define SX1262_NRESET_Pin 1U
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *,uint16_t,GPIO_PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *,uint16_t);
int HAL_UART_Transmit(UART_HandleTypeDef *,uint8_t *,uint16_t,uint32_t);
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *);
#endif
