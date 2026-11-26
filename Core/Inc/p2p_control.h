#ifndef P2P_CONTROL_H
#define P2P_CONTROL_H
#include "p2p_protocol.h"

typedef struct {
  float gain;
  uint32_t rate_hz, mode;
  float gyro_bias[3], accel_bias[3], mag_offset[3], mag_scale[3];
  uint32_t gyro_calibrated, accel_calibrated, mag_calibrated;
} P2P_Config;
typedef struct {
  float accel[3], gyro[3], mag[3];
  bool imu_ok, mag_ok;
} P2P_Sample;
typedef bool (*P2P_Emit)(const char *line);
typedef bool (*P2P_Save)(const P2P_Config *config, uint32_t now);
typedef bool (*P2P_Load)(P2P_Config *config);
typedef struct {
  P2P_Config config;
  uint32_t revision;
  bool reset_filter, imu_ok, mag_ok, auto_calibration;
  uint32_t last_mag_ms;
  int calibration; /* 0=idle, 1=gyro, 2=level accel, 3=mag */
  uint16_t calibration_id;
  uint32_t started_ms, count;
  const char *gyro_rejection;
  double sum[3], sum_sq[3];
  float min_mag[3], max_mag[3], mag_samples[256][3];
  P2P_Emit emit;
  P2P_Save save;
  P2P_Load load;
} P2P_Control;
void P2P_ConfigDefaults(P2P_Config *config);
bool P2P_ConfigValid(const P2P_Config *config);
void P2P_ControlInit(P2P_Control *control, P2P_Emit emit, P2P_Save save,
                     P2P_Load load);
void P2P_ControlEnableAutoCalibration(P2P_Control *control);
void P2P_ControlCommand(P2P_Control *control, const P2P_Command *command,
                        uint32_t now);
void P2P_ControlSample(P2P_Control *control, const P2P_Sample *sample,
                       uint32_t now);
void P2P_ApplyCalibration(const P2P_Config *config, P2P_Sample *sample);
bool P2P_FlashSave(const P2P_Config *config, uint32_t now);
bool P2P_FlashLoad(P2P_Config *config);
#endif
