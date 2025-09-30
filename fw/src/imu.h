#pragma once

#include <zephyr/types.h>
#include <zephyr/device.h>

/* MPU6050 definitions */
#define MPU6050_ADDR 0x68
#define MPU6050_REG_SMPLRT_DIV      0x19
#define MPU6050_REG_CONFIG          0x1A
#define MPU6050_REG_GYRO_CONFIG     0x1B
#define MPU6050_REG_ACCEL_CONFIG    0x1C
#define MPU6050_REG_INT_PIN_CFG     0x37
#define MPU6050_REG_ACCEL_XOUT_H    0x3B
#define MPU6050_REG_TEMP_OUT_H      0x41
#define MPU6050_REG_GYRO_XOUT_H     0x43
#define MPU6050_REG_USER_CTRL       0x6A
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_I2C_MST_CTRL    0x24
#define MPU6050_REG_I2C_SLV0_ADDR   0x25
#define MPU6050_REG_I2C_SLV0_REG    0x26
#define MPU6050_REG_I2C_SLV0_CTRL   0x27
#define MPU6050_REG_EXT_SENS_DATA   0x49

#define MPU6050_INT_PIN_CFG_BYPASS  0x02
#define MPU6050_USER_CTRL_I2C_MST_EN 0x20
#define MPU6050_I2C_SLV_EN          0x80
#define MPU6050_I2C_SLV_READ        0x80

/* HMC5883 definitions */
#define HMC5883_ADDR 0x1E
#define HMC5883_REG_CONFIG_A    0x00
#define HMC5883_REG_CONFIG_B    0x01
#define HMC5883_REG_MODE        0x02
#define HMC5883_REG_DATA_X_MSB  0x03
#define HMC5883_MODE_CONTINUOUS 0x00

/* Data structures */
struct imu_data {
    int16_t accel_x, accel_y, accel_z;
    int16_t temp;
    int16_t gyro_x, gyro_y, gyro_z;
    int16_t mag_x, mag_y, mag_z;
    int64_t timestamp;
};

/* Public API */
void imu_init(void);