#include "FreeRTOS.h"
StaticTask_t *xTaskCreateStatic(void (*)(void *), const char *, uint32_t, void *,
                              unsigned, StackType_t *, StaticTask_t *);
void vTaskDelay(TickType_t);
