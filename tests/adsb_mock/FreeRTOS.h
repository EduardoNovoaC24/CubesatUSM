#ifndef ADSB_MOCK_FREERTOS_H
#define ADSB_MOCK_FREERTOS_H
#include <stdint.h>
#include <stddef.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct { unsigned unused; } StaticTask_t;
typedef struct {
    uint8_t *storage;
    unsigned length, size, head, count;
} StaticQueue_t;
#define pdPASS 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portYIELD_FROM_ISR(wake) ((void)(wake))
#endif
