/*
 * Magicsee R1 Dual Report Reader (Zephyr v4.1+)
 * Subscribes to:
 *   - 0x0017: Main button report (2 bytes)
 *   - 0x0013: Secondary report (e.g., joysticks)
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

static struct bt_conn *default_conn;

/* Report 1: Main buttons (from gatttool) */
#define REPORT1_HANDLE  0x0017
#define REPORT1_CCCD    0x0018

/* Report 2: Secondary (e.g., joysticks) */
#define REPORT2_HANDLE  0x0013
#define REPORT2_CCCD    0x0014

static struct bt_gatt_subscribe_params sub_params1;
static struct bt_gatt_subscribe_params sub_params2;

/* Callback for Report 1 (0x0017) — buttons */
static uint8_t report1_cb(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) return BT_GATT_ITER_STOP;

    if (length >= 2) {
        uint16_t buttons = sys_get_le16(data);
        printk("[BTN] 0x%04x", buttons);
        if (buttons & BIT(0)) printk(" A");
        if (buttons & BIT(1)) printk(" B");
        if (buttons & BIT(4)) printk(" LB");
        if (buttons & BIT(5)) printk(" RB");
        printk("\n");
    }
    return BT_GATT_ITER_CONTINUE;
}

/* Callback for Report 2 (0x0013) — possibly joysticks or extra data */
static uint8_t report2_cb(struct bt_conn *conn,
                          struct bt_gatt_subscribe_params *params,
                          const void *data, uint16_t length)
{
    if (!data) return BT_GATT_ITER_STOP;

    printk("[JS] %u bytes: ", length);
    for (int i = 0; i < length; i++) {
        printk("%02x ", ((uint8_t *)data)[i]);
    }
    printk("\n");

    return BT_GATT_ITER_CONTINUE;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err %u)\n", err);
        return;
    }

    printk("Connected — subscribing to reports...\n");

    /* Subscribe to Report 1 (buttons) */
    sub_params1.notify = report1_cb;
    sub_params1.value = BT_GATT_CCC_NOTIFY;
    sub_params1.value_handle = REPORT1_HANDLE;
    sub_params1.ccc_handle = REPORT1_CCCD;
    bt_gatt_subscribe(conn, &sub_params1);

    /* Subscribe to Report 2 (joysticks/extra) */
    sub_params2.notify = report2_cb;
    sub_params2.value = BT_GATT_CCC_NOTIFY;
    sub_params2.value_handle = REPORT2_HANDLE;
    sub_params2.ccc_handle = REPORT2_CCCD;
    bt_gatt_subscribe(conn, &sub_params2);

    printk("Subscribed to both reports. Ready!\n");
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
                printk("Found %s\n", name);

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
    printk("Disconnected (reason %u)\n", reason);
    bt_conn_unref(default_conn);
    default_conn = NULL;
    bt_le_scan_start(BT_LE_SCAN_PASSIVE, scan_cb);
}

static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

int main(void)
{
    bt_enable(NULL);
    bt_conn_cb_register(&conn_callbacks);
    bt_le_scan_start(BT_LE_SCAN_PASSIVE, scan_cb);

    for (;;) {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}