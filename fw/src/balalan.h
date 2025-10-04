#pragma once

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <zephyr/bluetooth/conn.h>

/* External declaration - defined in joys.c */
extern struct bt_conn *default_conn;

/* Function declarations */
void joys_init(void);
void imu_init(void);
void fan_init(void);
void fan_set_speed(uint8_t speed);  /* 0-255 */
uint16_t fan_get_rpm(void);