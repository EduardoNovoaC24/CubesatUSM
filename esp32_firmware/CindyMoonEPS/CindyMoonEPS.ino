#include <Arduino.h>
#include <Wire.h>

// Confirm the fitted shunt marking (R100 means 0.1 ohm).
#ifndef EPS_SOLAR_SHUNT_UOHM
#define EPS_SOLAR_SHUNT_UOHM 100000L
#endif

// Leave disabled until EPS specifies an ADC1 pin and a safe divider.
#ifndef EPS_BATTERY_ADC_PIN
#define EPS_BATTERY_ADC_PIN -1
#endif
#if EPS_BATTERY_ADC_PIN >= 0
#if !defined(EPS_BATTERY_R_TOP_OHM) || !defined(EPS_BATTERY_R_BOTTOM_OHM)
#error "Define the actual battery voltage divider before enabling its ADC"
#endif
static_assert(EPS_BATTERY_R_TOP_OHM > 0 && EPS_BATTERY_R_BOTTOM_OHM > 0);
#endif
static_assert(EPS_SOLAR_SHUNT_UOHM > 0);

static constexpr int inaSdaPin = 21;
static constexpr int inaSclPin = 19;
static constexpr uint32_t sampleIntervalMs = 1000U;
static constexpr uint32_t retryIntervalMs = 5000U;
static uint8_t inaAddress;
static constexpr uint8_t inaConfigRegister = 0x00;
static constexpr uint8_t inaShuntRegister = 0x01;
static constexpr uint8_t inaBusRegister = 0x02;
// 32 V bus range, +/-320 mV shunt range, triggered 12-bit shunt and bus.
static constexpr uint16_t inaConfiguration = 0x399B;
static constexpr uint32_t inaConversionTimeoutMs = 20U;
static constexpr uint16_t inaConversionReady = 1U << 1;
static constexpr uint16_t inaMathOverflow = 1U;
static bool inaReady;
static uint16_t sequence;
static uint32_t lastSampleMs;
static uint32_t lastRetryMs;

static bool startInaBus()
{
    if (!Wire.begin(inaSdaPin, inaSclPin, 100000U)) {
        Serial0.println("[INA219] No se pudo iniciar I2C");
        return false;
    }
    Wire.setTimeOut(50);
    return true;
}

static bool readInaRegister(uint8_t reg, uint16_t &value)
{
    Wire.beginTransmission(inaAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(inaAddress, uint8_t(2)) != 2) {
        return false;
    }
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    value = (uint16_t(high) << 8U) | low;
    return true;
}

static bool triggerInaConversion()
{
    Wire.beginTransmission(inaAddress);
    Wire.write(inaConfigRegister);
    Wire.write(uint8_t(inaConfiguration >> 8U));
    Wire.write(uint8_t(inaConfiguration & 0xFFU));
    return Wire.endTransmission() == 0;
}

static bool readInaSample(uint16_t &bus, uint16_t &shunt)
{
    if (!triggerInaConversion()) {
        return false;
    }
    const uint32_t startedMs = millis();
    do {
        if (!readInaRegister(inaBusRegister, bus)) {
            return false;
        }
        if (bus & inaConversionReady) {
            return readInaRegister(inaShuntRegister, shunt);
        }
        delay(1);
    } while (millis() - startedMs < inaConversionTimeoutMs);
    return false;
}

static bool configureIna()
{
    uint8_t detected = 0;
    for (uint8_t address = 0x40; address <= 0x4F; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() != 0) {
            continue;
        }
        if (detected != 0) {
            Serial0.println("[INA219] Mas de un dispositivo en 0x40-0x4F");
            return false;
        }
        detected = address;
    }
    if (detected == 0) {
        Serial0.println("[INA219] Sin respuesta I2C en SDA GPIO21 / SCL GPIO19");
        return false;
    }
    inaAddress = detected;

    uint16_t config;
    if (!triggerInaConversion() ||
        !readInaRegister(inaConfigRegister, config) || config != inaConfiguration) {
        Serial0.printf("[INA219] Error de configuracion en 0x%02X\r\n", unsigned(inaAddress));
        return false;
    }
    Serial0.printf("[INA219] Detectado en 0x%02X\r\n", unsigned(inaAddress));
    return true;
}

void setup()
{
    // UART0 TX on GPIO1 for the STM32 link and USB serial monitor.
    Serial0.begin(115200, SERIAL_8N1, -1, 1);
    inaReady = startInaBus() && configureIna();
    lastRetryMs = millis();
    lastSampleMs = lastRetryMs - sampleIntervalMs;
#if EPS_BATTERY_ADC_PIN >= 0
    analogSetPinAttenuation(EPS_BATTERY_ADC_PIN, ADC_11db);
#endif
}

void loop()
{
    const uint32_t now = millis();
    if (now - lastSampleMs < sampleIntervalMs) {
        delay(1);
        return;
    }
    lastSampleMs = now;

    unsigned flags = 0, solarMv = 0, batteryMv = 0;
    long solarUa = 0;
    if (!inaReady) {
        if (now - lastRetryMs >= retryIntervalMs) {
            lastRetryMs = now;
            Wire.end();
            inaReady = startInaBus() && configureIna();
        }
    }
    if (inaReady) {
        uint16_t bus, shunt;
        if (!readInaSample(bus, shunt)) {
            Serial0.println("[INA219] Fallo I2C o conversion sin completar");
            inaReady = false;
            lastRetryMs = now;
        } else if (!(bus & inaMathOverflow) && (bus >> 3U) <= 8000U) {
            const int32_t shuntCounts = shunt < 32768U ? shunt : int32_t(shunt) - 65536;
            const int64_t current = int64_t(shuntCounts) * 10000000L / EPS_SOLAR_SHUNT_UOHM;
            if (current >= -10000000L && current <= 10000000L) {
                solarMv = (bus >> 3U) * 4U;
                solarUa = long(current);
                flags |= 1U;
            }
        }
        if (inaReady && !(flags & 1U)) {
            Serial0.printf("[INA219] Conversion invalida: BUS=0x%04X SHUNT=0x%04X\r\n",
                           unsigned(bus), unsigned(shunt));
        }
    }
#if EPS_BATTERY_ADC_PIN >= 0
    const uint32_t adcMv = analogReadMilliVolts(EPS_BATTERY_ADC_PIN);
    const uint64_t estimate = uint64_t(adcMv) *
        (uint64_t(EPS_BATTERY_R_TOP_OHM) + EPS_BATTERY_R_BOTTOM_OHM) / EPS_BATTERY_R_BOTTOM_OHM;
    if (adcMv < 3000U && estimate <= 33000U) {
        batteryMv = unsigned(estimate);
        flags |= 2U;
    }
#endif
    char line[64];
    snprintf(line, sizeof(line), "EPSI,%u,%u,%u,%ld,%u",
             unsigned(sequence++), flags, solarMv, solarUa, batteryMv);
    Serial0.println(line);
}
