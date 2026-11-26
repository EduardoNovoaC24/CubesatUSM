#include "p2p_control.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static char replies[64][221];
static unsigned reply_count;
static bool emit(const char *s) {
  assert(reply_count < 64);
  snprintf(replies[reply_count++], 221, "%s", s);
  return true;
}
static void command(P2P_Control *c, const char *s, uint32_t now) {
  P2P_Command cmd;
  assert(!P2P_ParseCommand(s, strlen(s), &cmd));
  P2P_ControlCommand(c, &cmd, now);
}
int main(void) {
  P2P_Command cmd;
  assert(!P2P_ParseCommand("CMD,41,SET_BETA,0.075", 21, &cmd));
  assert(cmd.id == 41 && fabsf(cmd.value - 0.075f) < 1e-6f);
  const struct { const char *frame; int action; } wheel_setup[] = {
      {"CMD,42,WHEEL,PROFILE_AIR", WHEEL_CMD_PROFILE_AIR},
      {"CMD,43,WHEEL,PROFILE_BIDIR", WHEEL_CMD_PROFILE_BIDIR},
      {"CMD,44,WHEEL,ENABLE", WHEEL_CMD_ENABLE},
      {"CMD,45,WHEEL,AXIS_Z-", WHEEL_CMD_AXIS_Z_NEG},
      {"CMD,46,WHEEL,TURN:-45", WHEEL_TURN_BASE + 135},
      {"CMD,47,WHEEL,LIGHTINV:359", WHEEL_LIGHT_BASE + 719},
  };
  for (unsigned i = 0; i < sizeof(wheel_setup) / sizeof(wheel_setup[0]); ++i) {
    assert(!P2P_ParseCommand(wheel_setup[i].frame, strlen(wheel_setup[i].frame), &cmd));
    assert(cmd.name == P2P_WHEEL && cmd.value == wheel_setup[i].action);
  }
  const char *bad[] = {
      "CMD,0,PING",         "CMD,65536,PING",     "CMD,1,PING,extra",
      "CMD,1,SET_BETA,nan", "CMD,1,SET_BETA,inf", "CMD,1,SET_BETA,0",
      "CMD,1,SET_RATE,1.5", "CMD,1,SET_RATE,11",  "CMD,1,SET_MODE,10DOF",
      "CMD,1,PING\n",       "CMD,1,PING\0junk"};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]) - 1; ++i)
    assert(P2P_ParseCommand(bad[i], strlen(bad[i]), &cmd));
  assert(P2P_ParseCommand(bad[10], 15, &cmd));
  char oversized[181];
  memset(oversized, 'A', sizeof(oversized));
  assert(P2P_ParseCommand(oversized, 181, &cmd));
  assert(!strcmp(P2P_ParseCommand("CMD,5,BOGUS", 11, &cmd), "UNKNOWN_CMD") &&
         cmd.id == 5);
  P2P_Cache cache = {0};
  const char *reply;
  assert(P2P_CacheClaim(&cache, 41, 123, &reply) == 0);
  P2P_CacheReply(&cache, 41, "ACK,41,OK,STARTED");
  assert(P2P_CacheClaim(&cache, 41, 123, &reply) == 1 &&
         !strcmp(reply, "ACK,41,OK,STARTED"));
  assert(P2P_CacheClaim(&cache, 41, 456, &reply) == -1);
  P2P_CacheReply(&cache, 41, "EVT,41,CAL_GYRO,DONE,BIAS_APPLIED");
  assert(P2P_CacheClaim(&cache, 41, 123, &reply) == 1 && strstr(reply, "DONE"));
  assert(P2P_Hash("123456789", 9) == 0xCBF43926U);
  assert(P2P_TimeOnAirUs(9) == 41216U);
  P2P_AirtimeBudget budget;
  P2P_BudgetInit(&budget, 0);
  unsigned count = 0;
  while (P2P_BudgetTake(&budget, 0, 150, false))
    ++count;
  assert(count > 0 && count < 10 && budget.tokens_us >= 200000);
  assert(P2P_BudgetTake(&budget, 0, 40, true));
  budget.tokens_us = 0;
  assert(!P2P_BudgetTake(&budget, 1, 40, true));
  assert(P2P_BudgetTake(&budget, 60000, 40, true));
  P2P_Control c;
  P2P_ControlInit(&c, emit, NULL, NULL);
  command(&c, "CMD,1,SET_BETA,0.075", 0);
  assert(strstr(replies[reply_count - 1], "NOT_READY"));
  P2P_Sample sample = {.accel = {0, 0, -1},
                       .gyro = {0.2f, -0.1f, 0.05f},
                       .mag = {40, 0, 0},
                       .imu_ok = true,
                       .mag_ok = true};
  P2P_ControlSample(&c, &sample, 10);
  command(&c, "CMD,2,SET_BETA,0.075", 10);
  assert(fabsf(c.config.gain - 0.075f) < 1e-6f);
  command(&c, "CMD,3,SET_MODE,9DOF", 10);
  assert(strstr(replies[reply_count - 1], "MAG_UNAVAILABLE"));
  command(&c, "CMD,4,CAL_GYRO", 10);
  assert(strstr(replies[reply_count - 1], "OK,STARTED"));
  command(&c, "CMD,5,RESET_FILTER", 10);
  assert(strstr(replies[reply_count - 1], "ERR,BUSY"));
  sample.gyro[0] = 10;
  P2P_ControlSample(&c, &sample, 15);
  assert(c.calibration && c.count == 0);
  sample.gyro[0] = 0.2f;
  for (unsigned i = 0; i < 200; ++i)
    P2P_ControlSample(&c, &sample, 20 + i * 20);
  assert(c.config.gyro_calibrated &&
         fabsf(c.config.gyro_bias[0] - 0.2f) < 1e-5f);
  assert(strstr(replies[reply_count - 1], "EVT,4,CAL_GYRO,DONE"));
  P2P_Config before = c.config;
  P2P_Control moving = c;
  command(&moving, "CMD,6,CAL_GYRO", 4020);
  sample.gyro[0] = 10;
  P2P_ControlSample(&moving, &sample, 4040);
  assert(moving.calibration);
  P2P_ControlSample(&moving, &sample, 24020);
  assert(strstr(replies[reply_count - 1], "EVT,6,CAL_GYRO,ERR,GYRO_RATE_HIGH"));
  assert(!memcmp(&before, &moving.config, sizeof(before)));
  sample.gyro[0] = 0.2f;
  sample.accel[0] = 0.02f;
  sample.accel[2] = -0.98f;
  command(&c, "CMD,7,CAL_ACCEL", 4060);
  for (unsigned i = 0; i < 200; ++i)
    P2P_ControlSample(&c, &sample, 4080 + i * 20);
  assert(c.config.accel_calibrated &&
         fabsf(c.config.accel_bias[2] - 0.02f) < 1e-5f);
  command(&c, "CMD,8,MAG_START", 8100);
  command(&c, "CMD,9,MAG_STOP", 8120);
  assert(
      strstr(replies[reply_count - 1], "EVT,8,MAG_START,ERR,CAL_INSUFFICIENT"));
  command(&c, "CMD,10,MAG_START", 8140);
  for (unsigned i = 0; i < 256; ++i) {
    float z = 1 - 2 * (i + 0.5f) / 256, phi = i * 2.39996323f,
          r = sqrtf(1 - z * z);
    sample.mag[0] = 12 + 40 * r * cosf(phi);
    sample.mag[1] = -8 + 45 * r * sinf(phi);
    sample.mag[2] = 5 + 35 * z;
    P2P_ControlSample(&c, &sample, 8160 + i * 20);
  }
  command(&c, "CMD,11,MAG_STOP", 13280);
  assert(c.config.mag_calibrated &&
         strstr(replies[reply_count - 1], "EVT,10,MAG_START,DONE"));
  command(&c, "CMD,12,SET_MODE,9DOF", 13280);
  assert(c.config.mode == 9);
  assert(P2P_ConfigValid(&c.config));
  c.config.gain = NAN;
  assert(!P2P_ConfigValid(&c.config));
  puts("P2P parser/cache/budget/calibration checks passed (host simulation)");
}
