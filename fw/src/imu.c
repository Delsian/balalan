#include "imu.h"
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(imu, LOG_LEVEL_INF);

ZBUS_CHAN_DEFINE(imu_chan, struct imu_data, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(.timestamp = 0));

static const struct device *i2c_dev;

/* MPU6050 low-level functions */
static int mpu6050_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_write(i2c_dev, buf, sizeof(buf), MPU6050_ADDR);
}

static int mpu6050_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_write_read(i2c_dev, MPU6050_ADDR, &reg, 1, buf, len);
}

static int mpu6050_init(void)
{
    mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00);
    k_sleep(K_MSEC(100));
    mpu6050_write_reg(MPU6050_REG_SMPLRT_DIV, 0x07);
    mpu6050_write_reg(MPU6050_REG_CONFIG, 0x00);
    mpu6050_write_reg(MPU6050_REG_GYRO_CONFIG, 0x00);
    mpu6050_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x00);
    LOG_INF("MPU6050 initialized");
    return 0;
}

static int mpu6050_enable_bypass(bool enable)
{
    return mpu6050_write_reg(MPU6050_REG_INT_PIN_CFG,
                            enable ? MPU6050_INT_PIN_CFG_BYPASS : 0x00);
}

static int mpu6050_setup_aux_i2c(uint8_t slave_addr, uint8_t slave_reg, uint8_t num_bytes)
{
    mpu6050_write_reg(MPU6050_REG_USER_CTRL, 0x00);
    k_sleep(K_MSEC(10));
    mpu6050_write_reg(MPU6050_REG_INT_PIN_CFG, 0x00);
    mpu6050_write_reg(MPU6050_REG_I2C_MST_CTRL, 0x4D);
    mpu6050_write_reg(MPU6050_REG_I2C_SLV0_ADDR, slave_addr | MPU6050_I2C_SLV_READ);
    mpu6050_write_reg(MPU6050_REG_I2C_SLV0_REG, slave_reg);
    mpu6050_write_reg(MPU6050_REG_I2C_SLV0_CTRL, MPU6050_I2C_SLV_EN | num_bytes);
    k_sleep(K_MSEC(10));
    mpu6050_write_reg(MPU6050_REG_USER_CTRL, MPU6050_USER_CTRL_I2C_MST_EN);
    LOG_INF("Auxiliary I2C configured");
    return 0;
}

/* HMC5883 functions */
static int hmc5883_init_via_mpu(void)
{
    uint8_t buf[2];

    buf[0] = HMC5883_REG_CONFIG_A;
    buf[1] = 0x70; /* 8 samples averaged, 15Hz, normal */
    i2c_write(i2c_dev, buf, 2, HMC5883_ADDR);

    buf[0] = HMC5883_REG_CONFIG_B;
    buf[1] = 0x20; /* Gain ±1.3 Ga */
    i2c_write(i2c_dev, buf, 2, HMC5883_ADDR);

    buf[0] = HMC5883_REG_MODE;
    buf[1] = HMC5883_MODE_CONTINUOUS;
    i2c_write(i2c_dev, buf, 2, HMC5883_ADDR);

    k_sleep(K_MSEC(50));

    mpu6050_setup_aux_i2c(HMC5883_ADDR, HMC5883_REG_DATA_X_MSB, 6);
    k_sleep(K_MSEC(100));

    LOG_INF("HMC5883 initialized via MPU6050");
    return 0;
}

/* Combined read function */
static int imu_read_all(struct imu_data *data)
{
    uint8_t buf[14];

    /* Read MPU6050 data */
    int ret = mpu6050_read_regs(MPU6050_REG_ACCEL_XOUT_H, buf, sizeof(buf));
    if (ret < 0) return ret;

    data->accel_x = sys_get_be16(&buf[0]);
    data->accel_y = sys_get_be16(&buf[2]);
    data->accel_z = sys_get_be16(&buf[4]);
    data->temp = sys_get_be16(&buf[6]);
    data->gyro_x = sys_get_be16(&buf[8]);
    data->gyro_y = sys_get_be16(&buf[10]);
    data->gyro_z = sys_get_be16(&buf[12]);

    /* Read HMC5883 data via auxiliary I2C */
    uint8_t mag_buf[6];
    ret = mpu6050_read_regs(MPU6050_REG_EXT_SENS_DATA, mag_buf, 6);
    if (ret < 0) return ret;

    /* HMC5883L data order: X, Z, Y */
    data->mag_x = sys_get_be16(&mag_buf[0]);
    data->mag_z = sys_get_be16(&mag_buf[2]);
    data->mag_y = sys_get_be16(&mag_buf[4]);

    data->timestamp = k_uptime_get();

    return 0;
}

/* IMU thread */
#define IMU_THREAD_STACK_SIZE 2048
#define IMU_THREAD_PRIORITY 5

static void imu_thread_fn(void *arg1, void *arg2, void *arg3)
{
    struct imu_data data;

    while (1) {
        if (imu_read_all(&data) == 0) {
            zbus_chan_pub(&imu_chan, &data, K_MSEC(100));

            static int count = 0;
            if (++count >= 10) {
                count = 0;
                LOG_DBG("A[%6d %6d %6d] G[%6d %6d %6d] M[%6d %6d %6d]",
                        data.accel_x, data.accel_y, data.accel_z,
                        data.gyro_x, data.gyro_y, data.gyro_z,
                        data.mag_x, data.mag_y, data.mag_z);
            }
        }

        k_sleep(K_MSEC(10));
    }
}

K_THREAD_DEFINE(imu_thread, IMU_THREAD_STACK_SIZE, imu_thread_fn,
                NULL, NULL, NULL, IMU_THREAD_PRIORITY, 0, 0);

/* Public initialization function */
void imu_init(void)
{
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c21));
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device not ready");
        return;
    }

    mpu6050_init();
    mpu6050_enable_bypass(true);
    k_sleep(K_MSEC(50));
    hmc5883_init_via_mpu();

    LOG_INF("IMU initialized");
}