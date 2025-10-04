#include "fan.h"
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <dk_buttons_and_leds.h>

LOG_MODULE_REGISTER(fan, LOG_LEVEL_INF);

/* Button subscriber */
ZBUS_SUBSCRIBER_DEFINE(button_sub, 4);

/* ZBUS */
ZBUS_CHAN_DEFINE(fan_chan, struct fan_data, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
                 ZBUS_MSG_INIT(0));

struct button_msg {
    uint16_t buttons;
};

ZBUS_CHAN_DEFINE(button_chan, struct button_msg, NULL, NULL,
                 ZBUS_OBSERVERS(button_sub), ZBUS_MSG_INIT(.buttons = 0));

/* Hardware */
static const struct pwm_dt_spec fan_pwm = PWM_DT_SPEC_GET(DT_NODELABEL(fan_pwm));
static const struct gpio_dt_spec tach = GPIO_DT_SPEC_GET(DT_NODELABEL(fan_tach_pin), gpios);

/* State */
static struct gpio_callback tach_cb;
static volatile uint32_t pulses = 0;
static volatile int64_t last_pulse = 0;
static struct fan_data state = {0};
static uint8_t fan_speed = 0;

/* Tachometer ISR */
static void tach_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    pulses++;
    last_pulse = k_uptime_get();
    dk_set_led(DK_LED4, pulses & 1);
}

/* Calculate RPM */
static uint16_t get_rpm(void)
{
    static uint32_t prev_pulses = 0;
    static int64_t prev_time = 0;

    int64_t now = k_uptime_get();
    int64_t dt = now - prev_time;

    if (dt < 1000) return state.rpm;

    uint32_t dp = pulses - prev_pulses;
    uint16_t rpm = (dp * 60000) / (2 * dt);

    prev_pulses = pulses;
    prev_time = now;

    return rpm;
}

/* Set fan speed (0-255) */
void fan_set_speed(uint8_t speed)
{
    uint32_t period = NSEC_PER_SEC / FAN_PWM_FREQUENCY_HZ;
    uint32_t pulse;
    uint32_t duty;

    if (speed == 0) {
        /* Keep PWM on at 1% duty when idle */
        duty = 1;
        pulse = (period * duty) / 100;
    } else {
        duty = FAN_MIN_DUTY_PERCENT + ((speed * (100 - FAN_MIN_DUTY_PERCENT)) / 255);
        pulse = (period * duty) / 100;
    }

    pwm_set_dt(&fan_pwm, period, pulse);

    state.speed_cmd = speed;
    state.duty_percent = duty;
    state.running = (speed > 0);
}

uint16_t fan_get_rpm(void)
{
    return state.rpm;
}

/* Monitor thread */
static void monitor_thread(void *a, void *b, void *c)
{
    while (1) {
        state.rpm = get_rpm();
        state.timestamp = k_uptime_get();
        zbus_chan_pub(&fan_chan, &state, K_MSEC(100));

        if (state.running && state.rpm < 100 && k_uptime_get() - last_pulse > 3000) {
            LOG_WRN("Fan stalled");
            dk_set_led(DK_LED4, 0);
        }
        if (!state.running) dk_set_led(DK_LED4, 0);

        k_sleep(K_MSEC(1000));
    }
}

K_THREAD_DEFINE(monitor, 1024, monitor_thread, NULL, NULL, NULL, 6, 0, 0);

/* Button control thread */
static void button_thread(void *a, void *b, void *c)
{
    const struct zbus_channel *chan;

    while (1) {
        if (zbus_sub_wait(&button_sub, &chan, K_FOREVER) == 0) {
            struct { uint16_t buttons; } msg;

            if (zbus_chan_read(&button_chan, &msg, K_MSEC(100)) == 0) {
                if (msg.buttons & BIT(0)) fan_speed = MIN(255, fan_speed + 25);
                if (msg.buttons & BIT(1)) fan_speed = fan_speed >= 25 ? fan_speed - 25 : 0;
                if (msg.buttons & BIT(4)) fan_speed = 50;
                if (msg.buttons & BIT(5)) fan_speed = 255;
                fan_set_speed(fan_speed);
                LOG_INF("Button: speed=%u", fan_speed);
            }
        }
    }
}

K_THREAD_DEFINE(button_ctrl, 1024, button_thread, NULL, NULL, NULL, 6, 0, 0);

/* Init */
void fan_init(void)
{
    if (!pwm_is_ready_dt(&fan_pwm) || !gpio_is_ready_dt(&tach)) {
        LOG_ERR("Hardware not ready");
        return;
    }

    gpio_pin_configure_dt(&tach, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&tach, GPIO_INT_EDGE_RISING);
    gpio_init_callback(&tach_cb, tach_isr, BIT(tach.pin));
    gpio_add_callback(tach.port, &tach_cb);

    fan_set_speed(127);  /* Start at 50% speed */
    fan_speed = 127;
    LOG_INF("Fan initialized at 50%% speed");
}