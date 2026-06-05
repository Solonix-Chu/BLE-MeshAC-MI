#include "smarthome_node.h"
#include "sdkconfig.h"

#if CONFIG_BLE_MESH_NODE

#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "ble_mesh_example_init.h"
#include "smarthome_protocol.h"
#include "smarthome_profiles.h"
#include "smarthome_store.h"

#define TAG "SH_NODE"
#define SH_PROFILE_BLOB_MAX 512

#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(x, tag, fmt, ...) do { \
        esp_err_t err_rc_ = (x);                    \
        if (err_rc_ != ESP_OK) {                    \
            return err_rc_;                         \
        }                                           \
    } while (0)
#endif

static uint8_t s_dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = {0xDD, 0xDD};
static const sh_device_profile_t *s_profile;
static sh_feature_state_t s_states[SH_MODEL_MAX_FEATURES];
static size_t s_state_count;
static sh_node_callbacks_t s_callbacks;
static sh_dynamic_profile_t s_dynamic_profile_storage;
static uint16_t s_device_addr;
static uint16_t s_client_addr;
static bool s_connected;
static uint8_t s_profile_blob[SH_PROFILE_BLOB_MAX];
static size_t s_profile_blob_len;
static uint8_t s_profile_set_blob[SH_PROFILE_BLOB_MAX];
static uint16_t s_profile_set_blob_len;
static uint16_t s_profile_set_expected_len;
static uint16_t s_profile_set_crc;
static uint8_t s_profile_set_chunks_seen;
static uint8_t s_profile_set_chunk_count;

static esp_ble_mesh_cfg_srv_t s_config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(3, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(3, 20),
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
    .default_ttl = 7,
};

static esp_ble_mesh_model_t s_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_config_server),
};

static esp_ble_mesh_model_op_t s_node_ops[] = {
    ESP_BLE_MESH_MODEL_OP(SH_OP_PROFILE_GET, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_PROFILE_SET, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_FEATURE_GET, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_FEATURE_SET, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_DISCONNECT_NOTIFY, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t s_vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(SH_COMPANY_ID, SH_MODEL_ID_NODE,
                              s_node_ops, NULL, NULL),
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

static void notify_feature_changed(const sh_feature_state_t *state)
{
    if (s_callbacks.feature_changed_cb && state) {
        s_callbacks.feature_changed_cb(state->feature_id, state->type, state->value,
                                       s_callbacks.user_data);
    }
}

static void notify_profile_changed(void)
{
    if (s_callbacks.profile_changed_cb && s_profile) {
        s_callbacks.profile_changed_cb(s_profile, s_callbacks.user_data);
    }
}

static void notify_provisioned(bool is_provisioned, uint16_t addr)
{
    if (s_callbacks.provisioned_cb) {
        s_callbacks.provisioned_cb(is_provisioned, addr, s_callbacks.user_data);
    }
}

static esp_err_t send_feature_status(esp_ble_mesh_msg_ctx_t *ctx, const sh_feature_state_t *state, uint8_t tid)
{
    if (!ctx || !state) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t payload[2 + 8];
    size_t offset = 0;
    size_t tlv_len = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload), tid, &offset),
                        TAG, "status header");

    esp_err_t err;
    if (state->type == SH_FEATURE_TYPE_INT && (state->value < 0 || state->value > 255)) {
        err = sh_tlv_encode_i32(payload + offset, sizeof(payload) - offset,
                                state->feature_id, state->value, &tlv_len);
    } else {
        err = sh_tlv_encode_u8(payload + offset, sizeof(payload) - offset,
                               state->feature_id, state->type,
                               (uint8_t)state->value, &tlv_len);
    }
    if (err != ESP_OK) {
        return err;
    }

    return esp_ble_mesh_server_model_send_msg(&s_vendor_models[0], ctx,
                                              SH_OP_FEATURE_STATUS,
                                              offset + tlv_len, payload);
}

static esp_err_t publish_feature_status_to_group(const esp_ble_mesh_msg_ctx_t *rx_ctx,
                                                 const sh_feature_state_t *state,
                                                 uint8_t tid)
{
    if (!rx_ctx || !state) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_ble_mesh_msg_ctx_t group_ctx = *rx_ctx;
    group_ctx.addr = SH_GROUP_ADDR_DEFAULT;
    group_ctx.recv_dst = 0;
    ESP_LOGI(TAG, "Publish feature status to group 0x%04x: feature=0x%04x value=%" PRId32,
             SH_GROUP_ADDR_DEFAULT, state->feature_id, state->value);
    return send_feature_status(&group_ctx, state, tid);
}

static esp_err_t send_profile_status(esp_ble_mesh_msg_ctx_t *ctx, uint8_t tid)
{
    if (!ctx || !s_profile || s_profile_blob_len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t crc = sh_protocol_crc16(s_profile_blob, s_profile_blob_len);
    uint8_t chunk_count = (s_profile_blob_len + SH_PROFILE_CHUNK_PAYLOAD - 1) / SH_PROFILE_CHUNK_PAYLOAD;
    ESP_LOGI(TAG, "Sending profile 0x%04x (%s), len=%u, chunks=%u to 0x%04x",
             s_profile->profile_id,
             s_profile->display_name ? s_profile->display_name : "Profile",
             (unsigned)s_profile_blob_len,
             chunk_count,
             ctx->addr);

    for (uint8_t i = 0; i < chunk_count; i++) {
        uint16_t offset = i * SH_PROFILE_CHUNK_PAYLOAD;
        uint8_t chunk_len = (s_profile_blob_len - offset) > SH_PROFILE_CHUNK_PAYLOAD ?
            SH_PROFILE_CHUNK_PAYLOAD : (uint8_t)(s_profile_blob_len - offset);

        uint8_t payload[sizeof(sh_profile_chunk_header_t) + SH_PROFILE_CHUNK_PAYLOAD];
        sh_profile_chunk_header_t header = {
            .version = SH_PROTOCOL_VERSION,
            .tid = tid,
            .profile_id = s_profile->profile_id,
            .profile_version = s_profile->version,
            .total_len = (uint16_t)s_profile_blob_len,
            .crc16 = crc,
            .chunk_index = i,
            .chunk_count = chunk_count,
            .chunk_len = chunk_len,
        };
        memcpy(payload, &header, sizeof(header));
        memcpy(payload + sizeof(header), &s_profile_blob[offset], chunk_len);

        esp_err_t err = esp_ble_mesh_server_model_send_msg(&s_vendor_models[0], ctx,
                                                           SH_OP_PROFILE_STATUS,
                                                           sizeof(header) + chunk_len, payload);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send profile chunk %u/%u: %s",
                     i + 1, chunk_count, esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

static esp_err_t publish_profile_status_to_group(const esp_ble_mesh_msg_ctx_t *rx_ctx,
                                                 uint8_t tid)
{
    if (!rx_ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_ble_mesh_msg_ctx_t group_ctx = *rx_ctx;
    group_ctx.addr = SH_GROUP_ADDR_DEFAULT;
    group_ctx.recv_dst = 0;
    ESP_LOGI(TAG, "Publish profile status to group 0x%04x: profile=0x%04x (%s)",
             SH_GROUP_ADDR_DEFAULT,
             s_profile ? s_profile->profile_id : 0,
             (s_profile && s_profile->display_name) ? s_profile->display_name : "Profile");
    return send_profile_status(&group_ctx, tid);
}

static esp_err_t apply_feature(uint16_t feature_id, int32_t value, sh_feature_state_t **out_state)
{
    if (!s_profile) {
        return ESP_ERR_INVALID_STATE;
    }

    const sh_feature_def_t *feature = sh_model_find_feature(s_profile, feature_id);
    if (!feature || !(feature->flags & SH_FEATURE_FLAG_WRITABLE)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_RETURN_ON_ERROR(sh_model_validate_value(feature, value), TAG, "validate feature");

    sh_feature_state_t *state = sh_model_find_state(s_states, s_state_count, feature_id);
    if (!state) {
        return ESP_ERR_NOT_FOUND;
    }
    state->value = value;
    state->type = feature->type;
    sh_store_save_feature(s_profile->profile_id, feature_id, value);
    notify_feature_changed(state);

    if (out_state) {
        *out_state = state;
    }
    return ESP_OK;
}

static void handle_feature_get(esp_ble_mesh_msg_ctx_t *ctx, const uint8_t *msg, uint16_t len)
{
    sh_msg_header_t header;
    size_t offset = 0;
    if (sh_protocol_read_header(msg, len, &header, &offset) != ESP_OK ||
        offset + 2 > len) {
        return;
    }
    uint16_t feature_id = (uint16_t)msg[offset] | ((uint16_t)msg[offset + 1] << 8);
    sh_feature_state_t *state = sh_model_find_state(s_states, s_state_count, feature_id);
    if (state) {
        send_feature_status(ctx, state, header.tid);
    }
}

static void handle_feature_set(esp_ble_mesh_msg_ctx_t *ctx, const uint8_t *msg, uint16_t len)
{
    sh_msg_header_t header;
    sh_tlv_t tlv;
    size_t offset = 0;
    size_t consumed = 0;
    int32_t value = 0;
    if (sh_protocol_read_header(msg, len, &header, &offset) != ESP_OK ||
        sh_tlv_decode(msg + offset, len - offset, &tlv, &consumed) != ESP_OK ||
        sh_tlv_value_to_i32(&tlv, &value) != ESP_OK) {
        return;
    }

    sh_feature_state_t *state = NULL;
    esp_err_t err = apply_feature(tlv.feature_id, value, &state);
    bool is_group = ctx->recv_dst == SH_GROUP_ADDR_DEFAULT;
    if (err == ESP_OK && state) {
        ESP_LOGI(TAG, "Feature set from 0x%04x dst=0x%04x%s: feature=0x%04x value=%" PRId32,
                 ctx->addr, ctx->recv_dst, is_group ? " (group)" : "",
                 state->feature_id, state->value);
        if (!is_group) {
            send_feature_status(ctx, state, header.tid);
        }
        publish_feature_status_to_group(ctx, state, header.tid);
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Feature set failed id=0x%04x value=%" PRId32 ": %s",
                 tlv.feature_id, value, esp_err_to_name(err));
    }
}

static void handle_profile_set(esp_ble_mesh_msg_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    if (!ctx || !data || len < sizeof(sh_profile_chunk_header_t)) {
        return;
    }

    sh_profile_chunk_header_t header;
    memcpy(&header, data, sizeof(header));
    if (header.version != SH_PROTOCOL_VERSION ||
        header.chunk_len > SH_PROFILE_CHUNK_PAYLOAD ||
        sizeof(header) + header.chunk_len > len ||
        header.total_len > SH_PROFILE_BLOB_MAX ||
        header.chunk_index >= header.chunk_count) {
        ESP_LOGW(TAG, "Invalid profile set chunk");
        return;
    }

    if (header.chunk_index == 0) {
        s_profile_set_blob_len = 0;
        s_profile_set_chunks_seen = 0;
        s_profile_set_chunk_count = header.chunk_count;
        s_profile_set_expected_len = header.total_len;
        s_profile_set_crc = header.crc16;
        memset(s_profile_set_blob, 0, sizeof(s_profile_set_blob));
    }

    uint16_t offset = header.chunk_index * SH_PROFILE_CHUNK_PAYLOAD;
    if (offset + header.chunk_len > sizeof(s_profile_set_blob)) {
        return;
    }
    memcpy(&s_profile_set_blob[offset], data + sizeof(header), header.chunk_len);
    if (offset + header.chunk_len > s_profile_set_blob_len) {
        s_profile_set_blob_len = offset + header.chunk_len;
    }
    s_profile_set_chunks_seen++;

    if (s_profile_set_chunks_seen < s_profile_set_chunk_count) {
        return;
    }
    if (s_profile_set_blob_len != s_profile_set_expected_len ||
        sh_protocol_crc16(s_profile_set_blob, s_profile_set_blob_len) != s_profile_set_crc) {
        ESP_LOGW(TAG, "Profile set CRC/length mismatch");
        return;
    }

    const sh_device_profile_t *profile = NULL;
    esp_err_t err = sh_model_deserialize_profile(s_profile_set_blob, s_profile_set_blob_len,
                                                 &s_dynamic_profile_storage, &profile);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse profile set: %s", esp_err_to_name(err));
        return;
    }

    s_profile = profile;
    memcpy(s_profile_blob, s_profile_set_blob, s_profile_set_blob_len);
    s_profile_blob_len = s_profile_set_blob_len;
    if (sh_model_default_states(s_profile, s_states,
                                SH_MODEL_MAX_FEATURES, &s_state_count) == ESP_OK) {
        sh_node_load_state();
    }
    sh_store_save_profile_blob(s_profile->profile_id, s_profile_blob, s_profile_blob_len);
    sh_store_save_active_profile(s_profile->profile_id);
    notify_profile_changed();

    ESP_LOGI(TAG, "Applied profile set 0x%04x (%s)",
             s_profile->profile_id, s_profile->display_name);
    send_profile_status(ctx, header.tid);
    publish_profile_status_to_group(ctx, header.tid);
}

static void model_cb(esp_ble_mesh_model_cb_event_t event,
                     esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT: {
        uint32_t opcode = param->model_operation.opcode;
        esp_ble_mesh_msg_ctx_t *ctx = param->model_operation.ctx;
        s_client_addr = ctx->addr;
        s_connected = true;

        switch (opcode) {
        case SH_OP_PROFILE_GET: {
            sh_msg_header_t header;
            size_t offset = 0;
            if (sh_protocol_read_header(param->model_operation.msg,
                                        param->model_operation.length,
                                        &header, &offset) == ESP_OK) {
                ESP_LOGI(TAG, "Profile get from 0x%04x", ctx->addr);
                send_profile_status(ctx, header.tid);
            }
            break;
        }
        case SH_OP_PROFILE_SET:
            handle_profile_set(ctx, param->model_operation.msg, param->model_operation.length);
            break;
        case SH_OP_FEATURE_GET:
            handle_feature_get(ctx, param->model_operation.msg, param->model_operation.length);
            break;
        case SH_OP_FEATURE_SET:
            handle_feature_set(ctx, param->model_operation.msg, param->model_operation.length);
            break;
        case SH_OP_DISCONNECT_NOTIFY: {
            uint8_t payload[2];
            size_t written = 0;
            sh_protocol_write_header(payload, sizeof(payload), sh_protocol_next_tid(), &written);
            esp_ble_mesh_server_model_send_msg(&s_vendor_models[0], ctx,
                                               SH_OP_DISCONNECT_ACK, written, payload);
            s_connected = false;
            s_client_addr = 0;
            if (s_callbacks.connection_cb) {
                s_callbacks.connection_cb(false, 0, s_callbacks.user_data);
            }
            if (s_callbacks.reset_requested_cb) {
                s_callbacks.reset_requested_cb(s_callbacks.user_data);
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown opcode 0x%06" PRIx32, opcode);
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    (void)net_idx;
    (void)flags;
    (void)iv_index;
    s_device_addr = addr;
    ESP_LOGI(TAG, "Provisioning complete addr=0x%04x", addr);
    notify_provisioned(true, addr);
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                            esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        prov_complete(param->node_prov_complete.net_idx,
                      param->node_prov_complete.addr,
                      param->node_prov_complete.flags,
                      param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        s_device_addr = 0;
        s_client_addr = 0;
        s_connected = false;
        notify_provisioned(false, 0);
        if (s_callbacks.connection_cb) {
            s_callbacks.connection_cb(false, 0, s_callbacks.user_data);
        }
        break;
    default:
        break;
    }
}

static void config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                             esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        return;
    }

    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND &&
        param->value.state_change.mod_app_bind.company_id == SH_COMPANY_ID &&
        param->value.state_change.mod_app_bind.model_id == SH_MODEL_ID_NODE) {
        s_client_addr = param->ctx.addr;
        s_connected = true;
        ESP_LOGI(TAG, "Smart-home node model bound by client 0x%04x", s_client_addr);
        if (s_callbacks.connection_cb) {
            s_callbacks.connection_cb(true, s_client_addr, s_callbacks.user_data);
        }
    }
}

esp_err_t sh_node_init(const sh_device_profile_t *profile, const sh_node_callbacks_t *callbacks)
{
    if (!profile) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(sh_profiles_register_builtin(), TAG, "register profiles");
    ESP_RETURN_ON_ERROR(sh_store_init(SH_STORE_NAMESPACE_DEFAULT), TAG, "store init");

    s_profile = profile;
    s_profile_blob_len = 0;
    uint16_t active_profile_id = 0;
    if (sh_store_load_active_profile(&active_profile_id) == ESP_OK) {
        size_t loaded_len = 0;
        const sh_device_profile_t *loaded_profile = NULL;
        esp_err_t load_err = sh_store_load_profile_blob(active_profile_id,
                                                        s_profile_blob,
                                                        sizeof(s_profile_blob),
                                                        &loaded_len);
        if (load_err == ESP_OK &&
            sh_model_deserialize_profile(s_profile_blob, loaded_len,
                                         &s_dynamic_profile_storage,
                                         &loaded_profile) == ESP_OK) {
            s_profile = loaded_profile;
            s_profile_blob_len = loaded_len;
            ESP_LOGI(TAG, "Restored active profile 0x%04x (%s)",
                     s_profile->profile_id, s_profile->display_name);
        } else {
            const sh_device_profile_t *builtin = sh_profiles_get_builtin(active_profile_id);
            if (builtin) {
                s_profile = builtin;
                ESP_LOGI(TAG, "Restored active built-in profile 0x%04x (%s)",
                         s_profile->profile_id, s_profile->display_name);
            }
        }
    }

    if (callbacks) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }

    ESP_RETURN_ON_ERROR(sh_model_default_states(s_profile, s_states,
                                                SH_MODEL_MAX_FEATURES, &s_state_count),
                        TAG, "default states");
    sh_node_load_state();

    if (s_profile_blob_len == 0) {
        ESP_RETURN_ON_ERROR(sh_model_serialize_profile(s_profile, s_profile_blob,
                                                       sizeof(s_profile_blob),
                                                       &s_profile_blob_len),
                            TAG, "serialize profile");
    }

    ble_mesh_get_dev_uuid(s_dev_uuid);
    esp_ble_mesh_register_prov_callback(provisioning_cb);
    esp_ble_mesh_register_config_server_callback(config_server_cb);
    esp_ble_mesh_register_custom_model_callback(model_cb);

    esp_err_t err = esp_ble_mesh_init(&s_provision, &s_composition);
    if (err != ESP_OK) {
        return err;
    }

    if (esp_ble_mesh_node_is_provisioned()) {
        s_device_addr = esp_ble_mesh_get_primary_element_address();
        ESP_LOGI(TAG, "Smart-home node restored provisioned state addr=0x%04x",
                 s_device_addr);
        notify_provisioned(true, s_device_addr);
    } else {
        err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)
                                            (ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "Generic smart-home node initialized with profile 0x%04x (%s)",
             s_profile->profile_id, s_profile->display_name);
    return ESP_OK;
}

esp_err_t sh_node_set_feature(uint16_t feature_id, int32_t value)
{
    return apply_feature(feature_id, value, NULL);
}

esp_err_t sh_node_get_feature(uint16_t feature_id, sh_feature_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    sh_feature_state_t *found = sh_model_find_state(s_states, s_state_count, feature_id);
    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    *state = *found;
    return ESP_OK;
}

esp_err_t sh_node_set_all(const sh_feature_state_t *states, size_t state_count)
{
    if (!states) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < state_count; i++) {
        ESP_RETURN_ON_ERROR(sh_node_set_feature(states[i].feature_id, states[i].value),
                            TAG, "set all");
    }
    return ESP_OK;
}

uint16_t sh_node_get_device_addr(void)
{
    return s_device_addr;
}

uint16_t sh_node_get_client_addr(void)
{
    return s_client_addr;
}

bool sh_node_is_connected(void)
{
    return s_connected;
}

const sh_device_profile_t *sh_node_get_profile(void)
{
    return s_profile;
}

esp_err_t sh_node_save_state(void)
{
    if (!s_profile) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < s_state_count; i++) {
        ESP_RETURN_ON_ERROR(sh_store_save_feature(s_profile->profile_id,
                                                  s_states[i].feature_id,
                                                  s_states[i].value),
                            TAG, "save feature");
    }
    return ESP_OK;
}

esp_err_t sh_node_load_state(void)
{
    if (!s_profile) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < s_state_count; i++) {
        int32_t value = 0;
        if (sh_store_load_feature(s_profile->profile_id,
                                  s_states[i].feature_id,
                                  &value) == ESP_OK) {
            const sh_feature_def_t *feature = sh_model_find_feature(s_profile, s_states[i].feature_id);
            if (feature && sh_model_validate_value(feature, value) == ESP_OK) {
                s_states[i].value = value;
            }
        }
    }
    return ESP_OK;
}

esp_err_t sh_node_clear_state(void)
{
    if (!s_profile) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint8_t i = 0; i < s_profile->feature_count; i++) {
        sh_store_save_feature(s_profile->profile_id,
                              s_profile->features[i].feature_id,
                              s_profile->features[i].default_value);
    }
    return sh_node_load_state();
}

#else

esp_err_t sh_node_init(const sh_device_profile_t *profile, const sh_node_callbacks_t *callbacks) { (void)profile; (void)callbacks; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_node_set_feature(uint16_t feature_id, int32_t value) { (void)feature_id; (void)value; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_node_get_feature(uint16_t feature_id, sh_feature_state_t *state) { (void)feature_id; (void)state; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_node_set_all(const sh_feature_state_t *states, size_t state_count) { (void)states; (void)state_count; return ESP_ERR_NOT_SUPPORTED; }
uint16_t sh_node_get_device_addr(void) { return 0; }
uint16_t sh_node_get_client_addr(void) { return 0; }
bool sh_node_is_connected(void) { return false; }
const sh_device_profile_t *sh_node_get_profile(void) { return NULL; }
esp_err_t sh_node_save_state(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_node_load_state(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_node_clear_state(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif
