#include "smarthome_client.h"
#include "sdkconfig.h"

#if CONFIG_BLE_MESH_PROVISIONER

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_idf_version.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "smarthome_protocol.h"
#include "smarthome_profiles.h"
#include "smarthome_store.h"

#define TAG "SH_CLIENT"
#define SH_PROV_OWN_ADDR       0x0001
#define SH_PROV_START_ADDR     0x0005
#define SH_MSG_SEND_TTL        7
#define SH_MSG_TIMEOUT_MS      1000
#define SH_APP_KEY_IDX         0x0000
#define SH_APP_KEY_OCTET       0x12
#define SH_MAX_TIMEOUTS        3
#define SH_PROFILE_BLOB_MAX    512

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
    uint8_t uuid[ESP_BLE_MESH_OCTET16_LEN];
    uint8_t consecutive_timeouts;
    uint8_t set_cmd_timeout_count;
    sh_dynamic_profile_t profile_storage;
    uint8_t profile_blob[SH_PROFILE_BLOB_MAX];
    uint16_t profile_blob_len;
    uint16_t profile_expected_len;
    uint16_t profile_crc;
    uint8_t profile_chunks_seen;
    uint8_t profile_chunk_count;
} sh_client_device_slot_t;

static struct {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t app_key[ESP_BLE_MESH_OCTET16_LEN];
} s_key = {
    .net_idx = ESP_BLE_MESH_KEY_PRIMARY,
    .app_idx = SH_APP_KEY_IDX,
};

static uint8_t s_dev_uuid[ESP_BLE_MESH_OCTET16_LEN];
static sh_client_device_slot_t s_devices[SH_CLIENT_MAX_DEVICES];
static uint8_t s_device_count;
static sh_client_callbacks_t s_callbacks;
static bool s_initialized;
static bool s_key_refresh_in_progress;
static uint8_t s_profile_set_blob[SH_PROFILE_BLOB_MAX];

static esp_ble_mesh_cfg_srv_t s_config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = SH_MSG_SEND_TTL,
};

static esp_ble_mesh_client_t s_config_client;
static esp_ble_mesh_client_t s_vendor_client;

static esp_ble_mesh_model_op_t s_client_ops[] = {
    ESP_BLE_MESH_MODEL_OP(SH_OP_PROFILE_STATUS, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_FEATURE_STATUS, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_NODE_EVENT, 0),
    ESP_BLE_MESH_MODEL_OP(SH_OP_DISCONNECT_ACK, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_client_op_pair_t s_client_op_pair[] = {
    {SH_OP_PROFILE_GET, SH_OP_PROFILE_STATUS},
    {SH_OP_PROFILE_SET, SH_OP_PROFILE_STATUS},
    {SH_OP_FEATURE_GET, SH_OP_FEATURE_STATUS},
    {SH_OP_FEATURE_SET, SH_OP_FEATURE_STATUS},
    {SH_OP_DISCONNECT_NOTIFY, SH_OP_DISCONNECT_ACK},
};

static esp_ble_mesh_model_t s_root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&s_config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&s_config_client),
};

static esp_ble_mesh_model_t s_vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(SH_COMPANY_ID, SH_MODEL_ID_CLIENT,
                              s_client_ops, NULL, &s_vendor_client),
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
    .prov_uuid = s_dev_uuid,
    .prov_unicast_addr = SH_PROV_OWN_ADDR,
    .prov_start_address = SH_PROV_START_ADDR,
};

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

static sh_client_device_slot_t *find_device(uint16_t addr)
{
    int idx = find_device_index(addr);
    return idx >= 0 ? &s_devices[idx] : NULL;
}

static sh_client_device_slot_t *add_device(uint16_t addr, const uint8_t *uuid)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (slot) {
        if (uuid) {
            memcpy(slot->uuid, uuid, sizeof(slot->uuid));
        }
        return slot;
    }
    if (s_device_count >= SH_CLIENT_MAX_DEVICES) {
        ESP_LOGW(TAG, "Device table full, cannot add 0x%04x", addr);
        return NULL;
    }

    slot = &s_devices[s_device_count++];
    memset(slot, 0, sizeof(*slot));
    slot->public_info.addr = addr;
    snprintf(slot->public_info.device_name, sizeof(slot->public_info.device_name), "NODE_%04X", addr);
    if (uuid) {
        memcpy(slot->uuid, uuid, sizeof(slot->uuid));
    }
    return slot;
}

static void set_common(esp_ble_mesh_client_common_param_t *common,
                       uint16_t addr,
                       esp_ble_mesh_model_t *model,
                       uint32_t opcode)
{
    memset(common, 0, sizeof(*common));
    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = s_key.net_idx;
    common->ctx.app_idx = s_key.app_idx;
    common->ctx.addr = addr;
    common->ctx.send_ttl = SH_MSG_SEND_TTL;
    common->msg_timeout = SH_MSG_TIMEOUT_MS;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    common->msg_role = ROLE_PROVISIONER;
#endif
}

static void set_config_common(esp_ble_mesh_client_common_param_t *common,
                              esp_ble_mesh_node_t *node,
                              uint32_t opcode)
{
    set_common(common, node->unicast_addr, s_config_client.model, opcode);
}

static esp_err_t send_vendor(uint16_t addr, uint32_t opcode,
                             const uint8_t *data, uint16_t len, bool need_rsp)
{
    if (!s_vendor_client.model) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx = s_key.net_idx,
        .app_idx = s_key.app_idx,
        .addr = addr,
        .send_ttl = SH_MSG_SEND_TTL,
    };

    return esp_ble_mesh_client_model_send_msg(s_vendor_client.model, &ctx, opcode,
                                              len, (uint8_t *)data,
                                              need_rsp ? SH_MSG_TIMEOUT_MS : 0,
                                              need_rsp,
                                              ROLE_PROVISIONER);
}

static esp_err_t request_profile_for_device(uint16_t addr)
{
    uint8_t payload[2];
    size_t written = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &written),
                        TAG, "write profile get header");
    esp_err_t err = send_vendor(addr, SH_OP_PROFILE_GET, payload, written, true);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Requested profile from 0x%04x", addr);
    } else {
        ESP_LOGW(TAG, "Failed to request profile from 0x%04x: %s",
                 addr, esp_err_to_name(err));
    }
    return err;
}

static void mark_device_configured(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return;
    }

    slot->public_info.is_configured = true;
    slot->public_info.is_online = true;
    slot->public_info.last_update_time = now_ms();
    slot->consecutive_timeouts = 0;

    request_profile_for_device(addr);

    if (s_callbacks.online_cb) {
        s_callbacks.online_cb(addr, true);
    }
    if (s_callbacks.provisioned_cb) {
        s_callbacks.provisioned_cb(addr);
    }
}

static void handle_profile_status(uint16_t addr, const uint8_t *data, uint16_t len)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot || !data || len < sizeof(sh_profile_chunk_header_t)) {
        return;
    }
    slot->consecutive_timeouts = 0;
    slot->public_info.is_online = true;
    slot->public_info.last_update_time = now_ms();

    sh_profile_chunk_header_t header;
    memcpy(&header, data, sizeof(header));
    if (header.version != SH_PROTOCOL_VERSION ||
        header.chunk_len > SH_PROFILE_CHUNK_PAYLOAD ||
        sizeof(header) + header.chunk_len > len ||
        header.total_len > SH_PROFILE_BLOB_MAX ||
        header.chunk_index >= header.chunk_count) {
        ESP_LOGW(TAG, "Invalid profile chunk from 0x%04x", addr);
        return;
    }

    if (header.chunk_index == 0) {
        slot->profile_blob_len = 0;
        slot->profile_chunks_seen = 0;
        slot->profile_chunk_count = header.chunk_count;
        slot->profile_expected_len = header.total_len;
        slot->profile_crc = header.crc16;
        memset(slot->profile_blob, 0, sizeof(slot->profile_blob));
    }

    uint16_t offset = header.chunk_index * SH_PROFILE_CHUNK_PAYLOAD;
    if (offset + header.chunk_len > sizeof(slot->profile_blob)) {
        return;
    }
    memcpy(&slot->profile_blob[offset], data + sizeof(header), header.chunk_len);
    if (offset + header.chunk_len > slot->profile_blob_len) {
        slot->profile_blob_len = offset + header.chunk_len;
    }
    slot->profile_chunks_seen++;

    if (slot->profile_chunks_seen < slot->profile_chunk_count) {
        return;
    }
    if (slot->profile_blob_len != slot->profile_expected_len ||
        sh_protocol_crc16(slot->profile_blob, slot->profile_blob_len) != slot->profile_crc) {
        ESP_LOGW(TAG, "Profile CRC/length mismatch for 0x%04x, retrying", addr);
        request_profile_for_device(addr);
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
    sh_store_save_profile_blob(profile->profile_id, slot->profile_blob, slot->profile_blob_len);
    sh_model_default_states(profile, slot->public_info.states,
                            SH_MODEL_MAX_FEATURES, &slot->public_info.state_count);
    strncpy(slot->public_info.device_name, profile->display_name,
            sizeof(slot->public_info.device_name) - 1);
    slot->public_info.device_name[sizeof(slot->public_info.device_name) - 1] = '\0';

    ESP_LOGI(TAG, "Loaded profile 0x%04x (%s) from 0x%04x",
             profile->profile_id, profile->display_name, addr);

    if (s_callbacks.profile_cb) {
        s_callbacks.profile_cb(addr, profile);
    }
    sh_client_refresh_device(addr);
    sh_client_add_device_to_group(addr);
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
        ESP_LOGW(TAG, "Invalid feature status from 0x%04x", addr);
        return;
    }

    sh_client_device_slot_t *slot = add_device(addr, NULL);
    if (!slot) {
        return;
    }

    slot->public_info.is_online = true;
    slot->public_info.last_update_time = now_ms();
    slot->consecutive_timeouts = 0;
    slot->set_cmd_timeout_count = 0;
    slot->public_info.is_set_cmd_unresponsive = false;

    sh_feature_state_t *state = sh_model_find_state(slot->public_info.states,
                                                    slot->public_info.state_count,
                                                    tlv.feature_id);
    if (!state && slot->public_info.state_count < SH_MODEL_MAX_FEATURES) {
        state = &slot->public_info.states[slot->public_info.state_count++];
        state->feature_id = tlv.feature_id;
        state->type = tlv.type;
    }
    if (state) {
        state->type = tlv.type;
        state->value = value;
    }

    if (s_callbacks.feature_status_cb) {
        s_callbacks.feature_status_cb(addr, tlv.feature_id, tlv.type, value);
    }
}

static void handle_operation(uint32_t opcode, const uint8_t *data, uint16_t len, uint16_t addr)
{
    sh_client_device_slot_t *slot = add_device(addr, NULL);
    if (slot) {
        slot->public_info.is_online = true;
        slot->public_info.last_update_time = now_ms();
    }

    switch (opcode) {
    case SH_OP_PROFILE_STATUS:
        handle_profile_status(addr, data, len);
        break;
    case SH_OP_FEATURE_STATUS:
        handle_feature_status(addr, data, len);
        break;
    case SH_OP_NODE_EVENT:
        ESP_LOGI(TAG, "Node event from 0x%04x", addr);
        break;
    case SH_OP_DISCONNECT_ACK:
        if (slot) {
            slot->public_info.is_manually_disconnected = true;
            slot->public_info.is_online = false;
        }
        if (s_callbacks.online_cb) {
            s_callbacks.online_cb(addr, false);
        }
        break;
    default:
        ESP_LOGW(TAG, "Unknown opcode 0x%06" PRIx32 " from 0x%04x", opcode, addr);
        break;
    }
}

static void vendor_model_cb(esp_ble_mesh_model_cb_event_t event,
                            esp_ble_mesh_model_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        ESP_LOGI(TAG, "Recv operation opcode=0x%06" PRIx32 " from 0x%04x",
                 param->model_operation.opcode,
                 param->model_operation.ctx ? param->model_operation.ctx->addr : 0);
        handle_operation(param->model_operation.opcode,
                         param->model_operation.msg,
                         param->model_operation.length,
                         param->model_operation.ctx->addr);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        ESP_LOGI(TAG, "Recv publish opcode=0x%06" PRIx32 " from 0x%04x",
                 param->client_recv_publish_msg.opcode,
                 param->client_recv_publish_msg.ctx ? param->client_recv_publish_msg.ctx->addr : 0);
        handle_operation(param->client_recv_publish_msg.opcode,
                         param->client_recv_publish_msg.msg,
                         param->client_recv_publish_msg.length,
                         param->client_recv_publish_msg.ctx->addr);
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT: {
        uint16_t addr = param->client_send_timeout.ctx->addr;
        sh_client_device_slot_t *slot = find_device(addr);
        if (slot) {
            slot->consecutive_timeouts++;
            if (param->client_send_timeout.opcode == SH_OP_PROFILE_GET &&
                !slot->public_info.profile_loaded &&
                slot->consecutive_timeouts < SH_MAX_TIMEOUTS) {
                request_profile_for_device(addr);
            }
            if (slot->consecutive_timeouts >= SH_MAX_TIMEOUTS) {
                slot->public_info.is_online = false;
                if (s_callbacks.online_cb) {
                    s_callbacks.online_cb(addr, false);
                }
            }
        }
        ESP_LOGW(TAG, "Send timeout opcode 0x%06" PRIx32 " addr 0x%04x",
                 param->client_send_timeout.opcode, addr);
        break;
    }
    default:
        break;
    }
}

static void recv_unprov_adv(uint8_t uuid[ESP_BLE_MESH_OCTET16_LEN],
                            uint8_t addr[BD_ADDR_LEN],
                            esp_ble_mesh_addr_type_t addr_type,
                            uint16_t oob_info,
                            esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    add_dev.addr_type = addr_type;
    memcpy(add_dev.uuid, uuid, ESP_BLE_MESH_OCTET16_LEN);
    add_dev.oob_info = oob_info;
    add_dev.bearer = bearer;

    esp_err_t err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev,
        ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add unprovisioned device: %s", esp_err_to_name(err));
    }
}

static esp_err_t provisioned(uint16_t node_index,
                             const esp_ble_mesh_octet16_t uuid,
                             uint16_t primary_addr,
                             uint8_t element_num,
                             uint16_t net_idx)
{
    ESP_LOGI(TAG, "Provisioned node idx=%u addr=0x%04x elems=%u net=0x%03x",
             node_index, primary_addr, element_num, net_idx);
    add_device(primary_addr, uuid);

    char name[12];
    snprintf(name, sizeof(name), "NODE-%02u", node_index);
    esp_ble_mesh_provisioner_set_node_name(node_index, name);

    esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_with_addr(primary_addr);
    if (!node) {
        return ESP_FAIL;
    }

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_get_state_t get = {0};
    set_config_common(&common, node, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get.comp_data_get.page = 0;
    return esp_ble_mesh_config_client_get_state(&common, &get);
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                            esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        recv_unprov_adv(param->provisioner_recv_unprov_adv_pkt.dev_uuid,
                        param->provisioner_recv_unprov_adv_pkt.addr,
                        param->provisioner_recv_unprov_adv_pkt.addr_type,
                        param->provisioner_recv_unprov_adv_pkt.oob_info,
                        param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        provisioned(param->provisioner_prov_complete.node_idx,
                    param->provisioner_prov_complete.device_uuid,
                    param->provisioner_prov_complete.unicast_addr,
                    param->provisioner_prov_complete.element_num,
                    param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        if (param->provisioner_add_app_key_comp.err_code == ESP_OK) {
            s_key.app_idx = param->provisioner_add_app_key_comp.app_idx;
            esp_err_t err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(
                SH_PROV_OWN_ADDR, s_key.app_idx, SH_MODEL_ID_CLIENT, SH_COMPANY_ID);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Local model bind failed: %s", esp_err_to_name(err));
                break;
            }
            err = esp_ble_mesh_model_subscribe_group_addr(SH_PROV_OWN_ADDR, SH_COMPANY_ID,
                                                          SH_MODEL_ID_CLIENT,
                                                          SH_GROUP_ADDR_DEFAULT);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Local controller group subscribe failed: %s", esp_err_to_name(err));
            }
        }
        break;
    default:
        break;
    }
}

static void config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                             esp_ble_mesh_cfg_client_cb_param_t *param)
{
    if (param->error_code) {
        ESP_LOGW(TAG, "Config op 0x%04" PRIx32 " failed err=%d",
                 param->params->opcode, param->error_code);
        return;
    }

    esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_with_addr(param->params->ctx.addr);
    if (!node) {
        return;
    }

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_set_state_t set = {0};
    esp_err_t err;

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
            err = esp_ble_mesh_provisioner_store_node_comp_data(
                param->params->ctx.addr,
                param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Store comp data failed: %s", esp_err_to_name(err));
            }
            set_config_common(&common, node, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set.app_key_add.net_idx = s_key.net_idx;
            set.app_key_add.app_idx = s_key.app_idx;
            memcpy(set.app_key_add.app_key, s_key.app_key, ESP_BLE_MESH_OCTET16_LEN);
            esp_ble_mesh_config_client_set_state(&common, &set);
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            set_config_common(&common, node, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set.model_app_bind.element_addr = node->unicast_addr;
            set.model_app_bind.model_app_idx = s_key.app_idx;
            set.model_app_bind.model_id = SH_MODEL_ID_NODE;
            set.model_app_bind.company_id = SH_COMPANY_ID;
            esp_ble_mesh_config_client_set_state(&common, &set);
        } else if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            mark_device_configured(node->unicast_addr);
        }
        break;
    default:
        break;
    }
}

esp_err_t sh_client_init(void)
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

    memset(s_key.app_key, SH_APP_KEY_OCTET, sizeof(s_key.app_key));
    s_dev_uuid[0] = 0xDD;
    s_dev_uuid[1] = 0xDD;

    s_vendor_client.op_pair_size = ARRAY_SIZE(s_client_op_pair);
    s_vendor_client.op_pair = s_client_op_pair;

    esp_ble_mesh_register_prov_callback(provisioning_cb);
    esp_ble_mesh_register_config_client_callback(config_client_cb);
    esp_ble_mesh_register_custom_model_callback(vendor_model_cb);

    err = esp_ble_mesh_init(&s_provision, &s_composition);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_client_model_init(&s_vendor_models[0]);
    if (err != ESP_OK) {
        return err;
    }
    s_vendor_client.model = &s_vendor_models[0];

    err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)
                                               (ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_mesh_provisioner_add_local_app_key(s_key.app_key, s_key.net_idx, s_key.app_idx);
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Generic smart-home mesh client initialized");
    return ESP_OK;
}

void sh_client_register_callbacks(const sh_client_callbacks_t *callbacks)
{
    if (callbacks) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }
}

uint8_t sh_client_get_device_count(void)
{
    return s_device_count;
}

uint8_t sh_client_get_online_device_count(void)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < s_device_count; i++) {
        if (s_devices[i].public_info.is_online && s_devices[i].public_info.is_configured) {
            count++;
        }
    }
    return count;
}

uint8_t sh_client_get_device_list(sh_client_device_t *devices, uint8_t max_devices)
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

esp_err_t sh_client_get_device_by_index(uint8_t index, sh_client_device_t *device)
{
    if (!device || index >= s_device_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *device = s_devices[index].public_info;
    return ESP_OK;
}

esp_err_t sh_client_get_device_by_addr(uint16_t addr, sh_client_device_t *device)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot || !device) {
        return ESP_ERR_NOT_FOUND;
    }
    *device = slot->public_info;
    return ESP_OK;
}

uint16_t sh_client_get_device_addr_by_index(uint8_t index)
{
    return index < s_device_count ? s_devices[index].public_info.addr : ESP_BLE_MESH_ADDR_UNASSIGNED;
}

bool sh_client_is_device_online(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    return slot && slot->public_info.is_online && slot->public_info.is_configured;
}

bool sh_client_is_device_set_cmd_responsive(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    return slot && !slot->public_info.is_set_cmd_unresponsive;
}

esp_err_t sh_client_get_profile(uint16_t addr)
{
    return request_profile_for_device(addr);
}

esp_err_t sh_client_set_profile(uint16_t addr, const sh_device_profile_t *profile)
{
    if (!profile) {
        return ESP_ERR_INVALID_ARG;
    }

    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t blob_len = 0;
    ESP_RETURN_ON_ERROR(sh_model_serialize_profile(profile, s_profile_set_blob,
                                                   sizeof(s_profile_set_blob), &blob_len),
                        TAG, "serialize profile set");

    uint16_t crc = sh_protocol_crc16(s_profile_set_blob, blob_len);
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
        memcpy(payload + sizeof(header), &s_profile_set_blob[offset], chunk_len);

        esp_err_t err = send_vendor(addr, SH_OP_PROFILE_SET, payload,
                                    sizeof(header) + chunk_len,
                                    i + 1 == chunk_count);
        if (err != ESP_OK) {
            return err;
        }
    }

    slot->public_info.profile = profile;
    slot->public_info.profile_loaded = true;
    sh_model_default_states(profile, slot->public_info.states,
                            SH_MODEL_MAX_FEATURES, &slot->public_info.state_count);
    strncpy(slot->public_info.device_name, profile->display_name,
            sizeof(slot->public_info.device_name) - 1);
    slot->public_info.device_name[sizeof(slot->public_info.device_name) - 1] = '\0';
    sh_store_save_profile_blob(profile->profile_id, s_profile_set_blob, blob_len);

    if (s_callbacks.profile_cb) {
        s_callbacks.profile_cb(addr, profile);
    }
    ESP_LOGI(TAG, "Sent profile 0x%04x (%s) to 0x%04x",
             profile->profile_id, profile->display_name, addr);
    return ESP_OK;
}

esp_err_t sh_client_get_feature(uint16_t addr, uint16_t feature_id)
{
    uint8_t payload[4];
    size_t written = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &written),
                        TAG, "feature get header");
    payload[written++] = (uint8_t)(feature_id & 0xFF);
    payload[written++] = (uint8_t)((feature_id >> 8) & 0xFF);
    return send_vendor(addr, SH_OP_FEATURE_GET, payload, written, true);
}

esp_err_t sh_client_set_feature(uint16_t addr, uint16_t feature_id, int32_t value)
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

esp_err_t sh_client_group_set_feature(uint16_t feature_id, int32_t value)
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

esp_err_t sh_client_refresh_device(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot || !slot->public_info.profile) {
        return ESP_ERR_INVALID_STATE;
    }
    for (uint8_t i = 0; i < slot->public_info.profile->feature_count; i++) {
        sh_client_get_feature(addr, slot->public_info.profile->features[i].feature_id);
    }
    return ESP_OK;
}

esp_err_t sh_client_refresh_all(void)
{
    for (uint8_t i = 0; i < s_device_count; i++) {
        if (s_devices[i].public_info.is_online) {
            sh_client_refresh_device(s_devices[i].public_info.addr);
        }
    }
    return ESP_OK;
}

esp_err_t sh_client_add_device_to_group(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_with_addr(addr);
    if (!slot || !node) {
        return ESP_ERR_NOT_FOUND;
    }
    if (slot->public_info.is_in_group) {
        return ESP_OK;
    }

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_set_state_t set = {0};
    set_config_common(&common, node, ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD);
    set.model_sub_add.element_addr = addr;
    set.model_sub_add.sub_addr = SH_GROUP_ADDR_DEFAULT;
    set.model_sub_add.model_id = SH_MODEL_ID_NODE;
    set.model_sub_add.company_id = SH_COMPANY_ID;
    esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set);
    if (err == ESP_OK) {
        slot->public_info.is_in_group = true;
    }
    return err;
}

esp_err_t sh_client_remove_device_from_group(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    esp_ble_mesh_node_t *node = esp_ble_mesh_provisioner_get_node_with_addr(addr);
    if (!slot || !node) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_set_state_t set = {0};
    set_config_common(&common, node, ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE);
    set.model_sub_delete.element_addr = addr;
    set.model_sub_delete.sub_addr = SH_GROUP_ADDR_DEFAULT;
    set.model_sub_delete.model_id = SH_MODEL_ID_NODE;
    set.model_sub_delete.company_id = SH_COMPANY_ID;
    esp_err_t err = esp_ble_mesh_config_client_set_state(&common, &set);
    if (err == ESP_OK) {
        slot->public_info.is_in_group = false;
    }
    return err;
}

uint16_t sh_client_get_group_address(void)
{
    return SH_GROUP_ADDR_DEFAULT;
}

bool sh_client_is_device_in_group(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    return slot && slot->public_info.is_in_group;
}

esp_err_t sh_client_disconnect_device(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }
    slot->public_info.is_manually_disconnected = true;
    slot->public_info.is_online = false;
    if (s_callbacks.online_cb) {
        s_callbacks.online_cb(addr, false);
    }
    return ESP_OK;
}

esp_err_t sh_client_reconnect_device(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }
    slot->public_info.is_manually_disconnected = false;
    slot->public_info.is_filtered = false;
    return request_profile_for_device(addr);
}

esp_err_t sh_client_add_device_to_filter(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }
    slot->public_info.is_filtered = true;
    return ESP_OK;
}

esp_err_t sh_client_remove_device_from_filter(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }
    slot->public_info.is_filtered = false;
    return ESP_OK;
}

bool sh_client_is_device_filtered(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    return slot && slot->public_info.is_filtered;
}

bool sh_client_is_device_blacklisted(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    return slot && slot->public_info.is_blacklisted;
}

esp_err_t sh_client_toggle_device_connection(uint16_t addr)
{
    sh_client_device_slot_t *slot = find_device(addr);
    if (!slot) {
        return ESP_ERR_NOT_FOUND;
    }
    if (slot->public_info.is_manually_disconnected || slot->public_info.is_filtered) {
        sh_client_remove_device_from_filter(addr);
        return sh_client_reconnect_device(addr);
    }
    sh_client_send_disconnect_notify(addr);
    sh_client_disconnect_device(addr);
    return sh_client_add_device_to_filter(addr);
}

esp_err_t sh_client_send_disconnect_notify(uint16_t addr)
{
    uint8_t payload[2];
    size_t written = 0;
    ESP_RETURN_ON_ERROR(sh_protocol_write_header(payload, sizeof(payload),
                                                 sh_protocol_next_tid(), &written),
                        TAG, "disconnect header");
    return send_vendor(addr, SH_OP_DISCONNECT_NOTIFY, payload, written, true);
}

esp_err_t sh_client_remove_device_completely(uint16_t addr)
{
    int idx = find_device_index(addr);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    sh_client_send_disconnect_notify(addr);
    s_devices[idx].public_info.is_blacklisted = true;
    for (uint8_t i = idx; i + 1 < s_device_count; i++) {
        s_devices[i] = s_devices[i + 1];
    }
    s_device_count--;
    return ESP_OK;
}

esp_err_t sh_client_perform_key_refresh(void)
{
    uint8_t new_key[ESP_BLE_MESH_OCTET16_LEN];
    esp_fill_random(new_key, sizeof(new_key));
    s_key_refresh_in_progress = true;
    esp_err_t err = esp_ble_mesh_provisioner_update_local_net_key(new_key, s_key.net_idx);
    s_key_refresh_in_progress = false;
    return err;
}

bool sh_client_is_key_refresh_in_progress(void)
{
    return s_key_refresh_in_progress;
}

uint8_t sh_client_get_queue_size(void)
{
    return 0;
}

sh_send_state_t sh_client_get_send_state(void)
{
    return SH_SEND_STATE_IDLE;
}

void sh_client_clear_queue(void)
{
}

#else

esp_err_t sh_client_init(void) { return ESP_ERR_NOT_SUPPORTED; }
void sh_client_register_callbacks(const sh_client_callbacks_t *callbacks) { (void)callbacks; }
uint8_t sh_client_get_device_count(void) { return 0; }
uint8_t sh_client_get_online_device_count(void) { return 0; }
uint8_t sh_client_get_device_list(sh_client_device_t *devices, uint8_t max_devices) { (void)devices; (void)max_devices; return 0; }
esp_err_t sh_client_get_device_by_index(uint8_t index, sh_client_device_t *device) { (void)index; (void)device; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_get_device_by_addr(uint16_t addr, sh_client_device_t *device) { (void)addr; (void)device; return ESP_ERR_NOT_SUPPORTED; }
uint16_t sh_client_get_device_addr_by_index(uint8_t index) { (void)index; return ESP_BLE_MESH_ADDR_UNASSIGNED; }
bool sh_client_is_device_online(uint16_t addr) { (void)addr; return false; }
bool sh_client_is_device_set_cmd_responsive(uint16_t addr) { (void)addr; return false; }
esp_err_t sh_client_get_profile(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_set_profile(uint16_t addr, const sh_device_profile_t *profile) { (void)addr; (void)profile; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_get_feature(uint16_t addr, uint16_t feature_id) { (void)addr; (void)feature_id; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_set_feature(uint16_t addr, uint16_t feature_id, int32_t value) { (void)addr; (void)feature_id; (void)value; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_group_set_feature(uint16_t feature_id, int32_t value) { (void)feature_id; (void)value; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_refresh_device(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_refresh_all(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_add_device_to_group(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_remove_device_from_group(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
uint16_t sh_client_get_group_address(void) { return SH_GROUP_ADDR_DEFAULT; }
bool sh_client_is_device_in_group(uint16_t addr) { (void)addr; return false; }
esp_err_t sh_client_disconnect_device(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_reconnect_device(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_add_device_to_filter(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_remove_device_from_filter(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
bool sh_client_is_device_filtered(uint16_t addr) { (void)addr; return false; }
bool sh_client_is_device_blacklisted(uint16_t addr) { (void)addr; return false; }
esp_err_t sh_client_toggle_device_connection(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_send_disconnect_notify(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_remove_device_completely(uint16_t addr) { (void)addr; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_client_perform_key_refresh(void) { return ESP_ERR_NOT_SUPPORTED; }
bool sh_client_is_key_refresh_in_progress(void) { return false; }
uint8_t sh_client_get_queue_size(void) { return 0; }
sh_send_state_t sh_client_get_send_state(void) { return SH_SEND_STATE_IDLE; }
void sh_client_clear_queue(void) {}

#endif
