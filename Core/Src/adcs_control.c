#include "adcs_control.h"

#include <math.h>

#define ADCS_PWM_MIN_US       1000U
#define ADCS_PWM_NEUTRAL_US   1500U
#define ADCS_PWM_MAX_US       2000U
#define ADCS_DEG_TO_RAD       0.01745329251994329577f

static float clamp_unit(const float value)
{
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

static void write_pulse(ADCS_Control_t *control, const uint16_t pulse_us)
{
    control->pulse_us = pulse_us;
    __HAL_TIM_SET_COMPARE(control->timer, control->channel, pulse_us);
}

void ADCS_ControlSetBenchPulse(ADCS_Control_t *control, const uint16_t pulse_us)
{
    write_pulse(control, pulse_us);
}

HAL_StatusTypeDef ADCS_ControlInit(ADCS_Control_t *control,
                                   TIM_HandleTypeDef *timer,
                                   const uint32_t channel)
{
    if ((control == NULL) || (timer == NULL)) return HAL_ERROR;

    control->timer = timer;
    control->channel = channel;
    control->target = FUSION_QUATERNION_IDENTITY;
    control->axis = ADCS_AXIS_X;
    control->kp = 0.8f;  /* Valores iniciales de banco; deben ajustarse. */
    control->kd = 0.08f;
    control->pulse_us = 0U;
    control->armed = false;
    control->target_configured = false;
    control->closed_loop_enabled = false;

    __HAL_TIM_SET_COMPARE(timer, channel, 0U);
    return HAL_TIM_PWM_Start(timer, channel);
}

void ADCS_ControlArm(ADCS_Control_t *control)
{
    if (control == NULL) return;
    control->armed = true;
    write_pulse(control, ADCS_PWM_NEUTRAL_US);
}

void ADCS_ControlDisarm(ADCS_Control_t *control)
{
    if (control == NULL) return;
    control->armed = false;
    control->closed_loop_enabled = false;
    write_pulse(control, 0U);
}

void ADCS_ControlEnableClosedLoop(ADCS_Control_t *control, const bool enabled)
{
    if (control == NULL) return;
    control->closed_loop_enabled = enabled && control->armed &&
                                   control->target_configured;
    if (!control->closed_loop_enabled && control->armed) {
        write_pulse(control, ADCS_PWM_NEUTRAL_US);
    }
}

void ADCS_ControlSetAxis(ADCS_Control_t *control, const ADCS_Axis_t axis)
{
    if ((control == NULL) || (axis > ADCS_AXIS_Z)) return;
    control->axis = axis;
}

void ADCS_ControlSetGains(ADCS_Control_t *control, const float kp, const float kd)
{
    if ((control == NULL) || !isfinite(kp) || !isfinite(kd) ||
        (kp < 0.0f) || (kd < 0.0f)) return;
    control->kp = kp;
    control->kd = kd;
}

void ADCS_ControlSetTarget(ADCS_Control_t *control, const FusionQuaternion target)
{
    if (control == NULL) return;
    const float norm_squared = (target.element.w * target.element.w) +
                               (target.element.x * target.element.x) +
                               (target.element.y * target.element.y) +
                               (target.element.z * target.element.z);
    if (!isfinite(norm_squared) || (norm_squared < 0.000001f)) return;
    const float inverse_norm = 1.0f / sqrtf(norm_squared);
    control->target.element.w = target.element.w * inverse_norm;
    control->target.element.x = target.element.x * inverse_norm;
    control->target.element.y = target.element.y * inverse_norm;
    control->target.element.z = target.element.z * inverse_norm;
    control->target_configured = true;
}

void ADCS_ControlSetManual(ADCS_Control_t *control, const float command)
{
    if ((control == NULL) || !control->armed || !isfinite(command)) return;
    const float limited = clamp_unit(command);
    const uint16_t pulse = (uint16_t)(ADCS_PWM_NEUTRAL_US + (limited * 500.0f));
    write_pulse(control, pulse);
}

void ADCS_ControlUpdate(ADCS_Control_t *control,
                        const FusionQuaternion attitude,
                        const FusionVector gyroscope_dps,
                        const bool attitude_valid)
{
    if ((control == NULL) || !control->armed) return;

    if (!attitude_valid) {
        ADCS_ControlDisarm(control);
        return;
    }
    if (!control->closed_loop_enabled) return;

    const FusionQuaternion attitude_conjugate = {
        .element = {
            .w = attitude.element.w,
            .x = -attitude.element.x,
            .y = -attitude.element.y,
            .z = -attitude.element.z,
        }
    };
    FusionQuaternion error = FusionQuaternionProduct(control->target, attitude_conjugate);

    /* q y -q representan la misma actitud: se usa siempre el camino corto. */
    if (error.element.w < 0.0f) {
        error.element.w = -error.element.w;
        error.element.x = -error.element.x;
        error.element.y = -error.element.y;
        error.element.z = -error.element.z;
    }

    float error_axis;
    float rate_dps;
    if (control->axis == ADCS_AXIS_Y) {
        error_axis = error.element.y;
        rate_dps = gyroscope_dps.axis.y;
    } else if (control->axis == ADCS_AXIS_Z) {
        error_axis = error.element.z;
        rate_dps = gyroscope_dps.axis.z;
    } else {
        error_axis = error.element.x;
        rate_dps = gyroscope_dps.axis.x;
    }

    /* Para errores pequenos, 2*q_vector aproxima el error angular en radianes. */
    const float angular_error_rad = 2.0f * error_axis;
    const float angular_rate_rad_s = rate_dps * ADCS_DEG_TO_RAD;
    const float command = (control->kp * angular_error_rad) -
                          (control->kd * angular_rate_rad_s);
    ADCS_ControlSetManual(control, command);
}
