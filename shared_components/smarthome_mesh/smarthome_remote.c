#include "smarthome_remote.h"
#include "sdkconfig.h"

#if CONFIG_BLE_MESH_NODE && !CONFIG_BLE_MESH_PROVISIONER

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "ble_mesh_example_init.h"
#include "smarthome_protocol.h"
#include "smarthome_profiles.h"
#include "smarthome_store.h"

#define TAG "SH_REMOTE"
#define SH_PROFILE_BLOB_MAX 512
#define SH_MSG_SEND_TTL 7
#define SH_MSG_TIMEOUT_MS 4000
#define SH_PROVISIONER_ADDR 0x0001
#define SH_PROFILE_RETRY_PERIOD_MS 500
#define SH_PROFILE_RETRY_FAST_MS 5000
#define SH_PROFILE_RETRY_SLOW_MS 15000
#define SH_PROFILE_RETRY_FAST_COUNT 3
#define SH_DIRECTORY_RETRY_PERIOD_MS 1000
#define SH_DIRECTORY_RETRY_FAST_MS 1000
#define SH_DIRECTORY_RETRY_SLOW_MS 5000
#define SH_DIRECTORY_RETRY_FAST_COUNT 5
#define SH_DIRECTORY_REFRESH_MS 60000

#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(x, tag, fmt, ...) do { \
        esp_err_t err_rc_ = (x);                    \
        if (err_rc_ != ESP_OK) {                    \
            return err_rc_;                         \
        }                                           \
    } while (0)
#endif

typedef struct {
    sh_client_device_t public_info;
    sh_dynamic_profile_t profile_storage;
    uint8_t profile_blob[SH_PROFILE_BLOB_MAX];
    uint16_t profile_blob_len;
    uint16_t profile_expected_len;
    uint16_t profile_crc;
    uint8_t profile_chunks_seen;
    uint8_t profile_chunk_count;
    uint8_t profile_retry_count;
    uint32_t next_profile_retry_ms;
    bool profile_request_pending;
} sh_remote_device_slot_t;

static uint8_t s_dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = {0xCC, 0xCC};
static sh_remote_device_slot_t s_devices[SH_CLIENT_MAX_DEVICES];
static uint8_t s_device_count;
static sh_client_callbacks_t s_callbacks;
static bool s_initialized;
static uint16_t s_net_idx = ESP_BLE_MESH_KEY_PRIMARY;
static uint16_t s_app_idx = 0x0000;
static uint16_t s_own_addr = ESP_BLE_MESH_ADDR_UNASSIGNED;
static esp_timer_handle_t s_profile_retry_timer;
static esp_timer_handle_t s_directory_retry_timer;
static uint8_t s_directory_request_count;
static uint32_t s_next_directory_request_ms;

static esp_ble_mesh_cfg_srv_t s_config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = SH_MSG_SEND_TTL,
};

static esp_ble_mesh_client_t s_vendor_client;

static esp_ble_mesh_model_op_t s_remote_ops[] = {
    ESP_BLE_MESH_MODEL_OP(SH_OP_PROFILE_STATUS, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_FEATURE_STATUS, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_DEVICE_DIRECTORY, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_NODE_EVENT, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_DISCONNECT_ACK, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_client_op_pair_t s_remote_op_pair[] = {
    {SH_OP_PROFILE_GET, SH_OP_PROFILE_STATUS},
    {SH_OP_PROFILE_SET, SH_OP_PROFILE_STATUS},
    {SH_OP_FEATURE_GET, SH_OP_FEATURE_STATUS},
    {SH_OP_FEATURE_SET, SH_OP_FEATURE_STATUS},
    {SH_OP_DEVICE_DIRECTORY_GET, SH_OP_DEVICE_DIRECTORY},
    {SH_OP_DEVICE_DIRECTORY, SH_OP_DEVICE_DIRECTORY},
};

static esp_ble_mesh_model_t s_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_config_server),
};

static esp_ble_mesh_model_t s_vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(SH_COMPANY_ID, SH_MODEL_ID_CLIENT,
                              s_remote_ops, NULL, &s_vendor_client),
};

static esp_ble_mesh_elem_t s_elements[] = {
    ESP_BLE_MESH_ELEMENT(0, s_root_models, s_vendor_models),
};

static esp_ble_mesh_comp_t s_composition = {
    .cid = SH_COMPANY_ID,
    .element_count = ARRAY_SIZE(s_elements),
    .elements = s_elements,
};

static esp_ble_mesh_prov_t s_provision = {
    .uuid = s_dev_uuid,
};

static void init_device_uuid(void)
{
    uint8_t mac[6] = {0};
    s_dev_uuid[0] = 0xCC;
    s_dev_uuid[1] = 0xCC;
    if (esp_read_mac(mac, ESP_MAC_BT) == ESP_OK) {
        memcpy(&s_dev_uuid[2], mac, sizeof(mac));
    }
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int find_device_index(uint16_t addr)
{
    for (uint8_t i = 0; i < s_device_count; i++) {
        if (s_devices[i].public_info.addr == addr) {
            return i;
        }
    }
    return -1;
}

static sh_remote_device_slot_t *find_device(uint16_t addr)
{
    int idx = find_device_index(addr);
    return idx >= 0 ? &s_devices[idx] : NULL;
}

static sh_remote_device_slot_t *add_device(uint16_t addr)
{
    sh_remote_device_slot_t *slot = find_device(addr);
    if (slot) {
        return slot;
    }
    if (s_device_count >= SH_CLIENT_MAX_DEVICES) {
        ESP_LOGW(TAG, "Device table full, cannot add 0x%04x", addr);
        return NULL;
    }

    slot = &s_devices[s_device_count++];
    memset(slot, 0, sizeof(*slot));
    slot->public_info.addr = addr;
    slot->public_info.is_online = true;
    slot->public_info.is_configured = true;
    slot->public_info.is_in_group = true;
    slot->public_info.last_update_time = now_ms();
    snprintf(slot->public_info.device_name, sizeof(slot->public_info.device_name),
             "NODE_%04X", addr);
    return slot;
}

static esp_err_t send_vendor(uint16_t addr, uint32_t opcode,
                             const uint8_t *data, uint16_t len, bool need_rsp)
{
    if (!s_vendor_client.model || s_own_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx = s_net_idx,
        .app_idx = s_app_idx,
        .addr = addr,
        .send_ttl = SH_MSG_SEND_TTL,
    };

    return esp_ble_mesh_client_model_send_msg(s_vendor_client.model, &ctx, opcode,
                                              len, (uint8_t *)data,
                                              need_rsp ? SH_MSG_TIMEOUT_MS : 0,
                                              need_rsp,
                                              ROLE_NODE);
}

static void profile_retry_timer_cb(void *arg);
static void directory_retry_timer_cb(void *arg);

static void ensure_profile_retry_timer(void)
{
    if (s_profile_retry_timer) {
        return;
    }

    esp_timer_create_args_t args = {
        .callback = profile_retry_timer_cb,
        .name = "sh_prof_retry",
    };
    esp_err_t err = esp_timer_create(&args, &s_profile_retry_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create profile retry timer: %s", esp_err_to_name(err));
        return;
    }

    err = esp_timer_start_periodic(s_profile_retry_timer, SH_PROFILE_RETRY_PERIOD_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start profile retry timer: %s", esp_err_to_name(err));
        esp_timer_delete(s_profile_retry_timer);
        s_profile_retry_timer = NULL;
    }
}

static void ensure_directory_retry_timer(void)
{
    if (s_directory_retry_timer) {
        return;
    }

    esp_timer_create_args_t args = {
        .callback = directory_retry_timer_cb,
        .name = "sh_dir_retry",
    };
    esp_err_t err = esp_timer_create(&args, &s_directory_retry_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create directory retry timer: %s", esp_err_to_name(err));
        return;
    }

    err = esp_timer_start_periodic(s_directory_retry_timer, SH_DIRECTORY_RETRY_PERIOD_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start directory retry timer: %s", esp_err_to_name(err));
        esp_timer_delete(s_directory_retry_timer);
        s_directory_retry_timer = NULL;
    }
}

static void schedule_device_directory_request(uint32_t delay_ms)
{
    if (s_own_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return;
    }
    s_next_directory_request_ms = now_ms() + delay_ms;
    ensure_directory_retry_timer();
}

static esp_err_t request_device_directory(void)
{
    if (s_own_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t payload[2];
    size_t written = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &written),
                        TAG, "directory get header");

    esp_err_t err = send_vendor(SH_PROVISIONER_ADDR, SH_OP_DEVICE_DIRECTORY_GET,
                                payload, written, true);
    if (err == ESP_OK) {
        s_directory_request_count++;
        uint32_t delay = s_directory_request_count <= SH_DIRECTORY_RETRY_FAST_COUNT ?
            SH_DIRECTORY_RETRY_FAST_MS : SH_DIRECTORY_RETRY_SLOW_MS;
        s_next_directory_request_ms = now_ms() + SH_MSG_TIMEOUT_MS + delay;
        ESP_LOGI(TAG, "Requested device directory from provisioner 0x%04x (attempt %u)",
                 SH_PROVISIONER_ADDR, (unsigned)s_directory_request_count);
    } else {
        s_next_directory_request_ms = now_ms() + SH_DIRECTORY_RETRY_FAST_MS;
        ESP_LOGW(TAG, "Failed to request device directory: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t request_profile_for_device(uint16_t addr)
{
    sh_remote_device_slot_t *slot = add_device(addr);
    if (!slot) {
        return ESP_ERR_NO_MEM;
    }
    if (slot->public_info.profile_loaded) {
        return ESP_OK;
    }
    if (slot->profile_request_pending) {
        return ESP_OK;
    }
    ensure_profile_retry_timer();

    uint8_t payload[2];
    size_t written = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &written),
                        TAG, "profile get header");
    esp_err_t err = send_vendor(addr, SH_OP_PROFILE_GET, payload, written, true);
    if (err == ESP_OK) {
        slot->profile_retry_count++;
        slot->profile_request_pending = true;
        uint32_t delay = slot->profile_retry_count <= SH_PROFILE_RETRY_FAST_COUNT ?
            SH_PROFILE_RETRY_FAST_MS : SH_PROFILE_RETRY_SLOW_MS;
        uint32_t fallback_delay = SH_MSG_TIMEOUT_MS + delay;
        slot->next_profile_retry_ms = now_ms() + fallback_delay;
        ESP_LOGI(TAG, "Requested profile from 0x%04x (attempt %u, fallback in %" PRIu32 " ms)",
                 addr, (unsigned)slot->profile_retry_count, fallback_delay);
    } else {
        slot->profile_request_pending = false;
        slot->next_profile_retry_ms = now_ms() + SH_PROFILE_RETRY_FAST_MS;
        ESP_LOGW(TAG, "Failed to request profile from 0x%04x: %s",
                 addr, esp_err_to_name(err));
    }
    return err;
}

static void profile_retry_timer_cb(void *arg)
{
    (void)arg;

    if (s_own_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return;
    }

    uint32_t now = now_ms();
    for (uint8_t i = 0; i < s_device_count; i++) {
        sh_remote_device_slot_t *slot = &s_devices[i];
        if (!slot->public_info.is_online || slot->public_info.profile_loaded) {
            continue;
        }
        if (slot->profile_request_pending) {
            continue;
        }
        if (slot->next_profile_retry_ms != 0 && now < slot->next_profile_retry_ms) {
            continue;
        }
        request_profile_for_device(slot->public_info.addr);
    }
}

static void directory_retry_timer_cb(void *arg)
{
    (void)arg;

    if (s_own_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return;
    }

    uint32_t now = now_ms();
    if (s_next_directory_request_ms != 0 && now < s_next_directory_request_ms) {
        return;
    }

    request_device_directory();
}

static void handle_directory(const uint8_t *data, uint16_t len)
{
    sh_msg_header_t header;
    size_t offset = 0;
    if (sh_protocol_read_header(data, len, &header, &offset) != ESP_OK ||
        offset >= len) {
        return;
    }

    uint8_t count = data[offset++];
    ESP_LOGI(TAG, "Received device directory count=%u", count);
    s_directory_request_count = 0;
    s_next_directory_request_ms = now_ms() + (count == 0 ?
        SH_DIRECTORY_RETRY_SLOW_MS : SH_DIRECTORY_REFRESH_MS);
    for (uint8_t i = 0; i < count && offset + 2 <= len; i++) {
        uint16_t addr = (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
        offset += 2;
        sh_remote_device_slot_t *slot = add_device(addr);
        if (slot) {
            if (!slot->public_info.profile_loaded && slot->next_profile_retry_ms == 0) {
                slot->next_profile_retry_ms = now_ms();
            }
            ensure_profile_retry_timer();
            if (s_callbacks.online_cb) {
                s_callbacks.online_cb(addr, true);
            }
            if (s_callbacks.provisioned_cb) {
                s_callbacks.provisioned_cb(addr);
            }
            request_profile_for_device(addr);
        }
    }
}

static void handle_profile_status(uint16_t addr, const uint8_t *data, uint16_t len)
{
    if (!data || len < sizeof(sh_profile_chunk_header_t)) {
        return;
    }

    sh_remote_device_slot_t *slot = add_device(addr);
    if (!slot) {
        return;
    }

    sh_profile_chunk_header_t header;
    memcpy(&header, data, sizeof(header));
    if (header.version != SH_PROTOCOL_VERSION ||
        header.chunk_index >= header.chunk_count ||
        header.chunk_len > SH_PROFILE_CHUNK_PAYLOAD ||
        sizeof(header) + header.chunk_len > len ||
        header.total_len > sizeof(slot->profile_blob)) {
        ESP_LOGW(TAG, "Invalid profile status from 0x%04x len=%u", addr, (unsigned)len);
        return;
    }

    ESP_LOGI(TAG, "Profile status chunk from 0x%04x: %u/%u len=%u total=%u",
             addr, (unsigned)header.chunk_index + 1, (unsigned)header.chunk_count,
             (unsigned)header.chunk_len, (unsigned)header.total_len);
    slot->profile_request_pending = false;

    if (header.chunk_index == 0) {
        slot->profile_blob_len = 0;
        slot->profile_expected_len = header.total_len;
        slot->profile_crc = header.crc16;
        slot->profile_chunk_count = header.chunk_count;
        slot->profile_chunks_seen = 0;
        memset(slot->profile_blob, 0, sizeof(slot->profile_blob));
    }
    if (header.total_len != slot->profile_expected_len ||
        header.crc16 != slot->profile_crc ||
        header.chunk_count != slot->profile_chunk_count) {
        return;
    }

    uint16_t chunk_offset = header.chunk_index * SH_PROFILE_CHUNK_PAYLOAD;
    if (chunk_offset + header.chunk_len > sizeof(slot->profile_blob)) {
        return;
    }
    memcpy(slot->profile_blob + chunk_offset, data + sizeof(header), header.chunk_len);
    if (chunk_offset + header.chunk_len > slot->profile_blob_len) {
        slot->profile_blob_len = chunk_offset + header.chunk_len;
    }
    slot->profile_chunks_seen++;
    if (slot->profile_chunks_seen < slot->profile_chunk_count) {
        return;
    }
    if (slot->profile_blob_len != slot->profile_expected_len ||
        sh_protocol_crc16(slot->profile_blob, slot->profile_blob_len) != slot->profile_crc) {
        ESP_LOGW(TAG, "Profile CRC/length mismatch for 0x%04x, retrying", addr);
        slot->next_profile_retry_ms = now_ms() + SH_PROFILE_RETRY_FAST_MS;
        return;
    }

    const sh_device_profile_t *profile = NULL;
    esp_err_t err = sh_model_deserialize_profile(slot->profile_blob, slot->profile_blob_len,
                                                 &slot->profile_storage, &profile);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse profile from 0x%04x: %s", addr, esp_err_to_name(err));
        return;
    }

    slot->public_info.profile = profile;
    slot->public_info.profile_loaded = true;
    slot->profile_retry_count = 0;
    slot->next_profile_retry_ms = 0;
    slot->profile_request_pending = false;
    sh_model_default_states(profile, slot->public_info.states,
                            SH_MODEL_MAX_FEATURES, &slot->public_info.state_count);
    strncpy(slot->public_info.device_name, profile->display_name,
            sizeof(slot->public_info.device_name) - 1);
    slot->public_info.device_name[sizeof(slot->public_info.device_name) - 1] = '\0';
    slot->public_info.last_update_time = now_ms();

    ESP_LOGI(TAG, "Loaded profile 0x%04x (%s) from 0x%04x",
             profile->profile_id, profile->display_name, addr);
    if (s_callbacks.profile_cb) {
        s_callbacks.profile_cb(addr, profile);
    }
    sh_remote_refresh_device(addr);
}

static void handle_feature_status(uint16_t addr, const uint8_t *data, uint16_t len)
{
    sh_msg_header_t header;
    sh_tlv_t tlv;
    size_t offset = 0;
    size_t consumed = 0;
    int32_t value = 0;

    if (sh_protocol_read_header(data, len, &header, &offset) != ESP_OK ||
        sh_tlv_decode(data + offset, len - offset, &tlv, &consumed) != ESP_OK ||
        sh_tlv_value_to_i32(&tlv, &value) != ESP_OK) {
        return;
    }

    sh_remote_device_slot_t *slot = add_device(addr);
    if (!slot) {
        return;
    }
    slot->public_info.is_online = true;
    slot->public_info.last_update_time = now_ms();
    sh_feature_state_t *state = sh_model_find_state(slot->public_info.states,
                                                    slot->public_info.state_count,
                                                    tlv.feature_id);
    if (state) {
        state->type = tlv.type;
        state->value = value;
    }
    ESP_LOGI(TAG, "Feature status from 0x%04x: feature=0x%04x value=%" PRId32,
             addr, tlv.feature_id, value);
    if (s_callbacks.feature_status_cb) {
        s_callbacks.feature_status_cb(addr, tlv.feature_id, tlv.type, value);
    }
}

static void handle_operation(uint32_t opcode, const uint8_t *data, uint16_t len, uint16_t addr)
{
    switch (opcode) {
    case SH_OP_DEVICE_DIRECTORY:
        handle_directory(data, len);
        break;
    case SH_OP_PROFILE_STATUS:
        handle_profile_status(addr, data, len);
        break;
    case SH_OP_FEATURE_STATUS:
        handle_feature_status(addr, data, len);
        break;
    default:
        ESP_LOGW(TAG, "Unknown opcode 0x%06" PRIx32 " from 0x%04x", opcode, addr);
        break;
    }
}

static void model_cb(esp_ble_mesh_model_cb_event_t event,
                     esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        handle_operation(param->model_operation.opcode,
                         param->model_operation.msg,
                         param->model_operation.length,
                         param->model_operation.ctx->addr);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        handle_operation(param->client_recv_publish_msg.opcode,
                         param->client_recv_publish_msg.msg,
                         param->client_recv_publish_msg.length,
                         param->client_recv_publish_msg.ctx->addr);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT: {
        uint16_t addr = param->client_send_timeout.ctx->addr;
        ESP_LOGW(TAG, "Send timeout opcode=0x%06" PRIx32 " addr=0x%04x",
                 param->client_send_timeout.opcode, addr);
        if (param->client_send_timeout.opcode == SH_OP_DEVICE_DIRECTORY_GET) {
            uint32_t delay = s_directory_request_count <= SH_DIRECTORY_RETRY_FAST_COUNT ?
                SH_DIRECTORY_RETRY_FAST_MS : SH_DIRECTORY_RETRY_SLOW_MS;
            s_next_directory_request_ms = now_ms() + delay;
            ESP_LOGW(TAG, "Device directory request timeout, retry in %" PRIu32 " ms", delay);
            ensure_directory_retry_timer();
        } else if (param->client_send_timeout.opcode == SH_OP_PROFILE_GET) {
            sh_remote_device_slot_t *slot = find_device(addr);
            if (slot && !slot->public_info.profile_loaded) {
                slot->profile_request_pending = false;
                uint32_t delay = slot->profile_retry_count <= SH_PROFILE_RETRY_FAST_COUNT ?
                    SH_PROFILE_RETRY_FAST_MS : SH_PROFILE_RETRY_SLOW_MS;
                slot->next_profile_retry_ms = now_ms() + delay;
                ESP_LOGW(TAG, "Profile request timeout for 0x%04x, retry in %" PRIu32 " ms",
                         addr, delay);
                ensure_profile_retry_timer();
            }
        }
        break;
    }
    default:
        break;
    }
}

static void prov_complete(uint16_t net_idx, uint16_t addr)
{
    s_net_idx = net_idx;
    s_own_addr = addr;
    ESP_LOGI(TAG, "Remote controller provisioned addr=0x%04x net=0x%03x", addr, net_idx);
    schedule_device_directory_request(500);
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                            esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        prov_complete(param->node_prov_complete.net_idx,
                      param->node_prov_complete.addr);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)
                                      (ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        break;
    default:
        break;
    }
}

esp_err_t sh_remote_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = sh_profiles_register_builtin();
    if (err != ESP_OK) {
        return err;
    }
    err = sh_store_init(SH_STORE_NAMESPACE_DEFAULT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Smart-home store init failed: %s", esp_err_to_name(err));
    }
    init_device_uuid();

    s_vendor_client.op_pair_size = ARRAY_SIZE(s_remote_op_pair);
    s_vendor_client.op_pair = s_remote_op_pair;

    esp_ble_mesh_register_prov_callback(provisioning_cb);
    esp_ble_mesh_register_custom_model_callback(model_cb);

    err = esp_ble_mesh_init(&s_provision, &s_composition);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_client_model_init(&s_vendor_models[0]);
    if (err != ESP_OK) {
        return err;
    }
    s_vendor_client.model = &s_vendor_models[0];

    if (esp_ble_mesh_node_is_provisioned()) {
        s_own_addr = esp_ble_mesh_get_primary_element_address();
        ESP_LOGI(TAG, "Remote restored provisioned state addr=0x%04x", s_own_addr);
        schedule_device_directory_request(500);
    } else {
        err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)
                                            (ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        if (err != ESP_OK) {
            return err;
        }
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Remote smart-home controller initialized");
    return ESP_OK;
}

void sh_remote_register_callbacks(const sh_client_callbacks_t *callbacks)
{
    if (callbacks) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }
}

uint8_t sh_remote_get_device_count(void) { return s_device_count; }
uint8_t sh_remote_get_online_device_count(void)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < s_device_count; i++) {
        if (s_devices[i].public_info.is_online && s_devices[i].public_info.is_configured) {
            count++;
        }
    }
    return count;
}
uint8_t sh_remote_get_device_list(sh_client_device_t *devices, uint8_t max_devices)
{
    if (!devices || max_devices == 0) {
        return 0;
    }
    uint8_t count = s_device_count < max_devices ? s_device_count : max_devices;
    for (uint8_t i = 0; i < count; i++) {
        devices[i] = s_devices[i].public_info;
    }
    return count;
}
esp_err_t sh_remote_get_device_by_index(uint8_t index, sh_client_device_t *device)
{
    if (!device || index >= s_device_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *device = s_devices[index].public_info;
    return ESP_OK;
}
esp_err_t sh_remote_get_device_by_addr(uint16_t addr, sh_client_device_t *device)
{
    sh_remote_device_slot_t *slot = find_device(addr);
    if (!slot || !device) {
        return ESP_ERR_NOT_FOUND;
    }
    *device = slot->public_info;
    return ESP_OK;
}
uint16_t sh_remote_get_device_addr_by_index(uint8_t index)
{
    return index < s_device_count ? s_devices[index].public_info.addr : ESP_BLE_MESH_ADDR_UNASSIGNED;
}
bool sh_remote_is_device_online(uint16_t addr)
{
    sh_remote_device_slot_t *slot = find_device(addr);
    return slot && slot->public_info.is_online && slot->public_info.is_configured;
}
bool sh_remote_is_device_set_cmd_responsive(uint16_t addr)
{
    sh_remote_device_slot_t *slot = find_device(addr);
    return slot && !slot->public_info.is_set_cmd_unresponsive;
}
esp_err_t sh_remote_get_profile(uint16_t addr) { return request_profile_for_device(addr); }

esp_err_t sh_remote_set_profile(uint16_t addr, const sh_device_profile_t *profile)
{
    if (!profile) {
        return ESP_ERR_INVALID_ARG;
    }

    sh_remote_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t blob_len = 0;
    ESP_RETURN_ON_ERROR(sh_model_serialize_profile(profile, slot->profile_blob,
                                                   sizeof(slot->profile_blob), &blob_len),
                        TAG, "serialize profile set");

    uint16_t crc = sh_protocol_crc16(slot->profile_blob, blob_len);
    uint8_t tid = sh_protocol_next_tid();
    uint8_t chunk_count = (blob_len + SH_PROFILE_CHUNK_PAYLOAD - 1) / SH_PROFILE_CHUNK_PAYLOAD;

    for (uint8_t i = 0; i < chunk_count; i++) {
        uint16_t offset = i * SH_PROFILE_CHUNK_PAYLOAD;
        uint8_t chunk_len = (blob_len - offset) > SH_PROFILE_CHUNK_PAYLOAD ?
            SH_PROFILE_CHUNK_PAYLOAD : (uint8_t)(blob_len - offset);

        uint8_t payload[sizeof(sh_profile_chunk_header_t) + SH_PROFILE_CHUNK_PAYLOAD];
        sh_profile_chunk_header_t header = {
            .version = SH_PROTOCOL_VERSION,
            .tid = tid,
            .profile_id = profile->profile_id,
            .profile_version = profile->version,
            .total_len = (uint16_t)blob_len,
            .crc16 = crc,
            .chunk_index = i,
            .chunk_count = chunk_count,
            .chunk_len = chunk_len,
        };
        memcpy(payload, &header, sizeof(header));
        memcpy(payload + sizeof(header), &slot->profile_blob[offset], chunk_len);

        esp_err_t err = send_vendor(addr, SH_OP_PROFILE_SET, payload,
                                    sizeof(header) + chunk_len,
                                    i + 1 == chunk_count);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send profile chunk %u/%u to 0x%04x: %s",
                     i + 1, chunk_count, addr, esp_err_to_name(err));
            return err;
        }
    }

    slot->profile_blob_len = (uint16_t)blob_len;
    slot->profile_expected_len = (uint16_t)blob_len;
    slot->profile_crc = crc;
    slot->profile_chunk_count = chunk_count;
    slot->profile_chunks_seen = chunk_count;
    slot->profile_retry_count = 0;
    slot->next_profile_retry_ms = 0;
    slot->profile_request_pending = false;
    slot->public_info.profile = profile;
    slot->public_info.profile_loaded = true;
    slot->public_info.last_update_time = now_ms();
    sh_model_default_states(profile, slot->public_info.states,
                            SH_MODEL_MAX_FEATURES, &slot->public_info.state_count);
    strncpy(slot->public_info.device_name, profile->display_name,
            sizeof(slot->public_info.device_name) - 1);
    slot->public_info.device_name[sizeof(slot->public_info.device_name) - 1] = '\0';

    if (s_callbacks.profile_cb) {
        s_callbacks.profile_cb(addr, profile);
    }
    ESP_LOGI(TAG, "Sent profile 0x%04x (%s) to 0x%04x",
             profile->profile_id, profile->display_name, addr);
    return ESP_OK;
}

esp_err_t sh_remote_get_feature(uint16_t addr, uint16_t feature_id)
{
    uint8_t payload[4];
    size_t written = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &written),
                        TAG, "feature get header");
    payload[written++] = (uint8_t)(feature_id & 0xFF);
    payload[written++] = (uint8_t)((feature_id >> 8) & 0xFF);
    esp_err_t err = send_vendor(addr, SH_OP_FEATURE_GET, payload, written, true);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Requested feature 0x%04x from 0x%04x", feature_id, addr);
    } else {
        ESP_LOGW(TAG, "Failed to request feature 0x%04x from 0x%04x: %s",
                 feature_id, addr, esp_err_to_name(err));
    }
    return err;
}
esp_err_t sh_remote_set_feature(uint16_t addr, uint16_t feature_id, int32_t value)
{
    uint8_t payload[2 + 8];
    size_t offset = 0;
    size_t tlv_len = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &offset),
                        TAG, "feature set header");
    esp_err_t err;
    if (value >= 0 && value <= 255) {
        err = sh_tlv_encode_u8(payload + offset, sizeof(payload) - offset,
                               feature_id, SH_FEATURE_TYPE_ENUM, (uint8_t)value, &tlv_len);
    } else {
        err = sh_tlv_encode_i32(payload + offset, sizeof(payload) - offset,
                                feature_id, value, &tlv_len);
    }
    if (err != ESP_OK) {
        return err;
    }
    return send_vendor(addr, SH_OP_FEATURE_SET, payload, offset + tlv_len, true);
}
esp_err_t sh_remote_group_set_feature(uint16_t feature_id, int32_t value)
{
    uint8_t payload[2 + 8];
    size_t offset = 0;
    size_t tlv_len = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &offset),
                        TAG, "group set header");
    esp_err_t err;
    if (value >= 0 && value <= 255) {
        err = sh_tlv_encode_u8(payload + offset, sizeof(payload) - offset,
                               feature_id, SH_FEATURE_TYPE_ENUM, (uint8_t)value, &tlv_len);
    } else {
        err = sh_tlv_encode_i32(payload + offset, sizeof(payload) - offset,
                                feature_id, value, &tlv_len);
    }
    if (err != ESP_OK) {
        return err;
    }
    return send_vendor(SH_GROUP_ADDR_DEFAULT, SH_OP_FEATURE_SET, payload, offset + tlv_len, false);
}
esp_err_t sh_remote_refresh_device(uint16_t addr)
{
    sh_remote_device_slot_t *slot = find_device(addr);
    if (!slot || !slot->public_info.profile) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint8_t i = 0; i < slot->public_info.profile->feature_count; i++) {
        esp_err_t err = sh_remote_get_feature(addr, slot->public_info.profile->features[i].feature_id);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to request feature 0x%04x from 0x%04x: %s",
                     slot->public_info.profile->features[i].feature_id, addr, esp_err_to_name(err));
        }
    }
    return ESP_OK;
}
esp_err_t sh_remote_refresh_all(void)
{
    for (uint8_t i = 0; i < s_device_count; i++) {
        if (s_devices[i].public_info.is_online) {
            sh_remote_refresh_device(s_devices[i].public_info.addr);
        }
    }
    return ESP_OK;
}
uint16_t sh_remote_get_group_address(void) { return SH_GROUP_ADDR_DEFAULT; }
bool sh_remote_is_device_in_group(uint16_t addr)
{
    sh_remote_device_slot_t *slot = find_device(addr);
    return slot && slot->public_info.is_in_group;
}

#else

esp_err_t sh_remote_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void sh_remote_register_callbacks(const sh_client_callbacks_t *callbacks) { (void)callbacks; }
uint8_t sh_remote_get_device_count(void) { return 0; }
uint8_t sh_remote_get_online_device_count(void) { return 0; }
uint8_t sh_remote_get_device_list(sh_client_device_t *devices, uint8_t max_devices) { (void)devices; (void)max_devices; return 0; }
esp_err_t sh_remote_get_device_by_index(uint8_t index, sh_client_device_t *device) { (void)index; (void)device; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_remote_get_device_by_addr(uint16_t addr, sh_client_device_t *device) { (void)addr; (void)device; return ESP_ERR_NOT_SUPPORTED; }
uint16_t sh_remote_get_device_addr_by_index(uint8_t index) { (void)index; return ESP_BLE_MESH_ADDR_UNASSIGNED; }
bool sh_remote_is_device_online(uint16_t addr) { (void)addr; return false; }
bool sh_remote_is_device_set_cmd_responsive(uint16_t addr) { (void)addr; return false; }
esp_err_t sh_remote_get_profile(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_remote_set_profile(uint16_t addr, const sh_device_profile_t *profile) { (void)addr; (void)profile; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_remote_get_feature(uint16_t addr, uint16_t feature_id) { (void)addr; (void)feature_id; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_remote_set_feature(uint16_t addr, uint16_t feature_id, int32_t value) { (void)addr; (void)feature_id; (void)value; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_remote_group_set_feature(uint16_t feature_id, int32_t value) { (void)feature_id; (void)value; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_remote_refresh_device(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_remote_refresh_all(void) { return ESP_ERR_NOT_SUPPORTED; }
uint16_t sh_remote_get_group_address(void) { return SH_GROUP_ADDR_DEFAULT; }
bool sh_remote_is_device_in_group(uint16_t addr) { (void)addr; return false; }

#endif
