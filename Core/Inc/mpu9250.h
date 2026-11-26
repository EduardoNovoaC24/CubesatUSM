#ifndef MPU9250_H
#define MPU9250_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* Adaptado de desertkun/MPU9250. El transporte local usa I2C1. */
#define MPU9250_I2C_ADDRESS          (0x68U << 1U)
#define AK8963_I2C_ADDRESS           (0x0CU << 1U)
#define MPU9250_ACCEL_LSB_PER_G       16384.0f
#define MPU9250_GYRO_RANGE_DPS         500.0f
#define MPU9250_GYRO_LSB_PER_DPS        65.5f
#define MPU9250_MAG_UT_PER_LSB            0.15f

typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t temperature;
    int16_t gx;
    int16_t gy;
    int16_t gz;
    int16_t mx;
    int16_t my;
    int16_t mz;
    bool magnetometer_valid;
} MPU9250_RawData_t;

typedef struct {
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
    float mx;
    float my;
    float mz;
    float temperature_c;
    bool magnetometer_valid;
} MPU9250_Data_t;

HAL_StatusTypeDef MPU9250_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef MPU9250_ReadWhoAmI(uint8_t *who_am_i);
HAL_StatusTypeDef MPU9250_ReadMagWhoAmI(uint8_t *who_am_i);
HAL_StatusTypeDef MPU9250_ReadRaw(MPU9250_RawData_t *data);
bool MPU9250_HasMagnetometer(void);
void MPU9250_Convert(const MPU9250_RawData_t *raw, MPU9250_Data_t *data);

#endif /* MPU9250_H */
