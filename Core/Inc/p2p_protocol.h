#ifndef P2P_PROTOCOL_H
#define P2P_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define P2P_SERIAL_MAX 180U
#define P2P_RF_MAX 220U
#define P2P_LORA_SF 7U
#define P2P_LORA_PREAMBLE 8U
/* Marker, four LE int16 (w,x,y,z x10000), optional ASCII |$ADSB suffix. */
#define P2P_QUAT_V1 0xA1U
#define P2P_QUAT_V1_LEN 9U
#define P2P_WHEEL_V1 0xA2U
#define P2P_WHEEL_V1_LEN 15U
#define P2P_WHEEL_ADCS_LEN 22U
#define P2P_WHEEL_LIGHT_LEN 28U
#define P2P_WHEEL_MANEUVER_LEN 37U
#define P2P_LIGHT_CHANNELS 4U
#define P2P_LIGHT_MAX 4095U
#define P2P_WHEEL_MAX_LEVEL 15
#define P2P_WHEEL_RPM_UNKNOWN UINT16_MAX
#define P2P_WHEEL_QUAT_VALID 0x10U
#define P2P_ADCS_GYRO_CAL 0x10U
#define P2P_ADCS_MAG_CAL 0x20U
#define P2P_ADCS_MAG_VALID 0x40U
#define P2P_ADCS_FUSION_9DOF 0x80U
#define P2P_RECENT_IDS 32U
typedef enum {
  P2P_PING,
  P2P_GET_STATUS,
  P2P_SET_BETA,
  P2P_SET_MODE,
  P2P_SET_RATE,
  P2P_RESET_FILTER,
  P2P_CAL_GYRO,
  P2P_CAL_ACCEL,
  P2P_MAG_START,
  P2P_MAG_STOP,
  P2P_SAVE_CONFIG,
  P2P_LOAD_CONFIG,
  P2P_RELAY,
  P2P_WHEEL
} P2P_CommandName;
typedef enum {
  P2P_RELAY_OFF,
  P2P_RELAY_ON,
  P2P_RELAY_STATUS
} P2P_RelayAction;
typedef enum { WHEEL_PROFILE_NONE, WHEEL_PROFILE_AIR, WHEEL_PROFILE_BIDIR } P2P_WheelProfile;
typedef enum { WHEEL_IDLE, WHEEL_ARMING, WHEEL_READY, WHEEL_RUNNING,
               WHEEL_STOPPING } P2P_WheelState;
typedef enum { DETUMBLE_NONE, DETUMBLE_ACTIVE, DETUMBLE_DONE, DETUMBLE_TIMEOUT,
               DETUMBLE_IMU, DETUMBLE_STOPPED, DETUMBLE_LIMIT, DETUMBLE_DIVERGED,
               DETUMBLE_LINK_LOST, DETUMBLE_NO_LIGHT } P2P_DetumbleState;
enum { WHEEL_CMD_ARM = 100, WHEEL_CMD_STOP, WHEEL_CMD_STATUS,
       WHEEL_CMD_PROFILE_AIR, WHEEL_CMD_PROFILE_BIDIR, WHEEL_CMD_PROFILE_OFF,
       WHEEL_CMD_ENABLE, WHEEL_CMD_DETUMBLE, WHEEL_CMD_AXIS_X_POS,
       WHEEL_CMD_AXIS_X_NEG, WHEEL_CMD_AXIS_Y_POS, WHEEL_CMD_AXIS_Y_NEG,
       WHEEL_CMD_AXIS_Z_POS, WHEEL_CMD_AXIS_Z_NEG,
       WHEEL_CMD_BENCH_ARM, WHEEL_CMD_RESULTS };
enum { WHEEL_TURN_BASE = 1000, WHEEL_LIGHT_BASE = 2000, WHEEL_RUNS_BASE = 3000 };
#define P2P_BRAKE_TRIALS 20U
typedef enum { WHEEL_MANEUVER_NONE, WHEEL_MANEUVER_BRAKE,
               WHEEL_MANEUVER_TURN, WHEEL_MANEUVER_LIGHT } P2P_WheelManeuver;
typedef struct {
  uint16_t id;
  P2P_CommandName name;
  float value;
} P2P_Command;
typedef struct {
  uint16_t id;
  uint32_t hash;
  char reply[P2P_RF_MAX + 1U];
} P2P_Recent;
typedef struct {
  P2P_Recent entries[P2P_RECENT_IDS];
  unsigned next;
} P2P_Cache;
typedef struct {
  uint32_t tokens_us, last_ms;
} P2P_AirtimeBudget;

/* Returns NULL on success, otherwise a compact protocol error. id is retained
 * when the header is valid so BAD_ARGS/UNKNOWN_CMD can be acknowledged. */
const char *P2P_ParseCommand(const char *line, size_t length, P2P_Command *out);
const char *P2P_CommandText(P2P_CommandName name);
uint32_t P2P_Hash(const void *data, size_t length);
/* 0=new, 1=duplicate, -1=ID reused for different contents. */
int P2P_CacheClaim(P2P_Cache *cache, uint16_t id, uint32_t hash,
                   const char **reply);
void P2P_CacheReply(P2P_Cache *cache, uint16_t id, const char *reply);
uint32_t P2P_TimeOnAirUs(size_t bytes);
void P2P_PackLight(uint8_t packed[6], const uint16_t samples[P2P_LIGHT_CHANNELS]);
void P2P_UnpackLight(uint16_t samples[P2P_LIGHT_CHANNELS], const uint8_t packed[6]);
void P2P_BudgetInit(P2P_AirtimeBudget *budget, uint32_t now);
bool P2P_BudgetTake(P2P_AirtimeBudget *budget, uint32_t now, size_t bytes,
                    bool priority);
#ifdef __cplusplus
}
#endif
#endif
