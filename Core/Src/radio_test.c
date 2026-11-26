#include "radio_test.h"
#include "p2p_protocol.h"
#include "sx126x.h"
#include "sx126x_hal_stm32.h"
#include "sx126x_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;
static bool diagnostic_tx_ready;
static bool diagnostic_radio_ready;
static bool lora_configured;
static bool radio_configure_lora(void);
static void (*radio_log_sink)(const char *);

void radio_set_log_sink(void (*sink)(const char *))
{
    radio_log_sink = sink;
}

static void radio_delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) { HAL_Delay(ms); }
    else { vTaskDelay(pdMS_TO_TICKS(ms)); }
}

static void log_msg(const char *msg)
{
    if (radio_log_sink != NULL) { radio_log_sink(msg); }
    else { HAL_UART_Transmit(&huart2, (uint8_t *)msg, strlen(msg), 100); }
}

static void radio_pin_probe(const char *tag)
{
    char msg[256];
    snprintf(msg, sizeof(msg),
             "[SXDIAG] GPIO: %s NSS PA4 ODR=%u IDR=%u mode=%lu pull=%lu; RESET PC0 ODR=%u IDR=%u mode=%lu pull=%lu\r\n",
             tag, (unsigned)((GPIOA->ODR >> 4) & 1U), (unsigned)((GPIOA->IDR >> 4) & 1U),
             (unsigned long)((GPIOA->MODER >> 8) & 3U), (unsigned long)((GPIOA->PUPDR >> 8) & 3U),
             (unsigned)(GPIOC->ODR & 1U), (unsigned)(GPIOC->IDR & 1U),
             (unsigned long)(GPIOC->MODER & 3U), (unsigned long)(GPIOC->PUPDR & 3U));
    log_msg(msg);
    snprintf(msg, sizeof(msg),
             "[SXDIAG] GPIO: GPIOB->IDR=0x%08lX MODER=0x%08lX PUPDR=0x%08lX; GPIOC->IDR=0x%08lX ODR=0x%08lX\r\n",
             (unsigned long)GPIOB->IDR, (unsigned long)GPIOB->MODER,
             (unsigned long)GPIOB->PUPDR, (unsigned long)GPIOC->IDR, (unsigned long)GPIOC->ODR);
    log_msg(msg);
    snprintf(msg, sizeof(msg),
             "[SXDIAG] GPIO: PB0/BUSY=%u mode=%lu pull=%lu; PB1/DIO1=%u mode=%lu pull=%lu (mode 0=input,1=output,2=AF; pull 0=none)\r\n",
             (unsigned)((GPIOB->IDR >> 0) & 1U), (unsigned long)((GPIOB->MODER >> 0) & 3U),
             (unsigned long)((GPIOB->PUPDR >> 0) & 3U), (unsigned)((GPIOB->IDR >> 1) & 1U),
             (unsigned long)((GPIOB->MODER >> 2) & 3U), (unsigned long)((GPIOB->PUPDR >> 2) & 3U));
    log_msg(msg);
}

/* Same C0 + dummy-byte transaction as sx126x_get_status(), retaining all bits. */
static bool radio_status_probe(const char *phase, unsigned sample, bool *transport_ok, bool *standby_rc)
{
    const uint8_t command = 0xC0;
    uint8_t raw = 0;
    sx126x_hal_status_t result = sx126x_hal_read(&sx126x_ctx, &command, 1, &raw, 1);
    *transport_ok = (result == SX126X_HAL_STATUS_OK);
    unsigned mode = (raw & SX126X_CHIP_MODES_MASK) >> SX126X_CHIP_MODES_POS;
    if (standby_rc != NULL) {
        *standby_rc = *transport_ok && mode == SX126X_CHIP_MODE_STBY_RC;
    }
    unsigned cmd = (raw & SX126X_CMD_STATUS_MASK) >> SX126X_CMD_STATUS_POS;
    char msg[192];
    snprintf(msg, sizeof(msg),
             "[SXDIAG] STATUS: phase=%s sample=%u hal=%d spi_error=0x%08lX raw=0x%02X chip_mode=%u cmd_status=%u valid_rx=%u\r\n",
             phase, sample, (int)result,
             (unsigned long)HAL_SPI_GetError(sx126x_ctx.hspi), raw, mode, cmd,
             *transport_ok ? 1U : 0U);
    log_msg(msg);
    return *transport_ok && mode == SX126X_CHIP_MODE_STBY_RC &&
           (cmd == SX126X_CMD_STATUS_RFU || cmd == SX126X_CMD_STATUS_DATA_AVAILABLE ||
            cmd == SX126X_CMD_STATUS_CMD_TX_DONE);
}

static bool radio_status_rc(void)
{
    bool transport_ok;
    return radio_status_probe("check_rc", 1U, &transport_ok, NULL);
}

#if SX1262_DIAGNOSTIC_MODE
static bool radio_spi_status_diagnostic(void)
{
    /* Pre-command error states are evidence, never a PASS. A safe standby RC
     * command is attempted only if all three SPI transfers completed. */
    bool transfers_ok = true;
    for (unsigned i = 1; i <= 3U; i++) {
        bool transport_ok;
        (void)radio_status_probe("before_standby", i, &transport_ok, NULL);
        transfers_ok = transfers_ok && transport_ok;
        HAL_Delay(10U); /* This diagnostic runs before the scheduler. */
    }
    if (!transfers_ok) {
        log_msg("[SXDIAG] STANDBY_RC: skipped; GetStatus transport failure\r\n");
        return false;
    }
    sx126x_status_t result = sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_RC);
    char msg[128];
    snprintf(msg, sizeof(msg), "[SXDIAG] STANDBY_RC: driver=%d spi_error=0x%08lX\r\n",
             (int)result, (unsigned long)HAL_SPI_GetError(sx126x_ctx.hspi));
    log_msg(msg);
    if (result != SX126X_STATUS_OK) { return false; }
    bool statuses_ok = true;
    for (unsigned i = 1; i <= 3U; i++) {
        bool transport_ok;
        bool standby_rc = false;
        bool valid = radio_status_probe("after_standby", i, &transport_ok, &standby_rc);
        if (!valid && standby_rc) {
            log_msg("[SXDIAG] STATUS: unexpected cmd_status; diagnostic register access only, RF blocked\r\n");
        }
        statuses_ok = statuses_ok && standby_rc;
        HAL_Delay(10U);
    }
    if (!statuses_ok) {
        log_msg("[SXDIAG] SPI: FAIL status after standby; register test skipped\r\n");
    }
    return statuses_ok;
}
#endif

static bool read_register_with_retries(uint16_t addr, uint8_t expected, uint8_t *got)
{
    char dbg[120];

    for (uint32_t attempt = 1; attempt <= RADIO_REGTEST_READ_RETRIES; attempt++) {
        if (sx126x_read_register(&sx126x_ctx, addr, got, 1) != SX126X_STATUS_OK) {
            snprintf(dbg, sizeof(dbg),
                     "[REGTEST] retry=%lu reg=0x%04X read SPI error\r\n",
                     (unsigned long)attempt, addr);
            log_msg(dbg);
        } else if (*got == expected) {
            if (attempt > 1U) {
                snprintf(dbg, sizeof(dbg),
                         "[REGTEST] recovered reg=0x%04X intento=%lu val=0x%02X\r\n",
                         addr, (unsigned long)attempt, *got);
                log_msg(dbg);
            }
            return true;
        } else {
            snprintf(dbg, sizeof(dbg),
                     "[REGTEST] retry=%lu reg=0x%04X esperado=0x%02X leido=0x%02X\r\n",
                     (unsigned long)attempt, addr, expected, *got);
            log_msg(dbg);
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }

    return false;
}

bool radio_test_init(SPI_HandleTypeDef *hspi)
{
    lora_configured = false;
    sx126x_hal_stm32_attach(hspi);
    HAL_GPIO_WritePin(SX1262_NSS_GPIO_Port, SX1262_NSS_Pin, GPIO_PIN_SET);
    radio_pin_probe("before_reset");
    sx126x_status_t result = sx126x_reset(&sx126x_ctx);
    radio_pin_probe("after_reset");
    if (result != SX126X_STATUS_OK) {
        log_msg("[SXDIAG] BUSY: FAIL after RESET; SPI skipped\r\n"
                "[SXDIAG] SUSPECT: check RESET PC0->pin20, BUSY PB0->pin4, common GND and supply at radio\r\n");
        return false;
    }
    log_msg("[SXDIAG] BUSY: LOW; TEST 4 SPI GetStatus\r\n");
#if SX1262_DIAGNOSTIC_MODE
    return radio_spi_status_diagnostic();
#else
    return radio_status_rc() &&
           sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_RC) == SX126X_STATUS_OK;
#endif
}

/* Pre-scheduler clock-only probe. TCXO 1.7 V / 5 ms matches the linked board example. */
static bool radio_clock_diagnostic(void)
{
    char msg[192];
    sx126x_errors_mask_t errors = 0;
    sx126x_status_t result = sx126x_get_device_errors(&sx126x_ctx, &errors);
    snprintf(msg, sizeof(msg), "[SXDIAG] XOSC: errors_before driver=%d mask=0x%04X valid=%u\r\n",
             (int)result, (unsigned)errors, result == SX126X_STATUS_OK ? 1U : 0U);
    log_msg(msg);
    if (result != SX126X_STATUS_OK) { return false; }
    result = sx126x_clear_device_errors(&sx126x_ctx);
    if (result != SX126X_STATUS_OK) {
        log_msg("[SXDIAG] XOSC: clear errors transport FAIL; clock request skipped\r\n");
        return false;
    }
    errors = 0;
    result = sx126x_get_device_errors(&sx126x_ctx, &errors);
    snprintf(msg, sizeof(msg), "[SXDIAG] XOSC: errors_cleared driver=%d mask=0x%04X valid=%u\r\n",
             (int)result, (unsigned)errors, result == SX126X_STATUS_OK ? 1U : 0U);
    log_msg(msg);
    if (result != SX126X_STATUS_OK || errors != 0U) { return false; }
    snprintf(msg, sizeof(msg), "[SXDIAG] TCXO: configuring DIO3=1.7V startup=%u ms; no RF\r\n",
             RADIO_TCXO_STARTUP_MS);
    log_msg(msg);
    sx126x_status_t tcxo_result = sx126x_set_dio3_as_tcxo_ctrl(
        &sx126x_ctx, SX126X_TCXO_CTRL_1_7V, RADIO_TCXO_STARTUP_TICKS);
    snprintf(msg, sizeof(msg), "[SXDIAG] TCXO: driver=%d spi_error=0x%08lX\r\n",
             (int)tcxo_result, (unsigned long)HAL_SPI_GetError(sx126x_ctx.hspi));
    log_msg(msg);
    sx126x_status_t cal_result = SX126X_STATUS_ERROR;
    if (tcxo_result == SX126X_STATUS_OK) {
        cal_result = sx126x_cal(&sx126x_ctx, SX126X_CAL_ALL);
        snprintf(msg, sizeof(msg), "[SXDIAG] TCXO: calibration driver=%d\r\n", (int)cal_result);
        log_msg(msg);
    }
    result = SX126X_STATUS_ERROR;
    if (tcxo_result == SX126X_STATUS_OK && cal_result == SX126X_STATUS_OK) {
        log_msg("[SXDIAG] XOSC: requesting STBY_XOSC with TCXO configured\r\n");
        result = sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_XOSC);
    }
    snprintf(msg, sizeof(msg), "[SXDIAG] XOSC: request driver=%d spi_error=0x%08lX BUSY=%u\r\n",
             (int)result, (unsigned long)HAL_SPI_GetError(sx126x_ctx.hspi),
             (unsigned)HAL_GPIO_ReadPin(SX1262_BUSY_GPIO_Port, SX1262_BUSY_Pin));
    log_msg(msg);
    HAL_Delay(100U);
    sx126x_chip_status_t status = {0};
    sx126x_status_t status_result = sx126x_get_status(&sx126x_ctx, &status);
    errors = 0;
    sx126x_status_t error_result = sx126x_get_device_errors(&sx126x_ctx, &errors);
    snprintf(msg, sizeof(msg),
             "[SXDIAG] XOSC: status_driver=%d chip_mode=%u cmd_status=%u errors_driver=%d mask=0x%04X\r\n",
             (int)status_result, status.chip_mode, status.cmd_status,
             (int)error_result, (unsigned)errors);
    log_msg(msg);
    bool clock_ok = tcxo_result == SX126X_STATUS_OK && cal_result == SX126X_STATUS_OK &&
                    result == SX126X_STATUS_OK && status_result == SX126X_STATUS_OK &&
                    error_result == SX126X_STATUS_OK && status.chip_mode == SX126X_CHIP_MODE_STBY_XOSC &&
                    errors == 0U;
    log_msg(clock_ok ? "[SXDIAG] XOSC: clock mode observed; RF untested\r\n" :
                       "[SXDIAG] XOSC: not verified; check clock type/TCXO voltage before next test\r\n");
    if (clock_ok && (status.cmd_status == SX126X_CMD_STATUS_RFU ||
                     status.cmd_status == SX126X_CMD_STATUS_DATA_AVAILABLE ||
                     status.cmd_status == SX126X_CMD_STATUS_CMD_TX_DONE)) {
        return true; /* Keep TCXO/XOSC configured for the controlled TX probe. */
    }
    /* Reset recovery also works when the clock request leaves BUSY high. */
    sx126x_status_t reset_result = sx126x_reset(&sx126x_ctx);
    bool transport_ok;
    bool rc_mode = false;
    (void)radio_status_probe("clock_recovery", 1U, &transport_ok, &rc_mode);
    snprintf(msg, sizeof(msg), "[SXDIAG] XOSC: recovery reset_driver=%d rc_mode=%u\r\n",
             (int)reset_result, rc_mode ? 1U : 0U);
    log_msg(msg);
    return clock_ok && reset_result == SX126X_STATUS_OK && rc_mode;
}

bool radio_diagnostic_run(SPI_HandleTypeDef *hspi)
{
    diagnostic_radio_ready = false;
    log_msg("[SXDIAG] TEST: BIDIRECTIONAL_P2P_V9; supply voltage is not measured by firmware\r\n"
            "[SXDIAG] TEST 1 GPIO; TEST 2 RESET (1000ms observation); TEST 3 BUSY\r\n");
    /* Validate the actual modes before driving or attempting SPI. */
    if ((GPIOB->MODER & ((3U << 0) | (3U << 2))) != 0U ||
        (GPIOB->PUPDR & ((3U << 0) | (3U << 2))) != 0U ||
        (GPIOC->MODER & 3U) != 1U || ((GPIOA->MODER >> 8) & 3U) != 1U ||
        hspi == NULL || hspi->Init.DataSize != SPI_DATASIZE_8BIT) {
        radio_pin_probe("invalid_configuration");
        log_msg("[SXDIAG] RESULT: FAIL; SUSPECT: GPIO/SPI configuration\r\n");
        return false;
    }
    if (!radio_test_init(hspi)) {
        log_msg("[SXDIAG] RESULT: FAIL; SUSPECT: RESET/BUSY or SPI wiring/status; no XOSC/TX attempted\r\n");
        return false;
    }
    log_msg("[SXDIAG] SPI: transport/standby RC confirmed; TEST 5 register 0x0740, standby RC, no RF\r\n");
    /* LoRa sync-word MSB: no PA/OCP/clock changes. Always restore after
     * a write attempt, even when write completion or verification failed. */
    uint8_t original = 0, got = 0, restored = 0;
    for (unsigned i = 1; i <= 3U; i++) {
        uint8_t sample = 0;
        sx126x_status_t result = sx126x_read_register(&sx126x_ctx, 0x0740, &sample, 1);
        char line[144];
        snprintf(line, sizeof(line),
                 "[SXDIAG] REGISTER_READ: sample=%u addr=0x0740 driver=%d value=0x%02X spi_error=0x%08lX\r\n",
                 i, (int)result, sample, (unsigned long)HAL_SPI_GetError(hspi));
        log_msg(line);
        if (result != SX126X_STATUS_OK || (i > 1U && sample != original)) {
            log_msg("[SXDIAG] RESULT: FAIL; register unreadable/unstable; no write, no RF\r\n");
            return false;
        }
        original = sample;
        HAL_Delay(10U);
    }
    uint8_t test = original ^ 0x10U;
    bool ok = sx126x_write_register(&sx126x_ctx, 0x0740, &test, 1) == SX126X_STATUS_OK;
    if (ok) { ok = sx126x_read_register(&sx126x_ctx, 0x0740, &got, 1) == SX126X_STATUS_OK && got == test; }
    bool restore_ok = sx126x_write_register(&sx126x_ctx, 0x0740, &original, 1) == SX126X_STATUS_OK;
    if (restore_ok) { restore_ok = sx126x_read_register(&sx126x_ctx, 0x0740, &restored, 1) == SX126X_STATUS_OK && restored == original; }
    if (!restore_ok) {
        log_msg("[SXDIAG] REGISTER: restore FAIL; physical reset required; no RF\r\n");
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "[SXDIAG] REGISTER: original=0x%02X test=0x%02X got=0x%02X restore=%s\r\n",
             original, test, got, restore_ok ? "verified" : "FAIL");
    log_msg(msg);
    bool status_ok = radio_status_rc(); /* Always log status after restoration. */
    log_msg(ok && restore_ok ? "[SXDIAG] REGISTER: PASS read/write/restore; RF untested\r\n" :
                              "[SXDIAG] REGISTER: FAIL read/write/restore; RF blocked\r\n");
    if (!ok || !restore_ok) {
        log_msg("[SXDIAG] RESULT: FAIL register test; XOSC skipped, RF blocked\r\n");
        return false;
    }
    if (!status_ok) {
        log_msg("[SXDIAG] STATUS: unexpected command status retained as finding; clock diagnostic only\r\n");
    }
    bool clock_ok = radio_clock_diagnostic();
    if (!clock_ok || !status_ok) {
        log_msg("[SXDIAG] RESULT: FAIL clock/status; TX skipped\r\n");
        return false;
    }
#if RADIO_QUATERNION_MODE
    const bool au915 = RADIO_FREQ_HZ >= 902000000UL;
    if (sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_RC) != SX126X_STATUS_OK ||
        sx126x_cal_img(&sx126x_ctx, au915 ? 0xE1U : 0xD7U,
                                    au915 ? 0xE9U : 0xDBU) != SX126X_STATUS_OK ||
        sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_XOSC) != SX126X_STATUS_OK) {
        log_msg("[SXDIAG] RESULT: FAIL RF image calibration sequence\r\n");
        return false;
    }
#endif
    diagnostic_radio_ready = true;
#if RADIO_QUATERNION_MODE
    log_msg("[SXDIAG] RESULT: PASS register/clock; ready for quaternion payload\r\n");
    return true;
#else
    return radio_p2p_send_test();
#endif
}

/* Called only after successful radio_diagnostic_run() initialization. */
bool radio_p2p_send_test(void)
{
    char msg[160];
    static const uint8_t payload[] = "TEST_LORA";
    snprintf(msg, sizeof(msg), "[SXDIAG] TX: text=TEST_LORA freq=%lu SF7 BW125 CR4/5 sync=3444 IQ=normal CRC=on\r\n",
             (unsigned long)RADIO_FREQ_HZ);
    log_msg(msg);
    diagnostic_tx_ready = true;
    bool tx_ok = radio_send_payload(payload, sizeof(payload) - 1U);
    diagnostic_tx_ready = false;
    sx126x_errors_mask_t errors = 0;
    sx126x_chip_status_t status = {0};
    sx126x_status_t er = sx126x_get_device_errors(&sx126x_ctx, &errors);
    sx126x_status_t sr = sx126x_get_status(&sx126x_ctx, &status);
    snprintf(msg, sizeof(msg), "[SXDIAG] TX: status_driver=%d mode=%u cmd=%u errors_driver=%d mask=0x%04X\r\n",
             (int)sr, status.chip_mode, status.cmd_status, (int)er, (unsigned)errors);
    log_msg(msg);
    bool ok = tx_ok && er == SX126X_STATUS_OK && sr == SX126X_STATUS_OK && errors == 0U;
    log_msg(ok ? "[SXDIAG] RESULT: PASS TX_DONE; reception must be checked at peer\r\n" :
                 "[SXDIAG] RESULT: FAIL TX; see IRQ/status/errors above\r\n");
    return ok;
}

/* ── radio_register_test ─────────────────────────────────────────────────
 * Escribe/lee 100 veces los 4 registros que el driver viejo no lograba
 * persistir (sync word LoRaWAN público + errata TX_CLAMP + OCP). Acceso
 * directo write_register/read_register, sin pasar por cfg_tx_clamp()/
 * set_ocp_value() — el objetivo es probar la persistencia cruda de SPI,
 * no la semántica de alto nivel. Tiene reintentos de lectura porque el
 * protoboard/cableado ha mostrado lecturas espurias aisladas. */
bool radio_register_test(void)
{
    static const uint16_t addrs[4]    = { 0x0740, 0x0741, 0x08D8, 0x08E7 };
    static const uint8_t  expected[4] = { 0x34,   0x44,   0xFE,   0x38   };
    char dbg[96];

    snprintf(dbg, sizeof(dbg), "[REGTEST] Inicio %lu iteraciones, read_retries=%lu\r\n",
             (unsigned long)RADIO_REGTEST_ITERATIONS,
             (unsigned long)RADIO_REGTEST_READ_RETRIES);
    log_msg(dbg);

    for (uint32_t i = 0; i < RADIO_REGTEST_ITERATIONS; i++) {
        for (uint32_t r = 0; r < 4U; r++) {
            if (sx126x_write_register(&sx126x_ctx, addrs[r], &expected[r], 1) != SX126X_STATUS_OK) {
                snprintf(dbg, sizeof(dbg), "[REGTEST] FAIL iter=%lu reg=0x%04X write SPI error\r\n",
                         (unsigned long)i, addrs[r]);
                log_msg(dbg);
                return false;
            }
        }

        for (uint32_t r = 0; r < 4U; r++) {
            uint8_t got = 0;
            if (!read_register_with_retries(addrs[r], expected[r], &got)) {
                snprintf(dbg, sizeof(dbg), "[REGTEST] FAIL iter=%lu reg=0x%04X esperado=0x%02X ultimo=0x%02X\r\n",
                         (unsigned long)i, addrs[r], expected[r], got);
                log_msg(dbg);
                return false;
            }
        }

        if ((i % 10U) == 9U) {
            snprintf(dbg, sizeof(dbg), "[REGTEST] OK iter=%lu\r\n", (unsigned long)(i + 1U));
            log_msg(dbg);
        }
    }

    snprintf(dbg, sizeof(dbg), "[REGTEST] OK %lu/%lu\r\n",
             (unsigned long)RADIO_REGTEST_ITERATIONS,
             (unsigned long)RADIO_REGTEST_ITERATIONS);
    log_msg(dbg);
    return true;
}

/* ── radio_xosc_test ──────────────────────────────────────────────────────
 * Un solo intento: STBY_RC -> DIO3=TCXO 1.7V -> STBY_XOSC
 * -> verifica chip_mode y OpError. Reintenta con reset si XOSC no arranca. */
bool radio_xosc_test(void)
{
    char dbg[128];

    for (uint32_t attempt = 1; attempt <= RADIO_XOSC_ATTEMPTS; attempt++) {
        snprintf(dbg, sizeof(dbg), "[XOSC] intento=%lu/%lu\r\n",
                 (unsigned long)attempt, (unsigned long)RADIO_XOSC_ATTEMPTS);
        log_msg(dbg);

        if (sx126x_clear_device_errors(&sx126x_ctx) != SX126X_STATUS_OK) { return false; }

        if (sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_RC) != SX126X_STATUS_OK) {
            log_msg("[XOSC] FAIL set_standby(RC) SPI error\r\n");
        } else if (sx126x_set_dio3_as_tcxo_ctrl(&sx126x_ctx, SX126X_TCXO_CTRL_1_7V, RADIO_TCXO_STARTUP_TICKS) != SX126X_STATUS_OK) {
            log_msg("[XOSC] FAIL set_dio3_as_tcxo_ctrl SPI error\r\n");
        } else if (sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_XOSC) != SX126X_STATUS_OK) {
            log_msg("[XOSC] FAIL set_standby(XOSC) SPI error\r\n");
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));

            sx126x_chip_status_t status = { 0 };
            sx126x_errors_mask_t errors = 0;
            if (sx126x_get_status(&sx126x_ctx, &status) != SX126X_STATUS_OK ||
                sx126x_get_device_errors(&sx126x_ctx, &errors) != SX126X_STATUS_OK) { return false; }

            snprintf(dbg, sizeof(dbg), "[XOSC] chip_mode=%u cmd_status=%u OpError=0x%04X\r\n",
                     status.chip_mode, status.cmd_status, errors);
            log_msg(dbg);

            bool ok = (status.chip_mode == SX126X_CHIP_MODE_STBY_XOSC) &&
                      ((errors & SX126X_ERRORS_XOSC_START) == 0U);
            sx126x_clear_device_errors(&sx126x_ctx);
            if (ok) {
                log_msg("[XOSC] OK\r\n");
                return true;
            }
        }

        radio_pin_probe("xosc_fail");
        if (attempt < RADIO_XOSC_ATTEMPTS) {
            log_msg("[XOSC] reset y reintento\r\n");
            if (sx126x_reset(&sx126x_ctx) != SX126X_STATUS_OK || !radio_status_rc()) { return false; }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    log_msg("[XOSC] FAIL\r\n");
    return false;
}

static bool radio_configure_lora(void)
{
    if (sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_RC) != SX126X_STATUS_OK) {
        log_msg("[P2P] LoRa configuration failed: SetStandby(RC)\r\n");
        return false;
    }

    if (sx126x_set_dio2_as_rf_sw_ctrl(&sx126x_ctx, true) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }
    if (sx126x_set_pkt_type(&sx126x_ctx, SX126X_PKT_TYPE_LORA) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }
    if (sx126x_set_rf_freq(&sx126x_ctx, RADIO_FREQ_HZ) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }

    sx126x_mod_params_lora_t mod = {
        .sf = (sx126x_lora_sf_t)P2P_LORA_SF,
        .bw = SX126X_LORA_BW_125,
        .cr = SX126X_LORA_CR_4_5,
        .ldro = 0,
    };
    if (sx126x_set_lora_mod_params(&sx126x_ctx, &mod) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }

    if (sx126x_set_lora_sync_word(&sx126x_ctx, 0x34) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    } /* codifica el patron publico 0x3444 */
    if (sx126x_set_buffer_base_address(&sx126x_ctx, 0, 0) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }
    if (sx126x_set_rx_tx_fallback_mode(&sx126x_ctx, SX126X_FALLBACK_STDBY_XOSC) != SX126X_STATUS_OK) {
        log_msg("[P2P] Failed to configure XOSC fallback\r\n");
        return false;
    }

    return true;
}

static bool radio_prepare_rf_clock(const char *operation)
{
    sx126x_chip_status_t status = {0};
    sx126x_errors_mask_t errors = 0;
    sx126x_status_t standby = SX126X_STATUS_OK;
    sx126x_status_t get_status = sx126x_get_status(&sx126x_ctx, &status);
    if (get_status != SX126X_STATUS_OK || status.chip_mode != SX126X_CHIP_MODE_STBY_XOSC) {
        standby = sx126x_set_standby(&sx126x_ctx, SX126X_STANDBY_CFG_XOSC);
        get_status = sx126x_get_status(&sx126x_ctx, &status);
    }
    const sx126x_status_t get_errors = sx126x_get_device_errors(&sx126x_ctx, &errors);
    if (standby == SX126X_STATUS_OK && get_status == SX126X_STATUS_OK &&
        get_errors == SX126X_STATUS_OK && status.chip_mode == SX126X_CHIP_MODE_STBY_XOSC &&
        errors == 0U) {
        return true;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "[P2P] %s XOSC failed: standby=%d status=%d mode=%u cmd=%u errors=%d mask=0x%04X\r\n",
             operation, (int)standby, (int)get_status, (unsigned)status.chip_mode,
             (unsigned)status.cmd_status, (int)get_errors, (unsigned)errors);
    log_msg(msg);
    return false;
}

static bool radio_prepare_packet(const char *operation, uint8_t payload_len)
{
    if (!lora_configured) {
        if (!radio_configure_lora()) { return false; }
        lora_configured = true;
    }
    if (!radio_prepare_rf_clock(operation)) { return false; }
    const sx126x_pkt_params_lora_t pkt = {
        .preamble_len_in_symb = P2P_LORA_PREAMBLE,
        .header_type = SX126X_LORA_PKT_EXPLICIT,
        .pld_len_in_bytes = payload_len,
        .crc_is_on = true,
        .invert_iq_is_on = false,
    };
    return sx126x_set_lora_pkt_params(&sx126x_ctx, &pkt) == SX126X_STATUS_OK;
}

/* ── radio_send_payload ─────────────────────────────────────────────────────
 * LoRa plano: SF7/BW125/CR4_5, CRC ON, header explícito, IQ normal,
 * syncword publico 0x3444. */
bool radio_send_payload(const uint8_t *payload, uint8_t payload_len)
{
    char dbg[96];

#if SX1262_DIAGNOSTIC_MODE
    if (!diagnostic_tx_ready && !(RADIO_QUATERNION_MODE && diagnostic_radio_ready)) {
        log_msg("[SXDIAG] TX blocked: clock/register checks required\r\n");
        return false;
    }
#endif
    if ((payload == NULL) || (payload_len == 0U)) {
        log_msg("[TX] FAIL payload vacio\r\n");
        return false;
    }

    if (!radio_prepare_packet("TX", payload_len)) { return false; }
    sx126x_pa_cfg_params_t pa_cfg = { .pa_duty_cycle = 0x02, .hp_max = 0x02, .device_sel = 0x00, .pa_lut = 0x01 };
    if (sx126x_set_pa_cfg(&sx126x_ctx, &pa_cfg) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }
    if (sx126x_cfg_tx_clamp(&sx126x_ctx) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }          /* errata TX_CLAMP_CFG */
    if (sx126x_set_ocp_value(&sx126x_ctx, 0x38) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }   /* 140 mA (0x38 * 2.5 mA) */
    if (sx126x_set_tx_params(&sx126x_ctx, RADIO_TX_POWER_DBM, SX126X_RAMP_200_US) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }

    if (sx126x_set_dio_irq_params(&sx126x_ctx,
                               SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT,
                               SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT,
                               SX126X_IRQ_NONE, SX126X_IRQ_NONE) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }
    if (sx126x_clear_irq_status(&sx126x_ctx, SX126X_IRQ_ALL) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }
    if (sx126x_write_buffer(&sx126x_ctx, 0, payload, payload_len) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
        return false;
    }

    snprintf(dbg, sizeof(dbg), "[TX] payload_len=%u\r\n", payload_len);
    log_msg(dbg);

    if (!radio_prepare_rf_clock("TX")) { return false; }
    if (sx126x_set_tx(&sx126x_ctx, 5000) != SX126X_STATUS_OK) {
        log_msg("[TX] FAIL set_tx SPI error\r\n");
        return false;
    }

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 5000U) {
        sx126x_irq_mask_t irq = 0;
        if (sx126x_get_irq_status(&sx126x_ctx, &irq) != SX126X_STATUS_OK) {
            log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
            return false;
        }

        if (irq & SX126X_IRQ_TX_DONE) {
            if (sx126x_clear_irq_status(&sx126x_ctx, SX126X_IRQ_ALL) != SX126X_STATUS_OK) {
                log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
                return false;
            }
            log_msg("[TX] TX_DONE\r\n");
            return true;
        }
        if (irq & SX126X_IRQ_TIMEOUT) {
            if (sx126x_clear_irq_status(&sx126x_ctx, SX126X_IRQ_ALL) != SX126X_STATUS_OK) {
                log_msg("[TX] FAIL configuration/IRQ SPI error; TX sequence stopped\r\n");
                return false;
            }
            log_msg("[TX] IRQ_TIMEOUT del chip\r\n");
            return false;
        }
        radio_delay_ms(5U);
    }

    snprintf(dbg, sizeof(dbg), "[TX] timeout SW tras %lums sin TX_DONE\r\n", 5000UL);
    log_msg(dbg);
    return false;
}

bool radio_send_plain_hello(void)
{
    static const uint8_t payload[] = "banana";
    return radio_send_payload(payload, (uint8_t)(sizeof(payload) - 1U));
}

bool radio_rx_start(void)
{
    if (!diagnostic_radio_ready || !radio_prepare_packet("RX", 220U)) {
        log_msg("[P2P] RX start failed: LoRa configuration\r\n");
        return false;
    }
    sx126x_irq_mask_t mask = SX126X_IRQ_RX_DONE | SX126X_IRQ_CRC_ERROR |
                             SX126X_IRQ_HEADER_ERROR | SX126X_IRQ_TIMEOUT;
    if (sx126x_set_dio_irq_params(&sx126x_ctx, mask, mask, 0, 0) != SX126X_STATUS_OK ||
        sx126x_clear_irq_status(&sx126x_ctx, SX126X_IRQ_ALL) != SX126X_STATUS_OK) {
        log_msg("[P2P] RX start failed: IRQ setup\r\n");
        return false;
    }
    if (!radio_prepare_rf_clock("RX")) { return false; }
    if (sx126x_set_rx_with_timeout_in_rtc_step(&sx126x_ctx, 0xFFFFFFU) != SX126X_STATUS_OK) {
        log_msg("[P2P] RX start failed: SetRx transport\r\n");
        return false;
    }
    return radio_rx_health();
}

int radio_rx_poll(uint8_t *payload, size_t capacity)
{
    sx126x_irq_mask_t irq = 0;
    if (sx126x_get_irq_status(&sx126x_ctx, &irq) != SX126X_STATUS_OK) {
        log_msg("[P2P] RX poll failed: GetIrqStatus transport\r\n");
        return -1;
    }
    if (irq == 0) { return 0; }
    int length = 0;
    if (irq & (SX126X_IRQ_CRC_ERROR | SX126X_IRQ_HEADER_ERROR)) {
        log_msg("[P2P] RX discarded: CRC/header error\r\n");
    } else if (irq & SX126X_IRQ_RX_DONE) {
        sx126x_rx_buffer_status_t info = {0};
        if (sx126x_get_rx_buffer_status(&sx126x_ctx, &info) != SX126X_STATUS_OK) { return -1; }
        if (info.pld_len_in_bytes > 0 && info.pld_len_in_bytes <= 220U &&
            info.pld_len_in_bytes < capacity) {
            if (sx126x_read_buffer(&sx126x_ctx, info.buffer_start_pointer,
                                  payload, info.pld_len_in_bytes) != SX126X_STATUS_OK) { return -1; }
            length = info.pld_len_in_bytes; payload[length] = 0;
        }
    }
    if (sx126x_clear_irq_status(&sx126x_ctx, SX126X_IRQ_ALL) != SX126X_STATUS_OK) { return -1; }
    if (irq & (SX126X_IRQ_TIMEOUT | SX126X_IRQ_CRC_ERROR | SX126X_IRQ_HEADER_ERROR)) {
        return radio_rx_start() ? 0 : -1;
    }
    return length;
}

bool radio_rx_health(void)
{
    sx126x_chip_status_t status = {0};
    sx126x_errors_mask_t errors = 0;
    const sx126x_status_t status_result = sx126x_get_status(&sx126x_ctx, &status);
    const sx126x_status_t errors_result = sx126x_get_device_errors(&sx126x_ctx, &errors);
    bool ok = status_result == SX126X_STATUS_OK && errors_result == SX126X_STATUS_OK &&
           status.chip_mode == SX126X_CHIP_MODE_RX && errors == 0U &&
           status.cmd_status != SX126X_CMD_STATUS_CMD_TIMEOUT &&
           status.cmd_status != SX126X_CMD_STATUS_CMD_PROCESS_ERROR &&
           status.cmd_status != SX126X_CMD_STATUS_CMD_EXEC_FAILURE;
    if (!ok) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "[P2P] RX state failed: status=%d mode=%u cmd=%u errors_status=%d mask=0x%04X\r\n",
                 (int)status_result, (unsigned)status.chip_mode, (unsigned)status.cmd_status,
                 (int)errors_result, (unsigned)errors);
        log_msg(msg);
    }
    return ok;
}
