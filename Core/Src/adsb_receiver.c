#include "adsb_receiver.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define RX_CAPACITY 512U
#define ADSB_STACK_WORDS 768U
static UART_HandleTypeDef *receiver_uart;
static void (*log_message)(const char *);
static bool (*forward_frame)(const char *);
static StaticQueue_t rx_control, update_control;
static uint8_t rx_storage[RX_CAPACITY], update_storage[sizeof(ADSB_Status_t)];
static QueueHandle_t rx_queue, update_queue;
static StaticTask_t task_control;
static StackType_t task_stack[ADSB_STACK_WORDS];
static uint8_t rx_byte;
static volatile bool rx_restart, rx_overflow;
static volatile uint32_t uart_errors, dropped_bytes;
static ADSB_Status_t shared_status;

static void debug(const char *message)
{
#if ADSB_DEBUG_ENABLED
    if (log_message) log_message(message);
#else
    (void)message;
#endif
}

static void publish(ADSB_Status_t *status, bool event)
{
    taskENTER_CRITICAL();
    status->uart_errors = uart_errors;
    status->dropped_bytes = dropped_bytes;
    shared_status = *status;
    taskEXIT_CRITICAL();
    if (event) (void)xQueueOverwrite(update_queue, status);
}

void ADSB_GetStatus(ADSB_Status_t *status)
{
    if (!status) return;
    taskENTER_CRITICAL();
    *status = shared_status;
    taskEXIT_CRITICAL();
}

QueueHandle_t ADSB_UpdateQueue(void) { return update_queue; }

bool ADSB_ReceiverInit(UART_HandleTypeDef *uart, void (*logger)(const char *),
                      bool (*data_sink)(const char *))
{
    if (!uart || uart->Instance != USART1 || !data_sink || receiver_uart) return false;
    receiver_uart = uart;
    log_message = logger;
    forward_frame = data_sink;
    rx_queue = xQueueCreateStatic(RX_CAPACITY, 1, rx_storage, &rx_control);
    update_queue = xQueueCreateStatic(1, sizeof(ADSB_Status_t), update_storage, &update_control);
    return xTaskCreateStatic(Task_ADSB_Receiver, "ADSB", ADSB_STACK_WORDS, NULL,
                             24U, task_stack, &task_control) != NULL;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart != receiver_uart) return;
    BaseType_t wake = pdFALSE;
    if (xQueueSendFromISR(rx_queue, &rx_byte, &wake) != pdPASS) {
        ++dropped_bytes;
        rx_overflow = true;
    }
    if (HAL_UART_Receive_IT(uart, &rx_byte, 1) != HAL_OK) rx_restart = true;
    portYIELD_FROM_ISR(wake);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart != receiver_uart) return;
    ++uart_errors;
    rx_restart = true; /* recovery and framing reset in task context */
}

void Task_ADSB_Receiver(void *argument)
{
    (void)argument;
    ADSB_Decoder_t decoder = {0};
    ADSB_Status_t status = {0};
    uint32_t started = HAL_GetTick(), last_byte_ms = started;
    uint32_t last_log_ms = started - 1000U;
    bool timeout_reported = false, receiving_started = false, discard_counted = false;
    rx_restart = true;
    debug("[ADSB] USART1 inicializado\r\n[ADSB] Esperando mensajes...\r\n");
    for (;;) {
        /* Priority 5 USART1 is masked here. Never splice bytes across loss. */
        taskENTER_CRITICAL();
        bool damaged = rx_restart || rx_overflow;
        if (damaged) {
            (void)xQueueReset(rx_queue);
            rx_overflow = false;
        }
        if (rx_restart) {
            (void)HAL_UART_AbortReceive(receiver_uart);
            rx_restart = HAL_UART_Receive_IT(receiver_uart, &rx_byte, 1) != HAL_OK;
        }
        taskEXIT_CRITICAL();
        if (damaged && receiving_started) {
            if (!decoder.discard) ++status.invalid_messages;
            ADSB_Discard(&decoder);
            discard_counted = true;
        }
        receiving_started = true;
        uint8_t byte;
        for (unsigned budget = 0; budget < 256U; ++budget) {
            if (xQueueReceive(rx_queue, &byte, budget == 0U ? pdMS_TO_TICKS(20) : 0) != pdPASS) break;
            /* A loss while draining must be handled before parsing more bytes. */
            if (rx_restart || rx_overflow) break;
            uint32_t now = HAL_GetTick();
            last_byte_ms = now;
            ADSB_Aircraft_t aircraft;
            ADSB_Result_t result = ADSB_Feed(&decoder, byte, &aircraft);
            if (result == ADSB_NONE) continue;
            bool can_log = (uint32_t)(now - last_log_ms) >= 1000U;
            if (result == ADSB_INVALID) {
                if (!discard_counted) ++status.invalid_messages;
                discard_counted = false;
                if (can_log) { debug("[ADSB] Mensaje invalido\r\n"); last_log_ms = now; }
                continue;
            }
            if (!status.connected && can_log) {
                debug("[ADSB] Conexion UART establecida\r\n");
            }
            status.connected = true;
            status.last_contact_ms = now;
            timeout_reported = false;
            if (result == ADSB_HELLO) {
                ++status.handshakes;
                static const uint8_t ack[] = "ACK_L476\r\n";
                if (HAL_UART_Transmit(receiver_uart, ack, sizeof(ack) - 1U, 20U) != HAL_OK)
                    ++status.ack_errors;
                if (can_log) debug("[ADSB] UART1 conectado\r\n[ADSB] Mensaje recibido: HOLA_L476\r\n");
            } else {
                aircraft.last_update_ms = now;
                aircraft.messages_received = ++status.valid_messages;
                status.aircraft = aircraft;
                /* Machine data is never subject to the diagnostic log limiter.
                 * The shared UART2 queue copies this stack buffer by value. */
                char frame[ADSB_LINE_MAX + 1U];
                if (ADSB_FormatUART(&aircraft, frame, sizeof(frame)) && forward_frame(frame))
                    ++status.pc_frames_queued;
                else ++status.pc_frames_dropped;
                if (can_log) {
                    char message[100];
                    if (aircraft.valid_fields & ADSB_VALID_ALTITUDE)
                        snprintf(message, sizeof(message), "[ADSB] ICAO=%s FLIGHT=%s ALT=%ld\r\n",
                                 aircraft.icao, aircraft.callsign, (long)aircraft.altitude_ft);
                    else snprintf(message, sizeof(message), "[ADSB] ICAO=%s FLIGHT=%s ALT=NA\r\n",
                                  aircraft.icao, aircraft.callsign);
                    debug(message);
                }
            }
            if (can_log) last_log_ms = now;
            publish(&status, true);
        }
        uint32_t now = HAL_GetTick();
        if (decoder.used && (uint32_t)(now - last_byte_ms) >= 1000U) {
            ADSB_Discard(&decoder);
            ++status.invalid_messages;
            discard_counted = true;
        }
        bool event = false;
        if (status.aircraft.data_valid && (uint32_t)(now - status.aircraft.last_update_ms) >= ADSB_TIMEOUT_MS) {
            status.aircraft.data_valid = false;
            event = true;
        }
        if (!timeout_reported && (uint32_t)(now - (status.connected ? status.last_contact_ms : started)) >= ADSB_TIMEOUT_MS) {
            status.connected = false;
            timeout_reported = true;
            debug("[ADSB] Timeout de comunicacion\r\n");
            event = true;
        }
        publish(&status, event);
        /* Bound CPU usage under continuous traffic; sensors/safety run above us. */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
