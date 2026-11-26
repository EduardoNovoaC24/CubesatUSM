#include "adsb_receiver.h"
#include "task.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef uart = {USART1};
static uint32_t tick = UINT32_MAX - 2000U, reference;
static uint8_t *receive_byte;
static unsigned stage, ack_count, timeout_logs, recovery_count;
static unsigned pc_count, stress_batches;
static bool reject_next;
static char pc_frames[7][ADSB_LINE_MAX + 1U];
static jmp_buf done;
static const char *frame = "$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810\n";
static const char *canonical = "$ADSB,1,E8032E,SKU176,12575,253,32,-33.472000,-70.810000\r\n";

uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *u, uint8_t *data, uint16_t length)
{
    assert(u == &uart && length == 1);
    receive_byte = data;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{
    assert(u == &uart);
    ++recovery_count;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u, const uint8_t *data, uint16_t length, uint32_t timeout)
{
    assert(u == &uart && timeout == 20 && length == 10);
    assert(!memcmp(data, "ACK_L476\r\n", length));
    ++ack_count;
    return HAL_OK;
}
QueueHandle_t xQueueCreateStatic(unsigned length, unsigned size, uint8_t *storage, StaticQueue_t *q)
{
    *q = (StaticQueue_t){.storage = storage, .length = length, .size = size};
    return q;
}
BaseType_t xQueueSendFromISR(QueueHandle_t q, const void *data, BaseType_t *wake)
{
    (void)wake;
    if (q->count == q->length) return 0;
    memcpy(q->storage + ((q->head + q->count) % q->length) * q->size, data, q->size);
    ++q->count;
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *data, TickType_t timeout)
{
    if (!q->count) { tick += timeout; return 0; }
    memcpy(data, q->storage + q->head * q->size, q->size);
    q->head = (q->head + 1U) % q->length;
    --q->count;
    return pdPASS;
}
BaseType_t xQueueReset(QueueHandle_t q) { q->count = q->head = 0; return pdPASS; }
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *data)
{
    assert(q->length == 1);
    memcpy(q->storage, data, q->size);
    q->head = 0; q->count = 1;
    return pdPASS;
}
StaticTask_t *xTaskCreateStatic(void (*entry)(void *), const char *name, uint32_t depth,
                               void *arg, unsigned priority, StackType_t *stack, StaticTask_t *control)
{
    (void)arg; (void)stack;
    assert(entry == Task_ADSB_Receiver && !strcmp(name, "ADSB") && depth == 768 && priority == 24);
    return control;
}
static void inject(const char *s)
{
    assert(receive_byte);
    while (*s) {
        *receive_byte = (uint8_t)*s++;
        HAL_UART_RxCpltCallback(&uart);
    }
}
static void logger(const char *s)
{
    if (strstr(s, "Timeout de comunicacion")) ++timeout_logs;
}
static bool pc_sink(const char *line)
{
    assert(strncmp(line, "$ADSB,1,", 8) == 0);
    size_t length = strlen(line);
    assert(length >= 2 && !strcmp(line + length - 2, "\r\n"));
    if (reject_next) { reject_next = false; return false; }
    if (pc_count < 7) snprintf(pc_frames[pc_count], sizeof(pc_frames[0]), "%s", line);
    ++pc_count;
    return true;
}
void vTaskDelay(TickType_t delay)
{
    tick += delay;
    ADSB_Status_t s, event;
    ADSB_GetStatus(&s);
    switch (stage) {
    case 0:
        inject("HOLA_L476\r\n"); ++stage; break;
    case 1:
        assert(s.handshakes == 1 && s.connected && ack_count == 1);
        inject(frame); inject("bad\n"); ++stage; break;
    case 2:
        assert(s.valid_messages == 1 && s.invalid_messages == 1 && s.aircraft.data_valid);
        assert(xQueueReceive(ADSB_UpdateQueue(), &event, 0) == pdPASS);
        assert(event.aircraft.data_valid);
        reference = tick; ++stage; break;
    case 3:
        if ((uint32_t)(tick - reference) < 5100U) break;
        assert(!s.connected && !s.aircraft.data_valid && timeout_logs == ADSB_DEBUG_ENABLED);
        assert(xQueueReceive(ADSB_UpdateQueue(), &event, 0) == pdPASS && !event.connected);
        inject("HOLA_L476\n"); ++stage; break;
    case 4:
        assert(s.connected && s.handshakes == 2 && !s.aircraft.data_valid);
        for (unsigned i = 0; i < 600; ++i) inject("X");
        ++stage; break;
    case 5:
        assert(s.dropped_bytes == 88);
        inject("\n"); inject(frame); ++stage; break;
    case 6:
        assert(s.valid_messages == 2 && s.aircraft.data_valid);
        HAL_UART_ErrorCallback(&uart); ++stage; break;
    case 7:
        assert(s.uart_errors == 1 && recovery_count == 2);
        inject("\n"); inject(frame); ++stage; break;
    case 8:
        assert(s.valid_messages == 3);
        inject("$ADSB,1,E8032E,"); reference = tick; ++stage; break;
    case 9:
        if ((uint32_t)(tick - reference) < 1100U) break;
        inject("SKU176,12575,253,32,-33.472,-70.810\n"); inject(frame); ++stage; break;
    case 10:
        assert(s.valid_messages == 4 && s.aircraft.data_valid && s.invalid_messages == 4);
        assert(pc_count == 4 && s.pc_frames_queued == 4 && s.pc_frames_dropped == 0);
        for (unsigned i = 0; i < 4; ++i) assert(!strcmp(pc_frames[i], canonical));
        inject("$ADSB,1,E8032E,SKU176,12575,253,32,,\n");
        inject("$ADSB,1,ABCDEF,OTHER,1000,120,270,1.234567,12.345678\n");
        inject(frame); ++stage; break;
    case 11:
        assert(s.valid_messages == 7 && pc_count == 7);
        assert(!strcmp(pc_frames[4], "$ADSB,1,E8032E,SKU176,12575,253,32,,\r\n"));
        assert(!strcmp(pc_frames[5], "$ADSB,1,ABCDEF,OTHER,1000,120,270,1.234567,12.345678\r\n"));
        assert(!strcmp(pc_frames[6], canonical));
        assert(!strcmp(s.aircraft.icao, "E8032E"));
        reject_next = true; inject(frame); ++stage; break;
    case 12:
        assert(s.valid_messages == 8 && s.pc_frames_queued == 7 && s.pc_frames_dropped == 1);
        ++stage; break;
    case 13:
        assert(s.valid_messages == 8 + stress_batches * 4);
        assert(s.pc_frames_queued == 7 + stress_batches * 4 && s.pc_frames_dropped == 1);
        assert(s.dropped_bytes == 88 && s.invalid_messages == 4);
        if (stress_batches == 250) longjmp(done, 1);
        for (unsigned i = 0; i < 4; ++i) inject(frame);
        ++stress_batches; break;
    }
}
int main(void)
{
    assert(!ADSB_ReceiverInit(NULL, logger, pc_sink));
    assert(!ADSB_ReceiverInit(&uart, logger, NULL));
    assert(ADSB_ReceiverInit(&uart, logger, pc_sink));
    assert(!ADSB_ReceiverInit(&uart, logger, pc_sink));
    if (setjmp(done) == 0) Task_ADSB_Receiver(NULL);
    puts("ADSB receiver: handshake, timeout/wrap, overflow, recovery, PC frames, absent position, multiple aircraft, 1000-frame stream, backpressure OK");
}
