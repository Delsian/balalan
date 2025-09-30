#include "balalan.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(joys, LOG_LEVEL_INF);

struct button_msg {
    uint16_t buttons;
};

ZBUS_CHAN_DEFINE(button_chan,                // Channel name
    struct button_msg,                     // Message type
    NULL,                                    // Validator (optional)
    NULL,                                    // User data (optional)
    ZBUS_OBSERVERS_EMPTY,                    // No observers for now
    ZBUS_MSG_INIT(.buttons = 0)              // Initial value
);

struct report_sub {
    struct bt_gatt_subscribe_params *params;
    uint16_t value_handle;
    bt_gatt_notify_func_t notify_cb;
};

static struct bt_gatt_subscribe_params sub_params1;
static struct bt_gatt_subscribe_params sub_params2;
struct bt_conn *default_conn;

static uint8_t report1_cb(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) return BT_GATT_ITER_STOP;

    if (length >= 2) {
        uint16_t buttons = sys_get_le16(data);
        LOG_DBG("[BTN] 0x%04x", buttons);
        if (buttons & BIT(0)) LOG_DBG(" A");
        if (buttons & BIT(1)) LOG_DBG(" B");
        if (buttons & BIT(4)) LOG_DBG(" LB");
        if (buttons & BIT(5)) LOG_DBG(" RB");
        struct button_msg msg = { .buttons = buttons };
        zbus_chan_pub(&button_chan, &msg, K_NO_WAIT);
    }
    return BT_GATT_ITER_CONTINUE;
}

static uint8_t report2_cb(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) return BT_GATT_ITER_STOP;

    LOG_INF("[JS] %u bytes: ", length);

    return BT_GATT_ITER_CONTINUE;
}

static struct report_sub reports[] = {
    { &sub_params1, 0x0017, report1_cb },
    { &sub_params2, 0x0013, report2_cb },
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    LOG_INF("Connected — subscribing to reports...");

    for (size_t i = 0; i < ARRAY_SIZE(reports); ++i) {
        reports[i].params->notify = reports[i].notify_cb;
        reports[i].params->value = BT_GATT_CCC_NOTIFY;
        reports[i].params->value_handle = reports[i].value_handle;
        reports[i].params->ccc_handle = reports[i].value_handle + 1;
        bt_gatt_subscribe(conn, reports[i].params);
    }

    LOG_DBG("Subscribed to all reports. Ready!");
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    while (buf->len > 1) {
        uint8_t len = net_buf_simple_pull_u8(buf);
        if (len == 0 || len > buf->len) break;

        uint8_t type = net_buf_simple_pull_u8(buf);
        if (type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) {
            char name[32];
            size_t name_len = MIN(len - 1, sizeof(name) - 1);
            memcpy(name, buf->data, name_len);
            name[name_len] = '\0';

            if (strstr(name, "Magicsee")) {
                char addr_str[BT_ADDR_LE_STR_LEN];
                bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
                LOG_INF("Found %s", name);

                bt_le_scan_stop();
                struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
                bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn);
                return;
            }
        }
        net_buf_simple_pull_mem(buf, len - 1);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);
    bt_conn_unref(default_conn);
    default_conn = NULL;
    bt_le_scan_start(BT_LE_SCAN_PASSIVE, scan_cb);
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

void joys_init(void)
{
    bt_enable(NULL);
    bt_conn_cb_register(&conn_callbacks);
    bt_le_scan_start(BT_LE_SCAN_PASSIVE, scan_cb);
}