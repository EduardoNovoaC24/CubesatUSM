/* Host fault injection tests. These do not establish that hardware works. */
#include "sx126x_hal_stm32.h"
#include "sx126x_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
GPIO_TypeDef port_a, port_b, port_c;
UART_HandleTypeDef huart2;
static uint32_t tick, reset_low_tick;
static unsigned transfers, selects, reset_edges;
static int fail_transfer, busy_after_transfer, release_on_reset;
uint32_t HAL_GetTick(void) { return tick++; }
void HAL_Delay(uint32_t ms) { tick += ms; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin) {
    return (p->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState value) {
    if (value) { p->ODR |= pin; p->IDR |= pin; }
    else { p->ODR &= ~pin; p->IDR &= ~pin; }
    if (p == SX1262_NSS_GPIO_Port && pin == SX1262_NSS_Pin && !value) { selects++; }
    if (p == &port_c) {
        reset_edges++;
        if (!value) { reset_low_tick = tick; }
        else {
            assert(tick - reset_low_tick >= 20U);
            if (release_on_reset) { port_b.IDR &= ~SX1262_BUSY_Pin; }
        }
    }
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *u, uint8_t *s, uint16_t n, uint32_t t) {
    (void)u; (void)s; (void)n; (void)t; return HAL_OK;
}
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *s) { (void)s; return 1; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *s, uint8_t *tx, uint8_t *rx, uint16_t n, uint32_t t) {
    (void)s; (void)t;
    assert(!(SX1262_NSS_GPIO_Port->ODR & SX1262_NSS_Pin)); /* NSS stays selected across both phases. */
    assert(tx != rx);
    transfers++;
    memset(rx, transfers == 1 ? 0xEE : 0x24, n);
    if (busy_after_transfer) { port_b.IDR |= SX1262_BUSY_Pin; }
    return fail_transfer ? HAL_ERROR : HAL_OK;
}
static void fresh(void) {
    tick = transfers = selects = reset_edges = 0;
    fail_transfer = busy_after_transfer = release_on_reset = 0;
    SX1262_NSS_GPIO_Port->ODR = SX1262_NSS_Pin; port_b.IDR = 0;
}
int main(void) {
    SPI_HandleTypeDef spi = {0};
    sx126x_hal_stm32_attach(&spi);
    uint8_t cmd[] = {0xC0}, out = 0xAB, data = 0x34;
    fresh(); port_b.IDR = SX1262_BUSY_Pin;
    assert(sx126x_hal_read(&sx126x_ctx, cmd, 1, &out, 1) == SX126X_HAL_STATUS_ERROR);
    assert(transfers == 0 && selects == 0 && out == 0 && tick < 1100);
    assert(sx126x_hal_write(&sx126x_ctx, cmd, 1, &data, 1) == SX126X_HAL_STATUS_ERROR);
    assert(transfers == 0 && selects == 0);
    fresh(); port_b.IDR = SX1262_BUSY_Pin; release_on_reset = 1;
    assert(sx126x_hal_reset(&sx126x_ctx) == SX126X_HAL_STATUS_OK);
    assert(reset_edges == 2 && transfers == 0 && (SX1262_NSS_GPIO_Port->ODR & SX1262_NSS_Pin));
    fresh(); port_b.IDR = SX1262_BUSY_Pin;
    assert(sx126x_hal_reset(&sx126x_ctx) == SX126X_HAL_STATUS_ERROR);
    assert(reset_edges == 2 && transfers == 0 && tick < 1200);
    fresh();
    assert(sx126x_hal_read(&sx126x_ctx, cmd, 1, &out, 1) == SX126X_HAL_STATUS_OK);
    assert(out == 0x24 && transfers == 2 && selects == 1 && (SX1262_NSS_GPIO_Port->ODR & SX1262_NSS_Pin));
    fresh(); fail_transfer = 1;
    assert(sx126x_hal_read(&sx126x_ctx, cmd, 1, &out, 1) == SX126X_HAL_STATUS_ERROR);
    assert(transfers == 1 && out == 0 && (SX1262_NSS_GPIO_Port->ODR & SX1262_NSS_Pin));
    fresh(); fail_transfer = 1;
    assert(sx126x_hal_write(&sx126x_ctx, cmd, 1, &data, 1) == SX126X_HAL_STATUS_ERROR);
    assert(transfers == 1 && (SX1262_NSS_GPIO_Port->ODR & SX1262_NSS_Pin));
    fresh(); busy_after_transfer = 1;
    assert(sx126x_hal_read(&sx126x_ctx, cmd, 1, &out, 1) == SX126X_HAL_STATUS_ERROR);
    assert(out == 0 && (SX1262_NSS_GPIO_Port->ODR & SX1262_NSS_Pin) && tick < 1100);
    puts("HAL fault injection checks passed (simulation only)");
}
