#ifndef ADSB_PROTOCOL_H
#define ADSB_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ADSB_LINE_MAX 127U /* bytes before LF, including optional CR */
#define ADSB_VALID_ALTITUDE 0x01U
#define ADSB_VALID_SPEED    0x02U
#define ADSB_VALID_HEADING  0x04U
#define ADSB_VALID_POSITION 0x08U

typedef struct {
    char icao[7];
    char callsign[9];
    int32_t altitude_ft;
    uint16_t speed_kt;
    uint16_t heading_deg;
    double latitude;
    double longitude;
    int16_t rssi_db10; /* not carried by v1; always zero, not measured */
    uint32_t last_update_ms;
    uint32_t messages_received; /* global valid ADS-B frames, not per aircraft */
    uint8_t valid_fields;
    bool position_valid;
    bool data_valid;
} ADSB_Aircraft_t;

typedef enum { ADSB_NONE, ADSB_HELLO, ADSB_DATA, ADSB_INVALID } ADSB_Result_t;
typedef struct {
    char line[ADSB_LINE_MAX + 1U];
    size_t used;
    bool discard;
} ADSB_Decoder_t;

uint16_t ADSB_CRC16(const char *data, size_t length);
ADSB_Result_t ADSB_Parse(const char *line, ADSB_Aircraft_t *aircraft);
ADSB_Result_t ADSB_Feed(ADSB_Decoder_t *decoder, uint8_t byte,
                        ADSB_Aircraft_t *aircraft);
/* Abort a damaged/incomplete line; resynchronize at the next LF. */
void ADSB_Discard(ADSB_Decoder_t *decoder);
/* Canonical nine-field ASCII frame with CRLF, no CRC. Missing fields stay
 * empty; position uses six decimal places. Returns bytes excluding NUL,
 * or zero on invalid snapshot/insufficient capacity. No printf float support. */
size_t ADSB_FormatUART(const ADSB_Aircraft_t *aircraft, char *output, size_t capacity);
/* Explicit 23-byte, little-endian v1 payload; no struct packing or radio TX.
 * Returns zero if stale/invalid or the optional ICAO selector does not match. */
size_t ADSB_EncodeCompact(const ADSB_Aircraft_t *aircraft, const char *icao,
                          uint8_t *output, size_t capacity);
#endif
