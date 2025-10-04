#pragma once

#include <zephyr/types.h>
#include <stdbool.h>

/* Fan control definitions */
#define FAN_PWM_FREQUENCY_HZ    25000
#define FAN_MIN_DUTY_PERCENT    20
#define FAN_MAX_DUTY_PERCENT    100

/* Fan data structure */
struct fan_data {
    uint8_t speed_cmd;
    uint16_t rpm;
    uint8_t duty_percent;
    bool running;
    int64_t timestamp;
};

/* Public API */
void fan_init(void);
void fan_set_speed(uint8_t speed);
uint16_t fan_get_rpm(void);