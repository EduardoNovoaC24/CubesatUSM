#include "adsb_protocol.h"
#include <string.h>
#include <stdio.h>

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

uint16_t ADSB_CRC16(const char *data, size_t length)
{
    uint16_t crc = 0xffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)(uint8_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc & 0x8000U) ? ((uint32_t)crc << 1) ^ 0x1021U : (uint32_t)crc << 1);
    }
    return crc;
}

/* Strict bounded decimal, no exponent, NaN, whitespace or libc allocation.
 * At most six fractional digits; integers use precision zero. */
static bool decimal(const char *s, int32_t min, int32_t max,
                    unsigned precision, double *value)
{
    bool negative = false;
    if (*s == '-') { negative = true; ++s; }
    if (*s < '0' || *s > '9') return false;
    uint32_t whole = 0;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10U + (unsigned)(*s++ - '0');
        if (whole > 1000000U) return false;
    }
    double fraction = 0.0, scale = 1.0;
    if (*s == '.' && precision != 0U) {
        ++s;
        unsigned digits = 0;
        while (*s >= '0' && *s <= '9') {
            if (++digits > precision) return false;
            fraction = fraction * 10.0 + (*s++ - '0');
            scale *= 10.0;
        }
        if (digits == 0) return false;
    }
    if (*s != '\0') return false;
    *value = ((double)whole + fraction / scale) * (negative ? -1.0 : 1.0);
    return *value >= min && *value <= max;
}

ADSB_Result_t ADSB_Parse(const char *line, ADSB_Aircraft_t *aircraft)
{
    if (line == NULL || aircraft == NULL) return ADSB_INVALID;
    size_t length = 0;
    while (length <= ADSB_LINE_MAX && line[length] != '\0') ++length;
    if (length > ADSB_LINE_MAX) return ADSB_INVALID;
    if (strcmp(line, "HOLA_L476") == 0) return ADSB_HELLO;
    if (strncmp(line, "$ADSB,1,", 8) != 0) return ADSB_INVALID;
    char copy[ADSB_LINE_MAX + 1U];
    memcpy(copy, line, length + 1U);
    char *star = strchr(copy, '*');
    if (star != NULL) {
        if (strlen(star + 1) != 4U) return ADSB_INVALID;
        uint16_t expected = 0;
        for (unsigned i = 1; i <= 4; ++i) {
            int digit = hex(star[i]);
            if (digit < 0) return ADSB_INVALID;
            expected = (uint16_t)((expected << 4) | digit);
        }
        /* CRC includes '$' and excludes '*'. */
        if (ADSB_CRC16(copy, (size_t)(star - copy)) != expected) return ADSB_INVALID;
        *star = '\0';
    }
    char *fields[9];
    unsigned count = 1;
    fields[0] = copy;
    for (char *p = copy; *p; ++p) {
        if (*p == ',') {
            if (count == 9U) return ADSB_INVALID;
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    if (count != 9U || strlen(fields[2]) != 6U || strlen(fields[3]) > 8U)
        return ADSB_INVALID;
    ADSB_Aircraft_t result = {0};
    for (unsigned i = 0; i < 6U; ++i) {
        int digit = hex(fields[2][i]);
        if (digit < 0) return ADSB_INVALID;
        result.icao[i] = "0123456789ABCDEF"[digit];
    }
    for (size_t i = 0; fields[3][i]; ++i) {
        char c = fields[3][i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' '))
            return ADSB_INVALID;
        result.callsign[i] = c;
    }
    double value;
    if (*fields[4]) {
        if (!decimal(fields[4], -2000, 100000, 0, &value)) return ADSB_INVALID;
        result.altitude_ft = (int32_t)value;
        result.valid_fields |= ADSB_VALID_ALTITUDE;
    }
    if (*fields[5]) {
        if (!decimal(fields[5], 0, 2000, 0, &value)) return ADSB_INVALID;
        result.speed_kt = (uint16_t)value;
        result.valid_fields |= ADSB_VALID_SPEED;
    }
    if (*fields[6]) {
        if (!decimal(fields[6], 0, 359, 0, &value)) return ADSB_INVALID;
        result.heading_deg = (uint16_t)value;
        result.valid_fields |= ADSB_VALID_HEADING;
    }
    if (*fields[7] && !decimal(fields[7], -90, 90, 6, &result.latitude)) return ADSB_INVALID;
    if (*fields[8] && !decimal(fields[8], -180, 180, 6, &result.longitude)) return ADSB_INVALID;
    result.position_valid = *fields[7] && *fields[8];
    if (result.position_valid) result.valid_fields |= ADSB_VALID_POSITION;
    else { result.latitude = 0; result.longitude = 0; }
    result.data_valid = true;
    *aircraft = result; /* publish only after full validation */
    return ADSB_DATA;
}

void ADSB_Discard(ADSB_Decoder_t *decoder)
{
    decoder->used = 0;
    decoder->discard = true;
}

ADSB_Result_t ADSB_Feed(ADSB_Decoder_t *decoder, uint8_t byte, ADSB_Aircraft_t *aircraft)
{
    if (byte == '\n') {
        if (decoder->discard) {
            decoder->discard = false;
            decoder->used = 0;
            return ADSB_INVALID;
        }
        if (decoder->used && decoder->line[decoder->used - 1U] == '\r') --decoder->used;
        decoder->line[decoder->used] = '\0';
        decoder->used = 0;
        return ADSB_Parse(decoder->line, aircraft);
    }
    if (!decoder->discard) {
        if (decoder->used == ADSB_LINE_MAX || byte == 0 || byte > 126U ||
            (byte < 32U && byte != '\r')) ADSB_Discard(decoder);
        else decoder->line[decoder->used++] = (char)byte;
    }
    return ADSB_NONE;
}

static void coordinate_text(double value, char *output, size_t capacity)
{
    bool negative = value < 0.0;
    double magnitude = negative ? -value : value;
    /* Parsed coordinates have at most six decimals. Round representation
     * noise instead of losing one microdegree to truncation. */
    int32_t microdegrees = (int32_t)(magnitude * 1000000.0 + 0.5);
    snprintf(output, capacity, "%s%ld.%06ld", negative ? "-" : "",
             (long)(microdegrees / 1000000), (long)(microdegrees % 1000000));
}

size_t ADSB_FormatUART(const ADSB_Aircraft_t *a, char *output, size_t capacity)
{
    if (!output || capacity == 0) return 0;
    output[0] = '\0';
    if (!a || !a->data_valid) return 0;
    char altitude[12] = "", speed[6] = "", heading[6] = "";
    char latitude[16] = "", longitude[16] = "";
    if (a->valid_fields & ADSB_VALID_ALTITUDE)
        snprintf(altitude, sizeof(altitude), "%ld", (long)a->altitude_ft);
    if (a->valid_fields & ADSB_VALID_SPEED)
        snprintf(speed, sizeof(speed), "%u", (unsigned)a->speed_kt);
    if (a->valid_fields & ADSB_VALID_HEADING)
        snprintf(heading, sizeof(heading), "%u", (unsigned)a->heading_deg);
    if (a->position_valid && (a->valid_fields & ADSB_VALID_POSITION)) {
        coordinate_text(a->latitude, latitude, sizeof(latitude));
        coordinate_text(a->longitude, longitude, sizeof(longitude));
    }
    int length = snprintf(output, capacity, "$ADSB,1,%s,%s,%s,%s,%s,%s,%s\r\n",
                          a->icao, a->callsign, altitude, speed, heading, latitude, longitude);
    if (length < 0 || (size_t)length >= capacity) {
        output[0] = '\0';
        return 0;
    }
    return (size_t)length;
}

static void put32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) out[i] = (uint8_t)(value >> (8U * i));
}

size_t ADSB_EncodeCompact(const ADSB_Aircraft_t *a, const char *icao,
                          uint8_t *output, size_t capacity)
{
    if (!a || !output || capacity < 23U || !a->data_valid ||
        (icao && strcmp(icao, a->icao) != 0)) return 0;
    output[0] = 1;
    output[1] = a->valid_fields;
    for (unsigned i = 0; i < 3; ++i) {
        int high = hex(a->icao[2U*i]), low = hex(a->icao[2U*i+1U]);
        if (high < 0 || low < 0) return 0;
        output[2U + i] = (uint8_t)((high << 4) | low);
    }
    put32(output + 5, (uint32_t)a->altitude_ft);
    output[9] = (uint8_t)a->speed_kt;
    output[10] = (uint8_t)(a->speed_kt >> 8);
    output[11] = (uint8_t)a->heading_deg;
    output[12] = (uint8_t)(a->heading_deg >> 8);
    put32(output + 13, (uint32_t)(int32_t)(a->latitude * 1000000.0));
    put32(output + 17, (uint32_t)(int32_t)(a->longitude * 1000000.0));
    output[21] = 0; /* reserved */
    output[22] = 0;
    return 23U;
}
