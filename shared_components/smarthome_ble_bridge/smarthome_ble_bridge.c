#include "smarthome_ble_bridge.h"

#include <inttypes.h>
#include <string.h>
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "smarthome_controller.h"

#define TAG "SH_BLE_BRIDGE"

#define SH_BRIDGE_APP_ID              0x44
#define SH_BRIDGE_SERVICE_UUID        0xA100
#define SH_BRIDGE_DEVICE_LIST_UUID    0xA101
#define SH_BRIDGE_STATE_UUID          0xA102
#define SH_BRIDGE_COMMAND_UUID        0xA103
#define SH_BRIDGE_NOTIFY_UUID         0xA104
#define SH_BRIDGE_MAX_PAYLOAD         244
#define SH_BRIDGE_MAX_NOTIFY_PAYLOAD  16

typedef enum {
    SH_BRIDGE_HANDLE_SERVICE = 0,
    SH_BRIDGE_HANDLE_DEVICE_LIST_CHAR,
    SH_BRIDGE_HANDLE_DEVICE_LIST_VAL,
    SH_BRIDGE_HANDLE_STATE_CHAR,
    SH_BRIDGE_HANDLE_STATE_VAL,
    SH_BRIDGE_HANDLE_COMMAND_CHAR,
    SH_BRIDGE_HANDLE_COMMAND_VAL,
    SH_BRIDGE_HANDLE_NOTIFY_CHAR,
    SH_BRIDGE_HANDLE_NOTIFY_VAL,
    SH_BRIDGE_HANDLE_NOTIFY_CFG,
    SH_BRIDGE_HANDLE_COUNT,
} sh_bridge_handle_t;

static uint16_t s_handles[SH_BRIDGE_HANDLE_COUNT];
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id;
static bool s_connected;
static bool s_initialized;
static bool s_notify_enabled = true;
static bool s_adv_configured;
static bool s_adv_started;
static char s_device_name[SH_BLE_BRIDGE_DEVICE_NAME_MAX] = "MeshAC Bridge";
static esp_attr_control_t s_rsp_by_app = {
    .auto_rsp = ESP_GATT_RSP_BY_APP,
};

static void start_advertising(void);

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int32_t read_le32s(const uint8_t *data)
{
    uint32_t value = (uint32_t)data[0] |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) |
        ((uint32_t)data[3] << 24);
    return (int32_t)value;
}

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFF);
    data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, int32_t value)
{
    uint32_t raw = (uint32_t)value;
    data[0] = (uint8_t)(raw & 0xFF);
    data[1] = (uint8_t)((raw >> 8) & 0xFF);
    data[2] = (uint8_t)((raw >> 16) & 0xFF);
    data[3] = (uint8_t)((raw >> 24) & 0xFF);
}

static uint16_t build_device_list(uint8_t *out, uint16_t out_len)
{
    if (!out || out_len < 1) {
        return 0;
    }

    sh_controller_device_t devices[SH_CLIENT_MAX_DEVICES];
    uint8_t count = sh_controller_get_device_list(devices, SH_CLIENT_MAX_DEVICES);
    uint16_t offset = 1;
    out[0] = count;

    for (uint8_t i = 0; i < count && offset + 6 <= out_len; i++) {
        write_le16(out + offset, devices[i].addr);
        offset += 2;
        out[offset++] = devices[i].is_online ? 1 : 0;
        out[offset++] = devices[i].profile_loaded ? 1 : 0;
        write_le16(out + offset, devices[i].profile ? devices[i].profile->profile_id : 0);
        offset += 2;
    }

    return offset;
}

static uint16_t build_state_snapshot(uint8_t *out, uint16_t out_len)
{
    if (!out || out_len < 1) {
        return 0;
    }

    sh_controller_device_t devices[SH_CLIENT_MAX_DEVICES];
    uint8_t count = sh_controller_get_device_list(devices, SH_CLIENT_MAX_DEVICES);
    uint16_t offset = 1;
    out[0] = count;

    for (uint8_t i = 0; i < count && offset + 4 <= out_len; i++) {
        write_le16(out + offset, devices[i].addr);
        offset += 2;
        out[offset++] = (uint8_t)devices[i].state_count;
        for (uint8_t j = 0; j < devices[i].state_count && offset + 8 <= out_len; j++) {
            write_le16(out + offset, devices[i].states[j].feature_id);
            offset += 2;
            out[offset++] = (uint8_t)devices[i].states[j].type;
            write_le32(out + offset, devices[i].states[j].value);
            offset += 4;
        }
    }

    return offset;
}

static void send_read_response(esp_gatt_if_t gatts_if,
                               esp_ble_gatts_cb_param_t *param,
                               const uint8_t *data,
                               uint16_t len)
{
    esp_gatt_rsp_t rsp = {0};
    rsp.attr_value.handle = param->read.handle;
    rsp.attr_value.len = len > ESP_GATT_MAX_ATTR_LEN ? ESP_GATT_MAX_ATTR_LEN : len;
    if (data && rsp.attr_value.len > 0) {
        memcpy(rsp.attr_value.value, data, rsp.attr_value.len);
    }
    esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                param->read.trans_id, ESP_GATT_OK, &rsp);
}

static void handle_command_write(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        return;
    }

    switch ((sh_ble_bridge_cmd_t)data[0]) {
    case SH_BLE_BRIDGE_CMD_SET_FEATURE:
        if (len >= 9) {
            uint16_t addr = read_le16(data + 1);
            uint16_t feature_id = read_le16(data + 3);
            int32_t value = read_le32s(data + 5);
            esp_err_t err = sh_controller_set_feature(addr, feature_id, value);
            ESP_LOGI(TAG, "App set addr=0x%04x feature=0x%04x value=%" PRId32 " err=%s",
                     addr, feature_id, value, esp_err_to_name(err));
        }
        break;
    case SH_BLE_BRIDGE_CMD_GROUP_SET_FEATURE:
        if (len >= 7) {
            uint16_t feature_id = read_le16(data + 1);
            int32_t value = read_le32s(data + 3);
            esp_err_t err = sh_controller_group_set_feature(feature_id, value);
            ESP_LOGI(TAG, "App group set feature=0x%04x value=%" PRId32 " err=%s",
                     feature_id, value, esp_err_to_name(err));
        }
        break;
    case SH_BLE_BRIDGE_CMD_REFRESH_ALL:
        sh_controller_refresh_all();
        break;
    default:
        ESP_LOGW(TAG, "Unknown app bridge command 0x%02x", data[0]);
        break;
    }
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        if (param->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "BLE bridge raw adv data config failed: status=%d",
                     param->adv_data_raw_cmpl.status);
            return;
        }
        s_adv_configured = true;
        ESP_LOGI(TAG, "BLE bridge raw adv data configured");
        start_advertising();
        break;
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        if (param->adv_data_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "BLE bridge adv data config failed: status=%d",
                     param->adv_data_cmpl.status);
            return;
        }
        s_adv_configured = true;
        ESP_LOGI(TAG, "BLE bridge adv data configured");
        start_advertising();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_started = true;
            ESP_LOGI(TAG, "BLE bridge advertising started as \"%s\"", s_device_name);
        } else {
            s_adv_started = false;
            ESP_LOGW(TAG, "BLE bridge advertising start failed: status=%d",
                     param->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_adv_started = false;
        break;
    default:
        break;
    }
}

static void start_advertising(void)
{
    if (!s_adv_configured) {
        return;
    }
    if (s_adv_started || s_connected) {
        return;
    }

    esp_ble_adv_params_t adv_params = {
        .adv_int_min = 0x40,
        .adv_int_max = 0x80,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };

    esp_err_t err = esp_ble_gap_start_advertising(&adv_params);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BLE bridge advertising start request failed: %s",
                 esp_err_to_name(err));
    }
}

static void configure_advertising(void)
{
    static uint8_t raw_adv_data[31];
    uint8_t offset = 0;
    size_t name_len = strlen(s_device_name);
    if (name_len > 20) {
        name_len = 20;
    }

    raw_adv_data[offset++] = 0x02;
    raw_adv_data[offset++] = ESP_BLE_AD_TYPE_FLAG;
    raw_adv_data[offset++] = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;

    raw_adv_data[offset++] = 0x03;
    raw_adv_data[offset++] = ESP_BLE_AD_TYPE_16SRV_CMPL;
    raw_adv_data[offset++] = (uint8_t)(SH_BRIDGE_SERVICE_UUID & 0xFF);
    raw_adv_data[offset++] = (uint8_t)(SH_BRIDGE_SERVICE_UUID >> 8);

    raw_adv_data[offset++] = (uint8_t)(name_len + 1);
    raw_adv_data[offset++] = ESP_BLE_AD_TYPE_NAME_CMPL;
    memcpy(raw_adv_data + offset, s_device_name, name_len);
    offset += name_len;

    s_adv_configured = false;
    esp_err_t err = esp_ble_gap_config_adv_data_raw(raw_adv_data, offset);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BLE bridge raw adv data config request failed: %s",
                 esp_err_to_name(err));
    }
}

static void gatts_cb(esp_gatts_cb_event_t event,
                     esp_gatt_if_t gatts_if,
                     esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        s_gatts_if = gatts_if;
        esp_ble_gap_set_device_name(s_device_name);
        esp_ble_gatts_create_service(gatts_if, &(esp_gatt_srvc_id_t) {
            .is_primary = true,
            .id = {
                .inst_id = 0,
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = {.uuid16 = SH_BRIDGE_SERVICE_UUID},
                },
            },
        }, SH_BRIDGE_HANDLE_COUNT);
        break;
    }
    case ESP_GATTS_CREATE_EVT:
        s_handles[SH_BRIDGE_HANDLE_SERVICE] = param->create.service_handle;
        esp_ble_gatts_start_service(param->create.service_handle);
        esp_ble_gatts_add_char(param->create.service_handle,
                               &(esp_bt_uuid_t){.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = SH_BRIDGE_DEVICE_LIST_UUID}},
                               ESP_GATT_PERM_READ,
                               ESP_GATT_CHAR_PROP_BIT_READ,
                               NULL, &s_rsp_by_app);
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        if (param->add_char.char_uuid.uuid.uuid16 == SH_BRIDGE_DEVICE_LIST_UUID) {
            s_handles[SH_BRIDGE_HANDLE_DEVICE_LIST_VAL] = param->add_char.attr_handle;
            esp_ble_gatts_add_char(param->add_char.service_handle,
                                   &(esp_bt_uuid_t){.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = SH_BRIDGE_STATE_UUID}},
                                   ESP_GATT_PERM_READ,
                                   ESP_GATT_CHAR_PROP_BIT_READ,
                                   NULL, &s_rsp_by_app);
        } else if (param->add_char.char_uuid.uuid.uuid16 == SH_BRIDGE_STATE_UUID) {
            s_handles[SH_BRIDGE_HANDLE_STATE_VAL] = param->add_char.attr_handle;
            esp_ble_gatts_add_char(param->add_char.service_handle,
                                   &(esp_bt_uuid_t){.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = SH_BRIDGE_COMMAND_UUID}},
                                   ESP_GATT_PERM_WRITE,
                                   ESP_GATT_CHAR_PROP_BIT_WRITE,
                                   NULL, &s_rsp_by_app);
        } else if (param->add_char.char_uuid.uuid.uuid16 == SH_BRIDGE_COMMAND_UUID) {
            s_handles[SH_BRIDGE_HANDLE_COMMAND_VAL] = param->add_char.attr_handle;
            esp_ble_gatts_add_char(param->add_char.service_handle,
                                   &(esp_bt_uuid_t){.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = SH_BRIDGE_NOTIFY_UUID}},
                                   ESP_GATT_PERM_READ,
                                   ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                                   NULL, &s_rsp_by_app);
        } else if (param->add_char.char_uuid.uuid.uuid16 == SH_BRIDGE_NOTIFY_UUID) {
            s_handles[SH_BRIDGE_HANDLE_NOTIFY_VAL] = param->add_char.attr_handle;
            esp_ble_gatts_add_char_descr(param->add_char.service_handle,
                                         &(esp_bt_uuid_t){.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG}},
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                         NULL, &s_rsp_by_app);
            configure_advertising();
        }
        break;
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        s_handles[SH_BRIDGE_HANDLE_NOTIFY_CFG] = param->add_char_descr.attr_handle;
        break;
    case ESP_GATTS_CONNECT_EVT:
        s_connected = true;
        s_conn_id = param->connect.conn_id;
        s_gatts_if = gatts_if;
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        s_connected = false;
        start_advertising();
        break;
    case ESP_GATTS_READ_EVT: {
        uint8_t payload[SH_BRIDGE_MAX_PAYLOAD];
        uint16_t len = 0;
        if (param->read.handle == s_handles[SH_BRIDGE_HANDLE_DEVICE_LIST_VAL]) {
            len = build_device_list(payload, sizeof(payload));
        } else if (param->read.handle == s_handles[SH_BRIDGE_HANDLE_STATE_VAL] ||
                   param->read.handle == s_handles[SH_BRIDGE_HANDLE_NOTIFY_VAL]) {
            len = build_state_snapshot(payload, sizeof(payload));
        }
        send_read_response(gatts_if, param, payload, len);
        break;
    }
    case ESP_GATTS_WRITE_EVT:
        if (param->write.handle == s_handles[SH_BRIDGE_HANDLE_COMMAND_VAL]) {
            handle_command_write(param->write.value, param->write.len);
        } else if (param->write.handle == s_handles[SH_BRIDGE_HANDLE_NOTIFY_CFG] &&
                   param->write.len >= 2) {
            s_notify_enabled = (read_le16(param->write.value) & 0x0001) != 0;
        }
        if (param->write.need_rsp) {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                        param->write.trans_id, ESP_GATT_OK, NULL);
        }
        break;
    default:
        break;
    }
}

esp_err_t sh_ble_bridge_init(const sh_ble_bridge_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (config) {
        if (config->device_name) {
            strncpy(s_device_name, config->device_name, sizeof(s_device_name) - 1);
            s_device_name[sizeof(s_device_name) - 1] = '\0';
        }
        s_notify_enabled = config->enable_notifications;
    }

    esp_err_t err = esp_ble_gap_register_callback(gap_cb);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_ble_gatts_register_callback(gatts_cb);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_ble_gatts_app_register(SH_BRIDGE_APP_ID);
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

esp_err_t sh_ble_bridge_notify_device_status(uint16_t addr,
                                             uint16_t feature_id,
                                             sh_feature_type_t type,
                                             int32_t value)
{
    if (!s_connected || !s_notify_enabled || s_gatts_if == ESP_GATT_IF_NONE ||
        s_handles[SH_BRIDGE_HANDLE_NOTIFY_VAL] == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t payload[SH_BRIDGE_MAX_NOTIFY_PAYLOAD] = {0};
    write_le16(payload, addr);
    write_le16(payload + 2, feature_id);
    payload[4] = (uint8_t)type;
    write_le32(payload + 5, value);

    return esp_ble_gatts_send_indicate(s_gatts_if, s_conn_id,
                                       s_handles[SH_BRIDGE_HANDLE_NOTIFY_VAL],
                                       9, payload, false);
}
