#include "adsb_protocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static ADSB_Result_t feed(ADSB_Decoder_t *d, const char *s, ADSB_Aircraft_t *a)
{
    ADSB_Result_t result = ADSB_NONE;
    while (*s) result = ADSB_Feed(d, (uint8_t)*s++, a);
    return result;
}

int main(void)
{
    const char *valid = "$ADSB,1,E8032E,SKU176,12575,253,32,-33.472,-70.810";
    ADSB_Aircraft_t a = {0};
    assert(ADSB_CRC16("123456789", 9) == 0x29b1U);
    assert(ADSB_Parse("HOLA_L476", &a) == ADSB_HELLO);
    assert(ADSB_Parse(valid, &a) == ADSB_DATA);
    assert(!strcmp(a.icao, "E8032E") && !strcmp(a.callsign, "SKU176"));
    assert(a.altitude_ft == 12575 && a.speed_kt == 253 && a.heading_deg == 32);
    assert(a.valid_fields == 15 && a.position_valid && a.data_valid);
    assert(fabs(a.latitude + 33.472) < 0.000001);
    assert(fabs(a.longitude + 70.810) < 0.000001);
    ADSB_Aircraft_t previous = a;
    char output[ADSB_LINE_MAX + 1U];
    const char *expected = "$ADSB,1,E8032E,SKU176,12575,253,32,-33.472000,-70.810000\r\n";
    assert(ADSB_FormatUART(&a, output, sizeof(output)) == strlen(expected));
    assert(strcmp(output, expected) == 0);
    assert(ADSB_FormatUART(&a, output, 8) == 0 && output[0] == '\0');
    assert(ADSB_FormatUART(&a, NULL, 0) == 0);
    const char *invalid[] = {
        "", "HOLA_L476\r", "$ADSB,2,E8032E,SKU176,1,1,1,0,0",
        "$ADSB,1,E8032,SKU176,1,1,1,0,0", "$ADSB,1,X8032E,SKU176,1,1,1,0,0",
        "$ADSB,1,E8032E,TOOLONG123,1,1,1,0,0", "$ADSB,1,E8032E,sku176,1,1,1,0,0",
        "$ADSB,1,E8032E,A,100001,1,1,0,0", "$ADSB,1,E8032E,A,-2001,1,1,0,0",
        "$ADSB,1,E8032E,A,1,2001,1,0,0", "$ADSB,1,E8032E,A,1,1,360,0,0",
        "$ADSB,1,E8032E,A,1,1,1,91,0", "$ADSB,1,E8032E,A,1,1,1,0,-181",
        "$ADSB,1,E8032E,A,1,1,1,nan,0", "$ADSB,1,E8032E,A,1,1,1,inf,0",
        "$ADSB,1,E8032E,A,1,1,1,1e2,0", "$ADSB,1,E8032E,A,1,1,1, 1,0",
        "$ADSB,1,E8032E,A,1,1,1,1.1234567,0", "$ADSB,1,E8032E,A,1,1,1,0",
        "$ADSB,1,E8032E,A,1,1,1,0,0,extra", "$ADSB,1,E8032E,A,1,1,1,0,0*FFFF",
        "$ADSB,1,E8032E,A,1,1,1,0,0*123", "$ADSB,1,E8032E,A,1,1,1,0,0*ZZZZ",
        "$ADSB,1,E8032E,A,9999999999999999999,1,1,0,0"
    };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        assert(ADSB_Parse(invalid[i], &a) == ADSB_INVALID);
        assert(memcmp(&a, &previous, sizeof(a)) == 0);
    }
    char crc_line[128];
    snprintf(crc_line, sizeof(crc_line), "%s*%04X", valid, ADSB_CRC16(valid, strlen(valid)));
    assert(ADSB_Parse(crc_line, &a) == ADSB_DATA);
    assert(ADSB_Parse("$ADSB,1,e8032e,,,,,,", &a) == ADSB_DATA);
    assert(!strcmp(a.icao, "E8032E") && a.valid_fields == 0 && !a.position_valid);
    assert(ADSB_FormatUART(&a, output, sizeof(output)) > 0);
    assert(!strcmp(output, "$ADSB,1,E8032E,,,,,,\r\n"));
    assert(ADSB_Parse("$ADSB,1,E8032E,,0,0,0,0,0", &a) == ADSB_DATA);
    assert(a.position_valid && a.valid_fields == 15);
    assert(ADSB_FormatUART(&a, output, sizeof(output)) > 0);
    assert(!strcmp(output, "$ADSB,1,E8032E,,0,0,0,0.000000,0.000000\r\n"));
    assert(ADSB_Parse("$ADSB,1,E8032E,,,,,-33.472,", &a) == ADSB_DATA);
    assert(!a.position_valid && a.latitude == 0 && a.longitude == 0);
    assert(ADSB_FormatUART(&a, output, sizeof(output)) > 0);
    assert(!strcmp(output, "$ADSB,1,E8032E,,,,,,\r\n"));
    const char *coordinates[][2] = {
        {"$ADSB,1,ABCDEF,TEST,-2000,2000,359,-0.000001,0.000001", "$ADSB,1,ABCDEF,TEST,-2000,2000,359,-0.000001,0.000001\r\n"},
        {"$ADSB,1,ABCDEF,TEST,100000,0,0,-90,180", "$ADSB,1,ABCDEF,TEST,100000,0,0,-90.000000,180.000000\r\n"},
        {"$ADSB,1,ABCDEF,TEST,,,,89.999999,-179.999999", "$ADSB,1,ABCDEF,TEST,,,,89.999999,-179.999999\r\n"},
        {"$ADSB,1,ABCDEF,TEST,,,,1.234567,12.345678", "$ADSB,1,ABCDEF,TEST,,,,1.234567,12.345678\r\n"}
    };
    for (size_t i = 0; i < sizeof(coordinates)/sizeof(*coordinates); ++i) {
        assert(ADSB_Parse(coordinates[i][0], &a) == ADSB_DATA);
        assert(ADSB_FormatUART(&a, output, sizeof(output)) > 0);
        assert(!strcmp(output, coordinates[i][1]));
    }
    ADSB_Decoder_t d = {0};
    assert(feed(&d, "HOLA_", &a) == ADSB_NONE);
    assert(feed(&d, "L476\r\n", &a) == ADSB_HELLO);
    for (unsigned i = 0; i < 256; ++i) assert(ADSB_Feed(&d, 'A', &a) == ADSB_NONE);
    assert(feed(&d, "HOLA_L476\n", &a) == ADSB_INVALID);
    assert(feed(&d, "HOLA_L476\n", &a) == ADSB_HELLO);
    assert(ADSB_Feed(&d, 0, &a) == ADSB_NONE);
    assert(feed(&d, "HOLA_L476\n", &a) == ADSB_INVALID);
    assert(feed(&d, "HOLA_", &a) == ADSB_NONE);
    ADSB_Discard(&d);
    assert(feed(&d, "L476\n", &a) == ADSB_INVALID);
    assert(feed(&d, valid, &a) == ADSB_NONE);
    assert(feed(&d, "\r\n", &a) == ADSB_DATA);
    uint8_t compact[23];
    assert(ADSB_EncodeCompact(&a, "ABCDEF", compact, sizeof(compact)) == 0);
    assert(ADSB_EncodeCompact(&a, NULL, compact, 22) == 0);
    assert(ADSB_EncodeCompact(&a, "E8032E", compact, sizeof(compact)) == 23);
    assert(compact[0] == 1 && compact[1] == 15 && compact[2] == 0xE8 && compact[4] == 0x2E);
    assert(compact[5] == 0x1f && compact[6] == 0x31 && compact[9] == 253);
    a.data_valid = false;
    assert(ADSB_FormatUART(&a, output, sizeof(output)) == 0);
    assert(ADSB_EncodeCompact(&a, NULL, compact, sizeof(compact)) == 0);
    puts("ADSB protocol: OK");
}
