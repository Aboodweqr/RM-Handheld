#include "ble_link.h"

#include <assert.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "freertos/FreeRTOS.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// A compact HID-over-GATT gamepad plus a private telemetry write service. The
// GATT layout is original; NimBLE initialization follows Espressif's public
// peripheral examples.

static const char *TAG = "rmh_ble";
static uint8_t own_addr_type;
static uint16_t connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t hid_input_value_handle;
static bool hid_notify_enabled;
static rmh_telemetry_receive_cb_t telemetry_callback;
static void *telemetry_context;
static uint8_t protocol_mode = 1;
static uint8_t control_point;
static rmh_ble_gamepad_report_t latest_gamepad;
static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

void ble_store_config_init(void);

static const ble_uuid128_t telemetry_service_uuid = BLE_UUID128_INIT(
    0x4c, 0x44, 0x48, 0x4d, 0x52, 0x44, 0x6b, 0x9a,
    0x6e, 0x4e, 0x15, 0x1b, 0x00, 0x00, 0x51, 0x7f);
static const ble_uuid128_t telemetry_characteristic_uuid = BLE_UUID128_INIT(
    0x4c, 0x44, 0x48, 0x4d, 0x52, 0x44, 0x6b, 0x9a,
    0x6e, 0x4e, 0x15, 0x1b, 0x01, 0x00, 0x51, 0x7f);

// Android-oriented Generic HID layout, based on the axis guidance from
// lemmingDev/ESP32-BLE-Gamepad: X/Y are the left stick, Z/Rx are the right
// stick, and Brake/Accelerator are the analog L2/R2 controls.
static const uint8_t gamepad_report_map[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x05,       // Usage (Game Pad)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (1)
    0x29, 0x10,       //   Usage Maximum (16)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x10,       //   Report Count (16)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x05, 0x01,       //   Usage Page (Generic Desktop)
    0x09, 0x39,       //   Usage (Hat Switch)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x07,       //   Logical Maximum (7)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (degrees)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x42,       //   Input (Data, Variable, Absolute, Null)
    0x65, 0x00,       //   Unit (none)
    0x75, 0x04,       //   Report Size (4)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x03,       //   Input (Constant)
    0x09, 0x30,       //   Usage (X): left stick X
    0x09, 0x31,       //   Usage (Y): left stick Y
    0x09, 0x32,       //   Usage (Z): right stick X on Android
    0x09, 0x33,       //   Usage (Rx): right stick Y on Android
    0x16, 0x01, 0x80, //   Logical Minimum (-32767)
    0x26, 0xFF, 0x7F, //   Logical Maximum (32767)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x04,       //   Report Count (4)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x05, 0x02,       //   Usage Page (Simulation Controls)
    0x09, 0xC5,       //   Usage (Brake): left trigger
    0x09, 0xC4,       //   Usage (Accelerator): right trigger
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x03, //   Logical Maximum (1023)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0xC0              // End Collection
};

static int append_value(struct ble_gatt_access_ctxt *ctxt,
                        const void *data, uint16_t length) {
    return os_mbuf_append(ctxt->om, data, length) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int access_hid_info(uint16_t conn, uint16_t attr,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    static const uint8_t value[] = {0x11, 0x01, 0x00, 0x02};
    return append_value(ctxt, value, sizeof(value));
}

static int access_report_map(uint16_t conn, uint16_t attr,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    return append_value(ctxt, gamepad_report_map, sizeof(gamepad_report_map));
}

static int access_protocol_mode(uint16_t conn, uint16_t attr,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return append_value(ctxt, &protocol_mode, sizeof(protocol_mode));
    }
    if (OS_MBUF_PKTLEN(ctxt->om) != 1 ||
        os_mbuf_copydata(ctxt->om, 0, 1, &protocol_mode) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return 0;
}

static int access_control_point(uint16_t conn, uint16_t attr,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (OS_MBUF_PKTLEN(ctxt->om) != 1 ||
        os_mbuf_copydata(ctxt->om, 0, 1, &control_point) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return 0;
}

static void serialize_gamepad(const rmh_ble_gamepad_report_t *report,
                              uint8_t output[15]) {
    const uint16_t values[] = {
        report->buttons,
        (uint16_t)report->left_x,
        (uint16_t)report->left_y,
        (uint16_t)report->right_x,
        (uint16_t)report->right_y,
        report->left_trigger,
        report->right_trigger,
    };
    output[0] = values[0] & 0xFF;
    output[1] = values[0] >> 8;
    output[2] = report->hat & 0x0F;
    for (size_t index = 1; index < 7; ++index) {
        output[1 + index * 2] = values[index] & 0xFF;
        output[2 + index * 2] = values[index] >> 8;
    }
}

static int access_input_report(uint16_t conn, uint16_t attr,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    uint8_t value[15];
    rmh_ble_gamepad_report_t snapshot;
    portENTER_CRITICAL(&state_mux);
    snapshot = latest_gamepad;
    portEXIT_CRITICAL(&state_mux);
    serialize_gamepad(&snapshot, value);
    return append_value(ctxt, value, sizeof(value));
}

static int access_report_reference(uint16_t conn, uint16_t attr,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    static const uint8_t value[] = {0x01, 0x01}; // ID 1, input report.
    return append_value(ctxt, value, sizeof(value));
}

static int access_telemetry(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    const uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t frame[20];
    if (length != sizeof(frame) ||
        os_mbuf_copydata(ctxt->om, 0, length, frame) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (telemetry_callback != NULL) {
        telemetry_callback(frame, sizeof(frame), telemetry_context);
    }
    return 0;
}

static const struct ble_gatt_dsc_def input_descriptors[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2908),
        .att_flags = BLE_ATT_F_READ,
        .access_cb = access_report_reference,
    },
    {0},
};

static const struct ble_gatt_chr_def hid_characteristics[] = {
    {
        .uuid = BLE_UUID16_DECLARE(0x2A4A),
        .access_cb = access_hid_info,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid = BLE_UUID16_DECLARE(0x2A4B),
        .access_cb = access_report_map,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid = BLE_UUID16_DECLARE(0x2A4E),
        .access_cb = access_protocol_mode,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = BLE_UUID16_DECLARE(0x2A4C),
        .access_cb = access_control_point,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = BLE_UUID16_DECLARE(0x2A4D),
        .access_cb = access_input_report,
        .descriptors = input_descriptors,
        .val_handle = &hid_input_value_handle,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    {0},
};

static const struct ble_gatt_chr_def telemetry_characteristics[] = {
    {
        .uuid = &telemetry_characteristic_uuid.u,
        .access_cb = access_telemetry,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {0},
};

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = hid_characteristics,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &telemetry_service_uuid.u,
        .characteristics = telemetry_characteristics,
    },
    {0},
};

static void advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                portENTER_CRITICAL(&state_mux);
                connection_handle = event->connect.conn_handle;
                portEXIT_CRITICAL(&state_mux);
                ESP_LOGI(TAG, "Phone connected");
                ble_gap_security_initiate(connection_handle);
            } else {
                advertise();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Phone disconnected; reason=%d", event->disconnect.reason);
            portENTER_CRITICAL(&state_mux);
            connection_handle = BLE_HS_CONN_HANDLE_NONE;
            hid_notify_enabled = false;
            portEXIT_CRITICAL(&state_mux);
            advertise();
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == hid_input_value_handle) {
                portENTER_CRITICAL(&state_mux);
                hid_notify_enabled = event->subscribe.cur_notify != 0;
                portEXIT_CRITICAL(&state_mux);
            }
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        default:
            return 0;
    }
}

static void advertise(void) {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    // The name and standard HID UUID belong in the primary packet so Android
    // Settings and the companion app can find the board without scan-response
    // data. Flags (3) + name (13) + HID UUID (4) + appearance (4) = 24 bytes.
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    static const ble_uuid16_t advertised_services[] = {BLE_UUID16_INIT(0x1812)};
    fields.uuids16 = advertised_services;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.appearance = 0x03C4;
    fields.appearance_is_present = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Advertising data failed: %d", rc);
        return;
    }

    // Keep the private telemetry service in the scan response for app scans.
    struct ble_hs_adv_fields response;
    memset(&response, 0, sizeof(response));
    response.uuids128 = (ble_uuid128_t *)&telemetry_service_uuid;
    response.num_uuids128 = 1;
    response.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan-response data failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params parameters;
    memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &parameters, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "Advertising failed: %d", rc);
    else ESP_LOGI(TAG, "Advertising as RM Handheld (HID + telemetry)");
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "No usable BLE address: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Could not choose BLE address: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "NimBLE synced; starting discoverable advertisement");
    advertise();
}

static void on_reset(int reason) {
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
}

static void host_task(void *parameter) {
    (void)parameter;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t rmh_ble_start(rmh_telemetry_receive_cb_t callback, void *context) {
    telemetry_callback = callback;
    telemetry_context = context;
    latest_gamepad.hat = 8;

    esp_err_t error = nimble_port_init();
    if (error != ESP_OK) return error;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set("RM Handheld");
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_count_cfg(services);
    if (rc != 0) return ESP_FAIL;
    rc = ble_gatts_add_svcs(services);
    if (rc != 0) return ESP_FAIL;
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

bool rmh_ble_connected(void) {
    portENTER_CRITICAL(&state_mux);
    const bool connected = connection_handle != BLE_HS_CONN_HANDLE_NONE;
    portEXIT_CRITICAL(&state_mux);
    return connected;
}

esp_err_t rmh_ble_send_gamepad(const rmh_ble_gamepad_report_t *report) {
    if (report == NULL) return ESP_ERR_INVALID_ARG;
    uint16_t handle;
    bool notify;
    portENTER_CRITICAL(&state_mux);
    latest_gamepad = *report;
    handle = connection_handle;
    notify = hid_notify_enabled;
    portEXIT_CRITICAL(&state_mux);
    if (handle == BLE_HS_CONN_HANDLE_NONE || !notify) return ESP_ERR_INVALID_STATE;
    uint8_t value[15];
    serialize_gamepad(report, value);
    struct os_mbuf *buffer = ble_hs_mbuf_from_flat(value, sizeof(value));
    if (buffer == NULL) return ESP_ERR_NO_MEM;
    const int rc = ble_gatts_notify_custom(handle,
                                           hid_input_value_handle, buffer);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
