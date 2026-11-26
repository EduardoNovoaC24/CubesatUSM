#include "sx126x_hal_stm32.h"
#include "sx126x_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

extern UART_HandleTypeDef huart2;

sx126x_stm32_ctx_t sx126x_ctx;

static void log_msg(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100);
}

void sx126x_hal_stm32_attach(SPI_HandleTypeDef *hspi)
{
    sx126x_ctx.hspi = hspi;
}

static sx126x_hal_status_t validate_ctx(const sx126x_stm32_ctx_t *ctx, const char *op)
{
    if ((ctx == NULL) || (ctx->hspi == NULL)) {
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "[SXHAL] %s sin SPI context\r\n", op);
        log_msg(dbg);
        return SX126X_HAL_STATUS_ERROR;
    }

    return SX126X_HAL_STATUS_OK;
}

static void log_hal_result(const sx126x_stm32_ctx_t *ctx, const char *op, HAL_StatusTypeDef r1, HAL_StatusTypeDef r2)
{
    if ((r1 != HAL_OK) || (r2 != HAL_OK)) {
        char dbg[96];
        snprintf(dbg, sizeof(dbg), "[SXHAL] %s HAL error r1=%d r2=%d err=0x%08lX\r\n",
                 op, (int)r1, (int)r2, (unsigned long)HAL_SPI_GetError(ctx->hspi));
        log_msg(dbg);
    }
}

/* BUSY debe estar en 0 antes de cualquier transacción SPI. Timeout duro:
 * sin reintentos infinitos, si se atasca se reporta y se devuelve error. */
static sx126x_hal_status_t wait_busy(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (HAL_GPIO_ReadPin(SX1262_BUSY_GPIO_Port, SX1262_BUSY_Pin) == GPIO_PIN_SET) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            char dbg[64];
            snprintf(dbg, sizeof(dbg), "[SXHAL] BUSY timeout tras %lums\r\n",
                     (unsigned long)(HAL_GetTick() - start));
            log_msg(dbg);
            return SX126X_HAL_STATUS_ERROR;
        }
    }
    return SX126X_HAL_STATUS_OK;
}

/* Drain command-phase RX explicitly. Buffers are separate and local; the radio
 * has a single owner (Task_RadioTest, or the pre-scheduler diagnostic). */
static HAL_StatusTypeDef transfer_command(SPI_HandleTypeDef *spi,
                                         const uint8_t *command, uint16_t length)
{
    uint8_t discarded[32];
    while (length != 0U) {
        uint16_t count = length > sizeof(discarded) ? sizeof(discarded) : length;
        HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(spi, (uint8_t *)command,
                                                         discarded, count, 100);
        if (result != HAL_OK) { return result; }
        command += count;
        length -= count;
    }
    return HAL_OK;
}

static void nss_low(void)  { HAL_GPIO_WritePin(SX1262_NSS_GPIO_Port, SX1262_NSS_Pin, GPIO_PIN_RESET); }
static void nss_high(void) { HAL_GPIO_WritePin(SX1262_NSS_GPIO_Port, SX1262_NSS_Pin, GPIO_PIN_SET);   }

sx126x_hal_status_t sx126x_hal_write(const void *context, const uint8_t *command, const uint16_t command_length,
                                      const uint8_t *data, const uint16_t data_length)
{
    const sx126x_stm32_ctx_t *ctx = (const sx126x_stm32_ctx_t *)context;

    if ((command == NULL) || (command_length == 0U) ||
        ((data_length > 0U) && (data == NULL))) { return SX126X_HAL_STATUS_ERROR; }

    if (validate_ctx(ctx, "write") != SX126X_HAL_STATUS_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }

    if (wait_busy(1000) != SX126X_HAL_STATUS_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }
    nss_low();
    HAL_StatusTypeDef r1 = transfer_command(ctx->hspi, command, command_length);
    HAL_StatusTypeDef r2 = HAL_OK;
    if ((r1 == HAL_OK) && (data_length > 0U)) {
        r2 = transfer_command(ctx->hspi, data, data_length);
    }
    nss_high();

    log_hal_result(ctx, "write", r1, r2);
    if ((r1 != HAL_OK) || (r2 != HAL_OK)) { return SX126X_HAL_STATUS_ERROR; }
    if (wait_busy(1000) != SX126X_HAL_STATUS_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }
    return (r1 == HAL_OK && r2 == HAL_OK) ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

sx126x_hal_status_t sx126x_hal_read(const void *context, const uint8_t *command, const uint16_t command_length,
                                     uint8_t *data, const uint16_t data_length)
{
    const sx126x_stm32_ctx_t *ctx = (const sx126x_stm32_ctx_t *)context;

    if ((command == NULL) || (command_length == 0U) ||
        ((data_length > 0U) && (data == NULL))) { return SX126X_HAL_STATUS_ERROR; }
    if (data_length > 0U) { memset(data, 0, data_length); }

    if (validate_ctx(ctx, "read") != SX126X_HAL_STATUS_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }

    if (data_length > 256U) {
        log_msg("[SXHAL] read data_length > dummy buffer\r\n");
        return SX126X_HAL_STATUS_ERROR;
    }

    if (wait_busy(1000) != SX126X_HAL_STATUS_OK) {
        return SX126X_HAL_STATUS_ERROR;
    }
    nss_low();
    HAL_StatusTypeDef r1 = transfer_command(ctx->hspi, command, command_length);
    HAL_StatusTypeDef r2 = HAL_OK;
    if ((r1 == HAL_OK) && (data_length > 0U)) {
        uint8_t dummy_tx[256];
        memset(dummy_tx, 0x00, data_length);
        r2 = HAL_SPI_TransmitReceive(ctx->hspi, dummy_tx, data, data_length, 100);
    }
    nss_high();

    log_hal_result(ctx, "read", r1, r2);
    if ((r1 != HAL_OK) || (r2 != HAL_OK) ||
        (wait_busy(1000) != SX126X_HAL_STATUS_OK)) {
        if (data_length > 0U) { memset(data, 0, data_length); }
        return SX126X_HAL_STATUS_ERROR;
    }
    return (r1 == HAL_OK && r2 == HAL_OK) ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
}

sx126x_hal_status_t sx126x_hal_reset(const void *context)
{
    (void)context;

    /* Recovery reset never waits for BUSY first. Keep the SPI slave deselected. */
    nss_high();
    HAL_GPIO_WritePin(SX1262_NRESET_GPIO_Port, SX1262_NRESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(SX1262_NRESET_GPIO_Port, SX1262_NRESET_Pin, GPIO_PIN_SET);
#if SX1262_DIAGNOSTIC_MODE
    uint32_t start = HAL_GetTick();
    uint32_t next_log = 0U;
    int previous = -1;
    bool saw_low = false;
    do {
        uint32_t elapsed = HAL_GetTick() - start;
        int busy = HAL_GPIO_ReadPin(SX1262_BUSY_GPIO_Port, SX1262_BUSY_Pin);
        if (busy == GPIO_PIN_RESET) { saw_low = true; }
        if ((busy != previous) || (elapsed >= next_log)) {
            char msg[144];
            snprintf(msg, sizeof(msg),
                     "[SXDIAG] RESET: t=%lums PB0/BUSY=%d PB1/DIO1=%u PC0/RESET=%u\r\n",
                     (unsigned long)elapsed, busy,
                     (unsigned)HAL_GPIO_ReadPin(SX1262_DIO1_GPIO_Port, SX1262_DIO1_Pin),
                     (unsigned)HAL_GPIO_ReadPin(SX1262_NRESET_GPIO_Port, SX1262_NRESET_Pin));
            log_msg(msg);
            previous = busy;
            next_log = elapsed + 100U;
        }
        if (elapsed >= 1000U) { break; }
        HAL_Delay(1U);
    } while (1);
    return (saw_low && HAL_GPIO_ReadPin(SX1262_BUSY_GPIO_Port, SX1262_BUSY_Pin) == GPIO_PIN_RESET)
               ? SX126X_HAL_STATUS_OK : SX126X_HAL_STATUS_ERROR;
#else
    HAL_Delay(20);
    return wait_busy(1000);
#endif
}

sx126x_hal_status_t sx126x_hal_wakeup(const void *context)
{
    (void)context;

    nss_low();
    HAL_Delay(1);
    nss_high();

    return wait_busy(1000);
}
