#pragma once

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <zephyr/bluetooth/conn.h>

/* External declaration - defined in joys.c */
extern struct bt_conn *default_conn;

/* Function declarations */
void joys_init(void);
