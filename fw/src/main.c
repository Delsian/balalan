#include "balalan.h"
#include <dk_buttons_and_leds.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static void on_button_changed_handler(uint32_t button_state, uint32_t has_changed)
{
    LOG_DBG("Board button pressed");

    /* Button 1: Start/stop scanning */
    if (has_changed & DK_BTN1_MSK) {
        if (button_state & DK_BTN1_MSK) {
            if (!default_conn) {
                LOG_INF("Button 1 pressed - connection handling done by joys module");
            }
        }
    }

    /* Button 2: Disconnect */
    if (has_changed & DK_BTN2_MSK) {
        if (button_state & DK_BTN2_MSK) {
            if (default_conn) {
                LOG_INF("Disconnecting from joystick");
                bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            }
        }
    }

        /* Button 3: Test fan control */
    // if (has_changed & DK_BTN3_MSK) {
    //     if (button_state & DK_BTN3_MSK) {
    //         static uint8_t test_speed = 0;
    //         test_speed += 50;
    //         if (test_speed > 255) test_speed = 0;
    //         LOG_INF("Testing fan at speed %u", test_speed);
    //         fan_set_speed(test_speed);
    //     }
    // }

    /* Button 4: Get fan RPM */
    if (has_changed & DK_BTN4_MSK) {
        if (button_state & DK_BTN4_MSK) {
            uint16_t rpm = fan_get_rpm();
            LOG_INF("Current fan RPM: %u", rpm);
        }
    }

}

int main(void)
{
    int err;

    LOG_INF("Starting BLE Joystick example");

    err = dk_leds_init();
    if (err) {
        LOG_ERR("LEDs init failed (err %d)", err);
        return err;
    }

    fan_init();

    // err = dk_buttons_init(on_button_changed_handler);
    // if (err) {
    //     LOG_ERR("Buttons init failed (err %d)", err);
    //     return err;
    // }

    /* Initialize IMU sensors (MPU6050 + HMC5883) */
    imu_init();

    joys_init();

    while (1) {
        k_sleep(K_FOREVER);
    }

    return 0;
}