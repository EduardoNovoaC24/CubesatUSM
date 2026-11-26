/*
 * MPU9250 + AK8963 driver for STM32 HAL over I2C.
 * Register setup adapted from https://github.com/desertkun/MPU9250
 * Upstream commit: 4d98a485df9101856ba122971afb1be073bf1300
 */
#include "mpu9250.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define REG_SMPLRT_DIV       0x19U
#define REG_CONFIG           0x1AU
#define REG_GYRO_CONFIG      0x1BU
#define REG_ACCEL_CONFIG     0x1CU
#define REG_ACCEL_CONFIG2    0x1DU
#define REG_INT_PIN_CFG      0x37U
#define REG_ACCEL_XOUT_H     0x3BU
#define REG_USER_CTRL        0x6AU
#define REG_PWR_MGMT_1       0x6BU
#define REG_PWR_MGMT_2       0x6CU
#define REG_WHO_AM_I         0x75U

#define AK8963_WHO_AM_I      0x00U
#define AK8963_ST1           0x02U
#define AK8963_CNTL1         0x0AU
#define AK8963_CNTL2         0x0BU
#define AK8963_ASAX          0x10U

#define I2C_TIMEOUT_MS       100U

static I2C_HandleTypeDef *mpu_i2c;
static float mag_adjustment[3] = {1.0f, 1.0f, 1.0f};
static bool magnetometer_available;

static void task_delay(uint32_t milliseconds)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(milliseconds));
    } else {
        HAL_Delay(milliseconds);
    }
}

static HAL_StatusTypeDef write_register(uint16_t device, uint8_t address, uint8_t value)
{
    return HAL_I2C_Mem_Write(mpu_i2c, device, address, I2C_MEMADD_SIZE_8BIT,
                             &value, 1U, I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef read_registers(uint16_t device, uint8_t address,
                                        uint8_t *data, uint16_t length)
{
    return HAL_I2C_Mem_Read(mpu_i2c, device, address, I2C_MEMADD_SIZE_8BIT,
                            data, length, I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef MPU9250_ReadWhoAmI(uint8_t *who_am_i)
{
    if ((mpu_i2c == NULL) || (who_am_i == NULL)) return HAL_ERROR;
    return read_registers(MPU9250_I2C_ADDRESS, REG_WHO_AM_I, who_am_i, 1U);
}

HAL_StatusTypeDef MPU9250_ReadMagWhoAmI(uint8_t *who_am_i)
{
    if ((mpu_i2c == NULL) || (who_am_i == NULL)) return HAL_ERROR;
    return read_registers(AK8963_I2C_ADDRESS, AK8963_WHO_AM_I, who_am_i, 1U);
}

bool MPU9250_HasMagnetometer(void)
{
    return magnetometer_available;
}

HAL_StatusTypeDef MPU9250_Init(I2C_HandleTypeDef *hi2c)
{
    uint8_t value = 0U;
    uint8_t asa[3];
    mpu_i2c = hi2c;
    magnetometer_available = false;
    if (mpu_i2c == NULL) return HAL_ERROR;

    if (write_register(MPU9250_I2C_ADDRESS, REG_PWR_MGMT_1, 0x80U) != HAL_OK) return HAL_ERROR;
    task_delay(100U);
    if (write_register(MPU9250_I2C_ADDRESS, REG_PWR_MGMT_1, 0x01U) != HAL_OK) return HAL_ERROR;
    if (write_register(MPU9250_I2C_ADDRESS, REG_PWR_MGMT_2, 0x00U) != HAL_OK) return HAL_ERROR;
    task_delay(10U);

    if ((MPU9250_ReadWhoAmI(&value) != HAL_OK) ||
        ((value != 0x70U) && (value != 0x71U) && (value != 0x73U))) return HAL_ERROR;

    /* +/-2 g, +/-250 dps, DLPF 41 Hz y muestreo a 50 Hz. */
    if (write_register(MPU9250_I2C_ADDRESS, REG_ACCEL_CONFIG, 0x00U) != HAL_OK) return HAL_ERROR;
    if (write_register(MPU9250_I2C_ADDRESS, REG_GYRO_CONFIG, 0x08U) != HAL_OK) return HAL_ERROR;
    if (write_register(MPU9250_I2C_ADDRESS, REG_ACCEL_CONFIG2, 0x03U) != HAL_OK) return HAL_ERROR;
    if (write_register(MPU9250_I2C_ADDRESS, REG_CONFIG, 0x03U) != HAL_OK) return HAL_ERROR;
    if (write_register(MPU9250_I2C_ADDRESS, REG_SMPLRT_DIV, 19U) != HAL_OK) return HAL_ERROR;

    /* El MPU6500 (ID 0x70) no incorpora AK8963: trabaja como IMU 6-DOF. */
    if (value == 0x70U) return HAL_OK;

    /* En MPU9250/9255 expone directamente el AK8963 en I2C. */
    if (write_register(MPU9250_I2C_ADDRESS, REG_USER_CTRL, 0x00U) != HAL_OK) return HAL_ERROR;
    if (write_register(MPU9250_I2C_ADDRESS, REG_INT_PIN_CFG, 0x02U) != HAL_OK) return HAL_ERROR;
    task_delay(10U);

    if ((MPU9250_ReadMagWhoAmI(&value) != HAL_OK) || (value != 0x48U)) {
        /* Acelerómetro y giróscopo continúan disponibles aunque falle el mag. */
        return HAL_OK;
    }
    magnetometer_available = true;
    if (write_register(AK8963_I2C_ADDRESS, AK8963_CNTL2, 0x01U) != HAL_OK) return HAL_ERROR;
    task_delay(100U);
    if (write_register(AK8963_I2C_ADDRESS, AK8963_CNTL1, 0x00U) != HAL_OK) return HAL_ERROR;
    task_delay(10U);
    if (write_register(AK8963_I2C_ADDRESS, AK8963_CNTL1, 0x0FU) != HAL_OK) return HAL_ERROR;
    task_delay(10U);
    if (read_registers(AK8963_I2C_ADDRESS, AK8963_ASAX, asa, sizeof(asa)) != HAL_OK) return HAL_ERROR;
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        mag_adjustment[axis] = (((float)asa[axis] - 128.0f) / 256.0f) + 1.0f;
    }
    if (write_register(AK8963_I2C_ADDRESS, AK8963_CNTL1, 0x00U) != HAL_OK) return HAL_ERROR;
    task_delay(10U);
    /* 16 bits, medición continua a 100 Hz. */
    return write_register(AK8963_I2C_ADDRESS, AK8963_CNTL1, 0x16U);
}

HAL_StatusTypeDef MPU9250_ReadRaw(MPU9250_RawData_t *data)
{
    uint8_t imu[14];
    uint8_t mag[8];
    if ((data == NULL) ||
        (read_registers(MPU9250_I2C_ADDRESS, REG_ACCEL_XOUT_H, imu, sizeof(imu)) != HAL_OK)) {
        return HAL_ERROR;
    }

    data->ax = (int16_t)((uint16_t)imu[0] << 8U | imu[1]);
    data->ay = (int16_t)((uint16_t)imu[2] << 8U | imu[3]);
    data->az = (int16_t)((uint16_t)imu[4] << 8U | imu[5]);
    data->temperature = (int16_t)((uint16_t)imu[6] << 8U | imu[7]);
    data->gx = (int16_t)((uint16_t)imu[8] << 8U | imu[9]);
    data->gy = (int16_t)((uint16_t)imu[10] << 8U | imu[11]);
    data->gz = (int16_t)((uint16_t)imu[12] << 8U | imu[13]);

    data->mx = 0;
    data->my = 0;
    data->mz = 0;
    data->magnetometer_valid = false;
    if (magnetometer_available &&
        (read_registers(AK8963_I2C_ADDRESS, AK8963_ST1, mag, sizeof(mag)) == HAL_OK)) {
        const bool ready = (mag[0] & 0x01U) != 0U;
        const bool overflow = (mag[7] & 0x08U) != 0U;
        if (ready && !overflow) {
            data->mx = (int16_t)((uint16_t)mag[2] << 8U | mag[1]);
            data->my = (int16_t)((uint16_t)mag[4] << 8U | mag[3]);
            data->mz = (int16_t)((uint16_t)mag[6] << 8U | mag[5]);
            data->magnetometer_valid = true;
        }
    }
    return HAL_OK;
}

void MPU9250_Convert(const MPU9250_RawData_t *raw, MPU9250_Data_t *data)
{
    if ((raw == NULL) || (data == NULL)) return;
    memset(data, 0, sizeof(*data));
    data->ax = (float)raw->ax / MPU9250_ACCEL_LSB_PER_G;
    data->ay = -(float)raw->ay / MPU9250_ACCEL_LSB_PER_G;
    data->az = (float)raw->az / MPU9250_ACCEL_LSB_PER_G;
    data->gx = (float)raw->gx / MPU9250_GYRO_LSB_PER_DPS;
    data->gy = -(float)raw->gy / MPU9250_GYRO_LSB_PER_DPS;
    data->gz = (float)raw->gz / MPU9250_GYRO_LSB_PER_DPS;
    data->mx = (float)raw->mx * mag_adjustment[0] * MPU9250_MAG_UT_PER_LSB;
    data->my = -(float)raw->my * mag_adjustment[1] * MPU9250_MAG_UT_PER_LSB;
    data->mz = (float)raw->mz * mag_adjustment[2] * MPU9250_MAG_UT_PER_LSB;
    data->temperature_c = ((float)raw->temperature / 333.87f) + 21.0f;
    data->magnetometer_valid = raw->magnetometer_valid;
}
