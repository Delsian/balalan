#include "balalan.h"
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/scan.h>

LOG_MODULE_REGISTER(joys, LOG_LEVEL_DBG);

/* Global connection handle */
struct bt_conn *default_conn = NULL;

/* Connection state tracking */
static bool connection_active = false;
static bool operations_in_progress = false;

/* Work item for delayed HID operations */
static void hid_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(hid_work, hid_work_handler);

/* Direct GATT subscription parameters - support multiple subscriptions */
static struct bt_gatt_subscribe_params hid_params[4];
static int active_subscriptions = 0;

/* Known HID Report handles from joystik.txt */
static const uint16_t hid_report_handles[] = {0x0013, 0x0017, 0x001b, 0x001f};
static const uint16_t hid_ccc_handles[] = {0x0014, 0x0018, 0x001c, 0x0020};
static const int num_hid_reports = 4;

/* HID notification callback */
static uint8_t hid_notify_callback(struct bt_conn *conn,
                                  struct bt_gatt_subscribe_params *params,
                                  const void *data, uint16_t length)
{
    if (!connection_active) {
        LOG_INF("HID notification - connection not active");
        return BT_GATT_ITER_STOP;
    }

    if (!data) {
        LOG_INF("HID notification ended for handle 0x%04x", params->value_handle);
        return BT_GATT_ITER_STOP;
    }

    const uint8_t *report_data = (const uint8_t *)data;

    LOG_INF("=== HID NOTIFICATION (handle 0x%04x, %d bytes) ===", params->value_handle, length);
    LOG_HEXDUMP_INF(report_data, length, "Data:");

    /* Parse joystick data based on handle and length */
    if (params->value_handle == 0x0017 && length == 2) {
        /* Handle 0x0017 appears to be button data */
        uint8_t buttons = report_data[0];
        uint8_t modifier = report_data[1];

        LOG_INF("Button Report: Buttons=0x%02X, Modifier=0x%02X", buttons, modifier);
    } else if (length >= 3) {
        /* Standard joystick format with X/Y axes */
        uint8_t buttons = report_data[0];
        int8_t x_axis = (int8_t)report_data[1];
        int8_t y_axis = (int8_t)report_data[2];

        LOG_INF("Joystick: Buttons=0x%02X, X=%d, Y=%d", buttons, x_axis, y_axis);
    } else if (length > 0) {
        /* Handle other report formats */
        LOG_INF("Generic HID Report (handle 0x%04x)", params->value_handle);

        /* Simple activity detection */
        bool has_activity = false;
        for (int i = 0; i < length; i++) {
            if (report_data[i] != 0) {
                has_activity = true;
                break;
            }
        }
    }

    return BT_GATT_ITER_CONTINUE;
}

/* Safe HID operations work handler */
static void hid_work_handler(struct k_work *work)
{
    int ret;
    int subscription_attempts = 0;

    /* Safety checks */
    if (!default_conn || !connection_active || operations_in_progress) {
        LOG_WRN("HID work skipped - invalid state");
        return;
    }

    operations_in_progress = true;
    active_subscriptions = 0;

    /* Try to subscribe to all known HID report handles */
    for (int i = 0; i < num_hid_reports; i++) {
        if (!connection_active) {
            LOG_WRN("Connection lost during subscription attempts");
            break;
        }

        LOG_INF("Subscribing to HID report %d: value=0x%04x, ccc=0x%04x",
               i, hid_report_handles[i], hid_ccc_handles[i]);

        /* Clear subscription parameters */
        memset(&hid_params[i], 0, sizeof(hid_params[i]));
        hid_params[i].notify = hid_notify_callback;
        hid_params[i].value = BT_GATT_CCC_NOTIFY;
        hid_params[i].value_handle = hid_report_handles[i];
        hid_params[i].ccc_handle = hid_ccc_handles[i];

        ret = bt_gatt_subscribe(default_conn, &hid_params[i]);
        if (ret && ret != -EALREADY) {
            LOG_WRN("Subscribe to handle 0x%04x failed (err %d), trying without explicit CCC",
                   hid_report_handles[i], ret);

            /* Try without explicit CCC handle */
            hid_params[i].ccc_handle = 0;
            ret = bt_gatt_subscribe(default_conn, &hid_params[i]);

            if (ret && ret != -EALREADY) {
                LOG_WRN("Subscribe to handle 0x%04x failed with auto CCC (err %d), trying different approach",
                       hid_report_handles[i], ret);

                /* For handle 0x001f, try a different CCC handle or skip it */
                if (hid_report_handles[i] == 0x001f) {
                    LOG_INF("Skipping problematic handle 0x001f - other handles are working");
                    /* Don't count this as a failure since others work */
                } else {
                    LOG_ERR("Subscribe to handle 0x%04x failed completely (err %d)",
                           hid_report_handles[i], ret);
                }
            } else {
                LOG_INF("Successfully subscribed to handle 0x%04x (auto CCC)", hid_report_handles[i]);
                active_subscriptions++;
            }
        } else {
            LOG_INF("Successfully subscribed to handle 0x%04x (explicit CCC)", hid_report_handles[i]);
            active_subscriptions++;
        }

        subscription_attempts++;

        /* Small delay between subscription attempts */
        k_sleep(K_MSEC(200));
    }

    LOG_INF("=== Subscription results: %d successful out of %d attempts ===",
           active_subscriptions, subscription_attempts);

    if (active_subscriptions > 0) {
        LOG_INF("Waiting for HID notifications... Move joystick or press buttons!");
        LOG_INF("Active subscriptions on handles:");
        for (int i = 0; i < num_hid_reports; i++) {
            if (hid_params[i].value_handle != 0) {
                LOG_INF("  - Handle 0x%04x", hid_params[i].value_handle);
            }
        }
    } else {
        LOG_ERR("No successful subscriptions - no HID data will be received");
    }

    operations_in_progress = false;
}

/* Start HID operations safely */
static void start_hid_operations(void)
{
    if (!connection_active || operations_in_progress) {
        return;
    }

    LOG_INF("Starting HID subscription operations...");
    k_work_reschedule(&hid_work, K_MSEC(2000));  /* Give connection time to stabilize */
}

/* Stop HID operations */
static void stop_hid_operations(void)
{
    LOG_INF("Stopping HID operations");
    k_work_cancel_delayable(&hid_work);
    operations_in_progress = false;
    active_subscriptions = 0;

    /* Clear subscription parameters */
    for (int i = 0; i < num_hid_reports; i++) {
        memset(&hid_params[i], 0, sizeof(hid_params[i]));
    }
}

/* Scan callbacks */
static void scan_filter_match(struct bt_scan_device_info *device_info,
                              struct bt_scan_filter_match *filter_match,
                              bool connectable)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
    LOG_INF("HID device found: %s (RSSI: %d)", addr, device_info->recv_info->rssi);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
    LOG_ERR("Connection failed");
}

static void scan_connecting(struct bt_scan_device_info *device_info,
                           struct bt_conn *conn)
{
    default_conn = bt_conn_ref(conn);
    LOG_INF("Connecting to joystick...");
}

static void scan_filter_no_match(struct bt_scan_device_info *device_info,
                                 bool connectable)
{
    /* Only connect to HID devices */
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match,
                scan_connecting_error, scan_connecting);

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int err;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err) {
        LOG_ERR("Failed to connect to %s (err 0x%02x)", addr, conn_err);
        if (conn == default_conn) {
            bt_conn_unref(default_conn);
            default_conn = NULL;

            /* Restart scanning */
            err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
            if (err) {
                LOG_ERR("Scanning failed to start (err %d)", err);
            }
        }
        return;
    }

    LOG_INF("Connected to: %s", addr);

    /* Mark connection as active */
    connection_active = true;

    /* Start HID operations with a delay to let connection stabilize */
    start_hid_operations();
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int err;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason 0x%02x)", addr, reason);

    /* Immediately mark connection as inactive */
    connection_active = false;

    /* Stop any ongoing operations */
    stop_hid_operations();

    if (default_conn != conn) {
        return;
    }

    bt_conn_unref(default_conn);
    default_conn = NULL;

    /* Restart scanning after a short delay */
    k_sleep(K_MSEC(1000));
    err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
    if (err) {
        LOG_ERR("Scanning failed to start (err %d)", err);
    } else {
        LOG_INF("Scanning restarted");
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Scan initialization */
static void scan_init(void)
{
    int err;

    struct bt_scan_init_param scan_init = {
        .connect_if_match = true,
        .scan_param = NULL,
        .conn_param = BT_LE_CONN_PARAM_DEFAULT
    };

    bt_scan_init(&scan_init);
    bt_scan_cb_register(&scan_cb);

    err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_HIDS);
    if (err) {
        LOG_ERR("Scanning filters cannot be set (err %d)", err);
        return;
    }

    err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
    if (err) {
        LOG_ERR("Filters cannot be turned on (err %d)", err);
    }
}

/* Bluetooth ready callback */
static void bt_ready_handler(int err)
{
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    /* Initialize scanning */
    scan_init();

    /* Start scanning immediately */
    LOG_INF("Starting scan for BLE HID devices...");
    err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
    if (err) {
        LOG_ERR("Scanning failed to start (err %d)", err);
    } else {
        LOG_INF("Scanning successfully started");
    }
}

int joys_init(void)
{
    return bt_enable(bt_ready_handler);
}