#ifndef ADSB_RECEIVER_H
#define ADSB_RECEIVER_H
#include "adsb_protocol.h"
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"

#ifndef ADSB_DEBUG_ENABLED
#define ADSB_DEBUG_ENABLED 1
#endif
#define ADSB_TIMEOUT_MS 5000U

typedef struct {
    ADSB_Aircraft_t aircraft;
    uint32_t valid_messages;
    uint32_t invalid_messages;
    uint32_t handshakes;
    uint32_t uart_errors;
    uint32_t dropped_bytes;
    uint32_t ack_errors;
    uint32_t pc_frames_queued;
    uint32_t pc_frames_dropped;
    uint32_t last_contact_ms;
    bool connected;
} ADSB_Status_t;

/* Call once after osKernelInitialize and before osKernelStart. Callbacks must
 * copy/enqueue before returning without waiting. data_sink returns false if
 * full. USART1 is exclusively owned by this module; logger is optional. */
bool ADSB_ReceiverInit(UART_HandleTypeDef *uart, void (*logger)(const char *),
                      bool (*data_sink)(const char *));
void Task_ADSB_Receiver(void *argument);
void ADSB_GetStatus(ADSB_Status_t *status);
/* One consumer, latest state only: xQueueReceive(...). Do not send/reset/delete.
 * Events include new data and timeout transitions. Other readers use GetStatus. */
QueueHandle_t ADSB_UpdateQueue(void);
#endif
