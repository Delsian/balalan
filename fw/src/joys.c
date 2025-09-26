#include "balalan.h"
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/scan.h>
#include <bluetooth/gatt_dm.h>

LOG_MODULE_REGISTER(joys, LOG_LEVEL_DBG);

/* HID Service UUID (0x1812) */
#define BT_UUID_HID_SERVICE BT_UUID_DECLARE_16(0x1812)
/* HID Report Characteristic UUID (0x2A4D) */
#define BT_UUID_HID_REPORT BT_UUID_DECLARE_16(0x2A4D)

struct bt_conn *default_conn = NULL;

/* HID Report notification callback */
static uint8_t hid_report_notify(struct bt_conn *conn,
                                struct bt_gatt_subscribe_params *params,
                                const void *data, uint16_t length)
{
    const uint8_t *report_data = (const uint8_t *)data;

    if (!data) {
        LOG_INF("HID report unsubscribed");
        return BT_GATT_ITER_STOP;
    }

    /* Log all received data */
    LOG_INF("HID Report (%d bytes):", length);
    for (int i = 0; i < length; i++) {
        LOG_INF("  [%d] = 0x%02X (%d)", i, report_data[i], report_data[i]);
    }

    /* Parse joystick data if format is known */
    if (length >= 3) {
        uint8_t buttons = report_data[0];
        int8_t x_axis = (int8_t)(report_data[1] - 128);
        int8_t y_axis = (int8_t)(report_data[2] - 128);

        LOG_INF("Joystick: Buttons=0x%02X, X=%d, Y=%d", buttons, x_axis, y_axis);
    }

    return BT_GATT_ITER_CONTINUE;
}

static struct bt_gatt_subscribe_params hid_report_params = {
    .notify = hid_report_notify,
    .value = BT_GATT_CCC_NOTIFY,
};

/* GATT Discovery completed callback */
static void gatt_dm_discovery_completed(struct bt_gatt_dm *dm, void *context) {
    LOG_INF("Service discovery completed");
    bt_gatt_dm_data_print(dm);
    const struct bt_gatt_dm_attr *gatt_service_attr;
    const struct bt_gatt_dm_attr *gatt_chrc;
    const struct bt_gatt_dm_attr *gatt_desc;

    gatt_service_attr = bt_gatt_dm_service_get(dm);
    gatt_chrc = bt_gatt_dm_char_next(dm, NULL);

    /* Iterate through all characteristics in the HID service */
    while (gatt_chrc != NULL) {
        struct bt_gatt_chrc *chrc_val = bt_gatt_dm_attr_chrc_val(gatt_chrc);

        if (!bt_uuid_cmp(chrc_val->uuid, BT_UUID_HID_REPORT)) {
            LOG_INF("Found HID Report characteristic, handle: 0x%04x, properties: 0x%02x",
                    chrc_val->value_handle, chrc_val->properties);

            /* Look for input reports (notifications enabled) */
            if (chrc_val->properties & BT_GATT_CHRC_NOTIFY) {
                LOG_INF("This appears to be an input report (has NOTIFY property)");

                /* Find the CCC descriptor for this characteristic */
                gatt_desc = bt_gatt_dm_desc_by_uuid(dm, gatt_chrc, BT_UUID_GATT_CCC);
                if (gatt_desc) {
                    LOG_INF("Found CCC descriptor at handle: 0x%04x", gatt_desc->handle);

                    /* Set subscription parameters */
                    hid_report_params.value_handle = chrc_val->value_handle;
                    hid_report_params.ccc_handle = gatt_desc->handle;

                    /* Subscribe to notifications */
                    int ret = bt_gatt_subscribe(bt_gatt_dm_conn_get(dm), &hid_report_params);
                    if (ret && ret != -EALREADY) {
                        LOG_ERR("Subscribe failed (err %d)", ret);
                    } else {
                        LOG_INF("Successfully subscribed to HID input reports");
                        goto release; // Successfully subscribed to first input report
                    }
                } else {
                    LOG_WRN("No CCC descriptor found for this HID Report characteristic");
                }
            } else {
                LOG_INF("This HID Report doesn't support notifications (likely output/feature report)");
            }
        }

        /* Get next characteristic */
        gatt_chrc = bt_gatt_dm_char_next(dm, gatt_chrc);
    }

    LOG_ERR("No suitable HID input report characteristic found");

release:
    bt_gatt_dm_data_release(dm);
}

static void gatt_dm_discovery_service_not_found(struct bt_conn *conn, void *context)
{
    LOG_ERR("HID service not found");
}

static void gatt_dm_discovery_error(struct bt_conn *conn, int err, void *context)
{
    LOG_ERR("Service discovery error (err %d)", err);
}

static const struct bt_gatt_dm_cb gatt_dm_cb = {
    .completed = gatt_dm_discovery_completed,
    .service_not_found = gatt_dm_discovery_service_not_found,
    .error_found = gatt_dm_discovery_error,
};

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Failed to connect to %s (err %u)", addr, err);
        return;
    }

    LOG_INF("Connected to %s", addr);

    /* Start HID service discovery */
    int ret = bt_gatt_dm_start(conn, BT_UUID_HID_SERVICE, &gatt_dm_cb, NULL);
    if (ret) {
        LOG_ERR("Service discovery failed (err %d)", ret);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason %u)", addr, reason);

    if (default_conn) {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }

    /* Restart scanning */
    bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Scan callbacks */
static void scan_filter_match(struct bt_scan_device_info *device_info,
                              struct bt_scan_filter_match *filter_match,
                              bool connectable) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
    LOG_INF("HID device found: %s (RSSI %d)", addr, device_info->recv_info->rssi);
}

static void scan_connecting(struct bt_scan_device_info *device_info,
                           struct bt_conn *conn) {
    default_conn = bt_conn_ref(conn);
    LOG_INF("Connecting to joystick...");
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
    LOG_ERR("Connection failed");
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, NULL, scan_connecting_error, scan_connecting);

/* Bluetooth ready callback */
static void bt_ready_handler(int err)
{
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    /* Configure scan for HID devices */
    struct bt_scan_init_param scan_init = {
        .connect_if_match = true,
        .scan_param = NULL,
        .conn_param = BT_LE_CONN_PARAM_DEFAULT
    };

    bt_scan_init(&scan_init);
    bt_scan_cb_register(&scan_cb);

    /* Filter for HID service */
    int ret = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_HID_SERVICE);
    if (ret) {
        LOG_ERR("Failed to add UUID filter (err %d)", ret);
    }

    ret = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
    if (ret) {
        LOG_ERR("Failed to enable UUID filter (err %d)", ret);
    }

    /* Start scanning immediately */
    LOG_INF("Starting scan for BLE joysticks...");
    bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
}

int joys_init(void) {
    return bt_enable(bt_ready_handler);
}