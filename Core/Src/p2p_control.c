#include "p2p_control.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void P2P_ConfigDefaults(P2P_Config *c) {
  memset(c, 0, sizeof(*c));
  c->gain = 0.5f;
  c->mode = 6;
  c->rate_hz = 2;
  for (unsigned i = 0; i < 3; ++i)
    c->mag_scale[i] = 1;
}
bool P2P_ConfigValid(const P2P_Config *c) {
  if (!isfinite(c->gain) || c->gain < 0.001f || c->gain > 2 || c->rate_hz < 1 ||
      c->rate_hz > 10 || (c->mode != 6 && c->mode != 9) ||
      c->gyro_calibrated > 1 || c->accel_calibrated > 1 ||
      c->mag_calibrated > 1 || (c->mode == 9 && !c->mag_calibrated))
    return false;
  for (unsigned i = 0; i < 3; ++i)
    if (!isfinite(c->gyro_bias[i]) || fabsf(c->gyro_bias[i]) > 20 ||
        !isfinite(c->accel_bias[i]) || fabsf(c->accel_bias[i]) > 0.25f ||
        !isfinite(c->mag_offset[i]) || fabsf(c->mag_offset[i]) > 2000 ||
        !isfinite(c->mag_scale[i]) || c->mag_scale[i] < 0.25f ||
        c->mag_scale[i] > 4)
      return false;
  return true;
}
void P2P_ControlInit(P2P_Control *c, P2P_Emit emit, P2P_Save save,
                     P2P_Load load) {
  memset(c, 0, sizeof(*c));
  P2P_ConfigDefaults(&c->config);
  c->emit = emit;
  c->save = save;
  c->load = load;
}
void P2P_ControlEnableAutoCalibration(P2P_Control *c) {
  c->auto_calibration = true;
  P2P_Config saved;
  if (c->load && c->load(&saved) && P2P_ConfigValid(&saved)) {
    c->config = saved;
    if (c->config.mag_calibrated)
      c->config.mode = 9;
    c->reset_filter = true;
    c->revision++;
  }
}
static bool auto_gyro_active(const P2P_Control *c) {
  return c->calibration == 1 && c->calibration_id == 0;
}
static void ack(P2P_Control *c, uint16_t id, bool ok, const char *detail) {
  char line[221];
  snprintf(line, sizeof(line), "ACK,%u,%s,%s", id, ok ? "OK" : "ERR", detail);
  c->emit(line);
}
static void event(P2P_Control *c, bool ok, const char *detail) {
  const char *name = c->calibration == 1   ? "CAL_GYRO"
                     : c->calibration == 2 ? "CAL_ACCEL"
                                           : "MAG_START";
  if (c->calibration_id != 0) {
    char line[221];
    snprintf(line, sizeof(line), "EVT,%u,%s,%s,%s", c->calibration_id, name,
             ok ? "DONE" : "ERR", detail);
    c->emit(line);
  }
  c->calibration = 0;
}
static void reset_cal_samples(P2P_Control *c) {
  c->count = 0;
  memset(c->sum, 0, sizeof(c->sum));
  memset(c->sum_sq, 0, sizeof(c->sum_sq));
}

static void reject_cal_sample(P2P_Control *c, const char *reason) {
  if (c->calibration == 1) {
    c->gyro_rejection = reason;
    reset_cal_samples(c);
  } else {
    event(c, false, "CAL_UNSTABLE");
  }
}
void P2P_ControlCommand(P2P_Control *c, const P2P_Command *cmd, uint32_t now) {
  char detail[170];
  if (cmd->name == P2P_PING) {
    snprintf(detail, sizeof(detail), "STM32L476_P2P_CMD_V1_UP_%lu",
             (unsigned long)now);
    ack(c, cmd->id, true, detail);
    return;
  }
  if (cmd->name == P2P_GET_STATUS) {
    snprintf(detail, sizeof(detail),
             "AHRS_FUSION_MODE_%lu_GAIN_milli_%ld_RATE_REQ_%lu_DUTY_1pct_IMU_%"
             "u_MAG_%u_CAL_%d_G%luA%luM%lu",
             (unsigned long)c->config.mode, (long)(c->config.gain * 1000),
             (unsigned long)c->config.rate_hz, c->imu_ok ? 1 : 0,
             c->mag_ok && now - c->last_mag_ms < 250 ? 1 : 0, c->calibration,
             (unsigned long)c->config.gyro_calibrated,
             (unsigned long)c->config.accel_calibrated,
             (unsigned long)c->config.mag_calibrated);
    ack(c, cmd->id, true, detail);
    return;
  }
  if (c->calibration && !auto_gyro_active(c) && cmd->name != P2P_MAG_STOP) {
    ack(c, cmd->id, false, "BUSY");
    return;
  }
  if (!c->imu_ok && cmd->name != P2P_SAVE_CONFIG) {
    ack(c, cmd->id, false, "NOT_READY");
    return;
  }
  switch (cmd->name) {
  case P2P_SET_BETA:
    c->config.gain = cmd->value;
    c->revision++;
    ack(c, cmd->id, true, "APPLIED_FUSION_GAIN");
    break;
  case P2P_SET_RATE:
    c->config.rate_hz = (uint32_t)cmd->value;
    c->revision++;
    ack(c, cmd->id, true, "APPLIED_REQUEST_DUTY_LIMITED");
    break;
  case P2P_SET_MODE:
    if (cmd->value == 9 && (!c->mag_ok || now - c->last_mag_ms >= 250 ||
                            !c->config.mag_calibrated)) {
      ack(c, cmd->id, false, "MAG_UNAVAILABLE");
      break;
    }
    c->config.mode = (uint32_t)cmd->value;
    c->revision++;
    ack(c, cmd->id, true, "APPLIED");
    break;
  case P2P_RESET_FILTER:
    c->reset_filter = true;
    c->revision++;
    ack(c, cmd->id, true, "APPLIED");
    break;
  case P2P_CAL_GYRO:
  case P2P_CAL_ACCEL:
  case P2P_MAG_START:
    if (!c->imu_ok) {
      ack(c, cmd->id, false, "NOT_READY");
      break;
    }
    if (cmd->name == P2P_MAG_START &&
        (!c->mag_ok || now - c->last_mag_ms >= 250)) {
      ack(c, cmd->id, false, "MAG_UNAVAILABLE");
      break;
    }
    c->calibration = cmd->name == P2P_CAL_GYRO    ? 1
                     : cmd->name == P2P_CAL_ACCEL ? 2
                                                  : 3;
    c->calibration_id = cmd->id;
    c->started_ms = now;
    c->gyro_rejection = NULL;
    reset_cal_samples(c);
    for (unsigned i = 0; i < 3; ++i) {
      c->min_mag[i] = 1e9f;
      c->max_mag[i] = -1e9f;
    }
    ack(c, cmd->id, true, "STARTED");
    break;
  case P2P_MAG_STOP: {
    if (c->calibration != 3) {
      ack(c, cmd->id, false, "NOT_READY");
      break;
    }
    /* MAG_START is a deferred job under its original ID; MAG_STOP gets its
     * own ACK, and the terminal EVT identifies the original MAG_START ID. */
    uint16_t stop_id = cmd->id;
    if (c->count < 200U) {
      ack(c, stop_id, false, "CAL_INSUFFICIENT");
      event(c, false, "CAL_INSUFFICIENT");
      break;
    }
    float offset[3], radius[3], mean_radius = 0;
    bool good = true;
    for (unsigned a = 0; a < 3; ++a) {
      offset[a] = (c->max_mag[a] + c->min_mag[a]) * 0.5f;
      radius[a] = (c->max_mag[a] - c->min_mag[a]) * 0.5f;
      if (radius[a] < 10 || radius[a] > 500)
        good = false;
      mean_radius += radius[a] / 3;
    }
    unsigned octants[8] = {0};
    double sum = 0, sum_sq = 0;
    unsigned n = c->count < 256 ? c->count : 256;
    if (good)
      for (unsigned i = 0; i < n; ++i) {
        float r2 = 0;
        unsigned oct = 0;
        for (unsigned a = 0; a < 3; ++a) {
          float delta = c->mag_samples[i][a] - offset[a];
          if (delta >= 0)
            oct |= 1U << a;
          r2 += (delta / radius[a]) * (delta / radius[a]);
        }
        float r = sqrtf(r2);
        sum += r;
        sum_sq += r * r;
        octants[oct]++;
      }
    for (unsigned i = 0; i < 8; ++i)
      if (octants[i] < 4)
        good = false;
    if (good && (sum / n < 0.7 || sum / n > 1.3 ||
                 sum_sq / n - (sum / n) * (sum / n) > 0.04))
      good = false;
    for (unsigned a = 0; a < 3 && good; ++a)
      if (mean_radius / radius[a] < 0.25f || mean_radius / radius[a] > 4)
        good = false;
    if (!good) {
      ack(c, stop_id, false, "CAL_INSUFFICIENT");
      event(c, false, "CAL_INSUFFICIENT");
      break;
    }
    for (unsigned a = 0; a < 3; ++a) {
      c->config.mag_offset[a] = offset[a];
      c->config.mag_scale[a] = mean_radius / radius[a];
    }
    c->config.mag_calibrated = 1;
    if (c->auto_calibration)
      c->config.mode = 9;
    c->revision++;
    ack(c, stop_id, true, "APPLIED_DIAGONAL_SOFTIRON");
    event(c, true, "DIAGONAL_SOFTIRON");
    break;
  }
  case P2P_SAVE_CONFIG: {
    bool saved = c->save && c->save(&c->config, now);
    ack(c, cmd->id, saved, saved ? "SAVED" : "FLASH_ERROR");
    break;
  }
  case P2P_LOAD_CONFIG: {
    P2P_Config next;
    if (!c->load || !c->load(&next) || !P2P_ConfigValid(&next)) {
      ack(c, cmd->id, false, "FLASH_ERROR");
      break;
    }
    if (next.mode == 9 && (!c->mag_ok || now - c->last_mag_ms >= 250)) {
      ack(c, cmd->id, false, "MAG_UNAVAILABLE");
      break;
    }
    c->config = next;
    if (auto_gyro_active(c))
      c->calibration = 0;
    c->reset_filter = true;
    c->revision++;
    ack(c, cmd->id, true, "APPLIED");
    break;
  }
  default:
    ack(c, cmd->id, false, "UNKNOWN_CMD");
    break;
  }
}
void P2P_ControlSample(P2P_Control *c, const P2P_Sample *s, uint32_t now) {
  c->imu_ok = s->imu_ok;
  if (s->mag_ok) {
    c->mag_ok = true;
    c->last_mag_ms = now;
  }
  if (now - c->last_mag_ms >= 250)
    c->mag_ok = false;
  if (!c->calibration && c->auto_calibration && !c->config.gyro_calibrated && s->imu_ok) {
    c->calibration = 1;
    c->calibration_id = 0;
    c->started_ms = now;
    c->gyro_rejection = NULL;
    reset_cal_samples(c);
  }
  if (!c->calibration)
    return;
  const bool gyro = c->calibration == 1;
  if (!auto_gyro_active(c) && now - c->started_ms >= (gyro ? 20000U : 120001U)) {
    event(c, false, gyro && c->gyro_rejection ? c->gyro_rejection : "CAL_INSUFFICIENT");
    return;
  }
  if (!s->imu_ok) {
    reject_cal_sample(c, "IMU_NO_SAMPLE");
    return;
  }
  if (c->calibration == 3) {
    if (!s->mag_ok)
      return;
    unsigned slot = c->count % 256U;
    for (unsigned a = 0; a < 3; ++a) {
      float v = s->mag[a];
      if (!isfinite(v)) {
        event(c, false, "CAL_UNSTABLE");
        return;
      }
      c->mag_samples[slot][a] = v;
      if (v < c->min_mag[a])
        c->min_mag[a] = v;
      if (v > c->max_mag[a])
        c->max_mag[a] = v;
    }
    c->count++;
    return;
  }
  float norm = 0;
  for (unsigned a = 0; a < 3; ++a) {
    if (!isfinite(s->gyro[a]) || !isfinite(s->accel[a])) {
      reject_cal_sample(c, "IMU_INVALID_SAMPLE");
      return;
    }
    if (fabsf(s->gyro[a]) > 3) {
      reject_cal_sample(c, "GYRO_RATE_HIGH");
      return;
    }
    norm += s->accel[a] * s->accel[a];
  }
  if (norm < (gyro ? 0.81f : 0.9f) || norm > (gyro ? 1.21f : 1.1f) ||
      (c->calibration == 2 &&
       (fabsf(s->accel[0]) > 0.1f || fabsf(s->accel[1]) > 0.1f ||
        fabsf(s->accel[2]) < 0.9f))) {
    reject_cal_sample(c, "ACCEL_NOT_NEAR_1G");
    return;
  }
  for (unsigned a = 0; a < 3; ++a) {
    float v = c->calibration == 1 ? s->gyro[a] : s->accel[a];
    c->sum[a] += v;
    c->sum_sq[a] += v * v;
  }
  if (++c->count < 200U)
    return;
  for (unsigned a = 0; a < 3; ++a) {
    double mean = c->sum[a] / c->count,
           var = c->sum_sq[a] / c->count - mean * mean;
    if (var > (c->calibration == 1 ? 0.04 : 0.0004)) {
      reject_cal_sample(c, "GYRO_VARIANCE_HIGH");
      return;
    }
  }
  for (unsigned a = 0; a < 3; ++a) {
    float mean = (float)(c->sum[a] / c->count);
    if (c->calibration == 1)
      c->config.gyro_bias[a] = mean;
    else
      c->config.accel_bias[a] =
          mean - (a == 2 ? (mean >= 0 ? 1.0f : -1.0f) : 0);
  }
  if (c->calibration == 1)
    c->config.gyro_calibrated = 1;
  else
    c->config.accel_calibrated = 1;
  c->reset_filter = true;
  c->revision++;
  event(c, true, c->calibration == 1 ? "BIAS_APPLIED" : "LEVEL_BIAS_ONLY");
}
void P2P_ApplyCalibration(const P2P_Config *c, P2P_Sample *s) {
  for (unsigned a = 0; a < 3; ++a) {
    s->accel[a] -= c->accel_bias[a];
    s->gyro[a] -= c->gyro_bias[a];
    s->mag[a] = (s->mag[a] - c->mag_offset[a]) * c->mag_scale[a];
  }
}
