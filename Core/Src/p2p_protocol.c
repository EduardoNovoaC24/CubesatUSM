#include "p2p_protocol.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[] = {"PING",     "GET_STATUS",  "SET_BETA",
                                    "SET_MODE", "SET_RATE",    "RESET_FILTER",
                                    "CAL_GYRO", "CAL_ACCEL",   "MAG_START",
                                    "MAG_STOP", "SAVE_CONFIG", "LOAD_CONFIG",
                                    "RELAY", "WHEEL"};
const char *P2P_CommandText(P2P_CommandName name) {
  return (unsigned)name < sizeof(names) / sizeof(names[0]) ? names[name]
                                                           : "UNKNOWN";
}
const char *P2P_ParseCommand(const char *line, size_t n, P2P_Command *out) {
  memset(out, 0, sizeof(*out));
  if (!line || n < 7U || n > P2P_SERIAL_MAX || memcmp(line, "CMD,", 4))
    return "BAD_ARGS";
  for (size_t i = 0; i < n; ++i)
    if ((unsigned char)line[i] < 33U || (unsigned char)line[i] > 126U)
      return "BAD_ARGS";
  size_t i = 4;
  uint32_t id = 0;
  for (; i < n && line[i] >= '0' && line[i] <= '9'; ++i) {
    id = id * 10U + (unsigned)(line[i] - '0');
    if (id > 65535U)
      return "BAD_ARGS";
  }
  if (!id || i == 4 || i >= n || line[i++] != ',')
    return "BAD_ARGS";
  out->id = (uint16_t)id;
  size_t start = i;
  while (i < n && line[i] != ',')
    ++i;
  unsigned cmd = 0;
  for (; cmd < sizeof(names) / sizeof(names[0]); ++cmd)
    if (strlen(names[cmd]) == i - start &&
        !memcmp(line + start, names[cmd], i - start))
      break;
  if (cmd == sizeof(names) / sizeof(names[0]))
    return "UNKNOWN_CMD";
  out->name = (P2P_CommandName)cmd;
  bool argument =
      cmd == P2P_SET_BETA || cmd == P2P_SET_RATE || cmd == P2P_SET_MODE ||
      cmd == P2P_RELAY || cmd == P2P_WHEEL;
  if (!argument)
    return i == n ? NULL : "BAD_ARGS";
  if (i >= n || n - i - 1U == 0U || n - i - 1U > 24U)
    return "BAD_ARGS";
  char arg[25];
  size_t count = n - i - 1U;
  memcpy(arg, line + i + 1U, count);
  arg[count] = 0;
  if (cmd == P2P_WHEEL) {
    if (!strcmp(arg, "ARM"))
      out->value = WHEEL_CMD_ARM;
    else if (!strcmp(arg, "STOP"))
      out->value = WHEEL_CMD_STOP;
    else if (!strcmp(arg, "STATUS"))
      out->value = WHEEL_CMD_STATUS;
    else if (!strcmp(arg, "DETUMBLE"))
      out->value = WHEEL_CMD_DETUMBLE;
    else if (!strcmp(arg, "PROFILE_AIR"))
      out->value = WHEEL_CMD_PROFILE_AIR;
    else if (!strcmp(arg, "PROFILE_BIDIR"))
      out->value = WHEEL_CMD_PROFILE_BIDIR;
    else if (!strcmp(arg, "ENABLE"))
      out->value = WHEEL_CMD_ENABLE;
    else if (strlen(arg) == 7U && !strncmp(arg, "AXIS_", 5U) &&
             arg[5] >= 'X' && arg[5] <= 'Z' && (arg[6] == '+' || arg[6] == '-'))
      out->value = WHEEL_CMD_AXIS_X_POS + 2 * (arg[5] - 'X') + (arg[6] == '-');
    else if (!strncmp(arg, "TURN:", 5U) || !strncmp(arg, "LIGHT:", 6U) ||
             !strncmp(arg, "LIGHTINV:", 9U)) {
      const bool turn = arg[0] == 'T';
      const bool inverted = !turn && arg[5] == 'I';
      const char *number = arg + (turn ? 5U : inverted ? 9U : 6U);
      char *end;
      if ((*number < '0' || *number > '9') && !(turn && *number == '-'))
        return "BAD_ARGS";
      const long degrees = strtol(number, &end, 10);
      if (end == number || *end ||
          (turn ? (degrees < -180 || degrees > 180 || degrees == 0)
                : (degrees < 0 || degrees >= 360)))
        return "BAD_ARGS";
      out->value = (float)(turn ? WHEEL_TURN_BASE + 180 + degrees
                               : WHEEL_LIGHT_BASE + degrees + (inverted ? 360 : 0));
    } else {
      char *end;
      long level = strtol(arg, &end, 10);
      if (end == arg || *end || level < -P2P_WHEEL_MAX_LEVEL ||
          level > P2P_WHEEL_MAX_LEVEL || level == 0)
        return "BAD_ARGS";
      out->value = (float)level;
    }
  } else if (cmd == P2P_RELAY) {
    if (!strcmp(arg, "OFF"))
      out->value = P2P_RELAY_OFF;
    else if (!strcmp(arg, "ON"))
      out->value = P2P_RELAY_ON;
    else if (!strcmp(arg, "STATUS"))
      out->value = P2P_RELAY_STATUS;
    else
      return "BAD_ARGS";
  } else if (cmd == P2P_SET_MODE) {
    if (!strcmp(arg, "6DOF"))
      out->value = 6;
    else if (!strcmp(arg, "9DOF"))
      out->value = 9;
    else
      return "BAD_ARGS";
  } else {
    char *end;
    out->value = strtof(arg, &end);
    if (end == arg || *end || !isfinite(out->value))
      return "BAD_ARGS";
    if (cmd == P2P_SET_BETA && (out->value < 0.001f || out->value > 2.0f))
      return "BAD_ARGS";
    if (cmd == P2P_SET_RATE &&
        (out->value < 1 || out->value > 10 || floorf(out->value) != out->value))
      return "BAD_ARGS";
  }
  return NULL;
}
uint32_t P2P_Hash(const void *data, size_t n) {
  const uint8_t *p = data;
  uint32_t crc = 0xFFFFFFFFU;
  while (n--) {
    crc ^= *p++;
    for (unsigned i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
  }
  return ~crc;
}
int P2P_CacheClaim(P2P_Cache *c, uint16_t id, uint32_t hash,
                   const char **reply) {
  *reply = NULL;
  for (unsigned i = 0; i < P2P_RECENT_IDS; ++i)
    if (c->entries[i].id == id) {
      *reply = c->entries[i].reply;
      return c->entries[i].hash == hash ? 1 : -1;
    }
  P2P_Recent *e = &c->entries[c->next++ % P2P_RECENT_IDS];
  e->id = id;
  e->hash = hash;
  e->reply[0] = 0;
  return 0;
}
void P2P_CacheReply(P2P_Cache *c, uint16_t id, const char *reply) {
  for (unsigned i = 0; i < P2P_RECENT_IDS; ++i)
    if (c->entries[i].id == id) {
      snprintf(c->entries[i].reply, sizeof(c->entries[i].reply), "%s", reply);
      return;
    }
}
uint32_t P2P_TimeOnAirUs(size_t n) {
  /* SX1262 SF7/BW125/CR4_5, explicit header, CRC, LDRO off. */
  const uint32_t blocks = (8U * (uint32_t)n + 16U + 27U) / 28U;
  return (P2P_LORA_PREAMBLE + 4U + 8U + blocks * 5U) * 1024U + 256U;
}
void P2P_PackLight(uint8_t packed[6], const uint16_t samples[P2P_LIGHT_CHANNELS]) {
  for (unsigned i = 0; i < 2; ++i) {
    const uint16_t a = samples[2U*i] & P2P_LIGHT_MAX;
    const uint16_t b = samples[2U*i+1U] & P2P_LIGHT_MAX;
    packed[3U*i] = (uint8_t)a;
    packed[3U*i+1U] = (uint8_t)((a >> 8U) | (b << 4U));
    packed[3U*i+2U] = (uint8_t)(b >> 4U);
  }
}
void P2P_UnpackLight(uint16_t samples[P2P_LIGHT_CHANNELS], const uint8_t packed[6]) {
  for (unsigned i = 0; i < 2; ++i) {
    const uint8_t middle = packed[3U*i+1U];
    samples[2U*i] = packed[3U*i] | (uint16_t)(middle & 0x0FU) << 8U;
    samples[2U*i+1U] = (middle >> 4U) | (uint16_t)packed[3U*i+2U] << 4U;
  }
}
void P2P_BudgetInit(P2P_AirtimeBudget *b, uint32_t now) {
  b->tokens_us = 1000000U;
  b->last_ms = now;
}
bool P2P_BudgetTake(P2P_AirtimeBudget *b, uint32_t now, size_t n,
                    bool priority) {
  /* Conservative 1% replenishment, 1 s burst capacity. Keep 200 ms for ACKs.
   * Requested rate is subordinate to this budget; not a regulatory
   * certification. */
  uint32_t elapsed = now - b->last_ms;
  b->last_ms = now;
  uint64_t tokens = (uint64_t)b->tokens_us + (uint64_t)elapsed * 10U;
  b->tokens_us = tokens > 1000000U ? 1000000U : (uint32_t)tokens;
  uint32_t cost = P2P_TimeOnAirUs(n), reserve = priority ? 0U : 200000U;
  if (b->tokens_us < cost + reserve)
    return false;
  b->tokens_us -= cost;
  return true;
}
