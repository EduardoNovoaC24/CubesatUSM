#include "FreeRTOS.h"
typedef StaticQueue_t *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(unsigned, unsigned, uint8_t *, StaticQueue_t *);
BaseType_t xQueueSendFromISR(QueueHandle_t, const void *, BaseType_t *);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
BaseType_t xQueueReset(QueueHandle_t);
BaseType_t xQueueOverwrite(QueueHandle_t, const void *);
