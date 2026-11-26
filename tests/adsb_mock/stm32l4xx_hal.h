#include <stdint.h>
typedef struct { void *Instance; } UART_HandleTypeDef;
typedef struct { unsigned unused; } TIM_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
#define USART1 ((void *)1)
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
