#ifndef ADCS_CONTROL_H
#define ADCS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "FusionMath.h"
#include "stm32l4xx_hal.h"

typedef enum {
    ADCS_AXIS_X = 0,
    ADCS_AXIS_Y,
    ADCS_AXIS_Z,
} ADCS_Axis_t;

typedef struct {
    TIM_HandleTypeDef *timer;
    uint32_t channel;
    FusionQuaternion target;
    ADCS_Axis_t axis;
    float kp;
    float kd;
    uint16_t pulse_us;
    bool armed;
    bool target_configured;
    bool closed_loop_enabled;
} ADCS_Control_t;

/* PWM esperado: contador de 1 MHz y periodo de 20 ms (50 Hz). */
HAL_StatusTypeDef ADCS_ControlInit(ADCS_Control_t *control,
                                   TIM_HandleTypeDef *timer,
                                   uint32_t channel);
void ADCS_ControlArm(ADCS_Control_t *control);
void ADCS_ControlDisarm(ADCS_Control_t *control);
void ADCS_ControlEnableClosedLoop(ADCS_Control_t *control, bool enabled);
void ADCS_ControlSetAxis(ADCS_Control_t *control, ADCS_Axis_t axis);
void ADCS_ControlSetGains(ADCS_Control_t *control, float kp, float kd);
void ADCS_ControlSetTarget(ADCS_Control_t *control, FusionQuaternion target);
void ADCS_ControlSetManual(ADCS_Control_t *control, float command);
void ADCS_ControlSetBenchPulse(ADCS_Control_t *control, uint16_t pulse_us);
void ADCS_ControlUpdate(ADCS_Control_t *control,
                        FusionQuaternion attitude,
                        FusionVector gyroscope_dps,
                        bool attitude_valid);

#endif
