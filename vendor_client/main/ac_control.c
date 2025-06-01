/* ac_control.c - Air Conditioner Bluetooth Mesh Client Control Implementation */

#include "ac_control.h"
#include "esp_log.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "board.h"
#include <string.h>
#include <inttypes.h>

#include "mesh_common.h"
#include "ble_mesh_example_nvs.h"

#define TAG "AC_CLIENT"

// BLE related definitions from main.c
#define PROV_OWN_ADDR       0x0001
#define MSG_SEND_TTL        7
#define MSG_TIMEOUT         0
#define MSG_ROLE            ROLE_PROVISIONER
#define COMP_DATA_PAGE_0    0x00
#define APP_KEY_IDX         0x0000
#define APP_KEY_OCTET       0x12

#define MAX_CONSECUTIVE_TIMEOUTS 3 // Define for timeout logic

#define COMP_DATA_1_OCTET(msg, offset)      (msg[offset])
#define COMP_DATA_2_OCTET(msg, offset)      (msg[offset + 1] << 8 | msg[offset])

// extern struct esp_ble_mesh_key prov_key; // Now internal
static struct esp_ble_mesh_key {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t  app_key[ESP_BLE_MESH_OCTET16_LEN];
} prov_key;

/* Global BLE variables from main.c */
static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN];
// static uint16_t client_primary_addr;

/* Structure to hold information about each managed AC server */
typedef struct {
    uint16_t addr;                          /* Server unicast address */
    bool is_online;                         /* Online status */
    uint8_t consecutive_timeouts;           /* Count of consecutive send timeouts */
} ac_server_info_t;

static struct example_info_store {
    ac_server_info_t servers[MAX_AC_SERVERS]; /* Array of AC server information */
    uint8_t num_servers;                      /* Number of currently stored server addresses */
    uint16_t vnd_tid;                         /* TID contained in the vendor message */
} store = {
    .num_servers = 0,
    .vnd_tid = 0,
};

static nvs_handle_t NVS_HANDLE;
static const char * NVS_KEY = "ac_client_nvs";

/* AC状态回调函数 */
// static ac_status_callback_t ac_status_cb = NULL;

/* 定义模型操作项 */
static esp_ble_mesh_model_op_t ac_client_op[] = {
    /* 状态消息响应处理器 */
    ESP_BLE_MESH_MODEL_OP(AC_OP_POWER_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_TEMPERATURE_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_MODE_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_FAN_SPEED_STATUS, 1),
    /* 心跳包处理器 */
    ESP_BLE_MESH_MODEL_OP(AC_OP_HEARTBEAT, 0),
    ESP_BLE_MESH_MODEL_OP(AC_OP_HEARTBEAT_ACK, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

/* Vendor客户端模型ID */
static esp_ble_mesh_client_op_pair_t ac_client_op_pair[] = {
    {AC_OP_SET_POWER, AC_OP_POWER_STATUS},
    {AC_OP_GET_POWER, AC_OP_POWER_STATUS},
    {AC_OP_SET_TEMPERATURE, AC_OP_TEMPERATURE_STATUS},
    {AC_OP_GET_TEMPERATURE, AC_OP_TEMPERATURE_STATUS},
    {AC_OP_SET_MODE, AC_OP_MODE_STATUS},
    {AC_OP_GET_MODE, AC_OP_MODE_STATUS},
    {AC_OP_SET_FAN_SPEED, AC_OP_FAN_SPEED_STATUS},
    {AC_OP_GET_FAN_SPEED, AC_OP_FAN_SPEED_STATUS},
    /* 心跳包操作对 - 心跳包不需要状态响应，所以使用相同的操作码 */
    {AC_OP_HEARTBEAT_ACK, AC_OP_HEARTBEAT_ACK},
};

static esp_ble_mesh_client_t ac_client = {
    .op_pair_size = ARRAY_SIZE(ac_client_op_pair),
    .op_pair = ac_client_op_pair,
    .model = NULL,
};

// BLE Configuration structures from main.c
static esp_ble_mesh_cfg_srv_t config_server_cfg = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
};

static esp_ble_mesh_client_t config_client;

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server_cfg),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(MY_COMPANY_ID, MY_MODEL_ID_AC_CLIENT,
    ac_client_op, NULL, &ac_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = MY_COMPANY_ID,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .prov_uuid          = dev_uuid,
    .prov_unicast_addr  = PROV_OWN_ADDR,
    .prov_start_address = 0x0005,
};

/* 模型发送消息的通用参数 */
static void set_msg_common(esp_ble_mesh_client_common_param_t *common, 
    uint16_t server_addr, uint32_t opcode)
{
    common->opcode = opcode;
    common->model = ac_client.model;
    common->ctx.net_idx = prov_key.net_idx;
    common->ctx.app_idx = prov_key.app_idx;
    common->ctx.addr = server_addr;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->msg_timeout = 2000;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    common->msg_role = MSG_ROLE;
#endif
}

/* Helper function to find server index by address */
static int _find_server_index(uint16_t addr) {
    for (uint8_t i = 0; i < store.num_servers; i++) {
        if (store.servers[i].addr == addr) {
            return i;
        }
    }
    return -1; // Not found
}

/* 处理状态消息 */
static void handle_status_message(uint32_t opcode, const uint8_t *data, uint16_t len, uint16_t src_addr)
{
    uint8_t type = 0;
    uint8_t value = 0;

    // Mark server as online and reset timeout count upon receiving any status message
    int server_idx = _find_server_index(src_addr);
    if (server_idx != -1) {
        if (!store.servers[server_idx].is_online) {
            ESP_LOGI(TAG, "Server 0x%04x is back online.", src_addr);
        }
        store.servers[server_idx].is_online = true;
        store.servers[server_idx].consecutive_timeouts = 0;
    }

    switch (opcode) {
        case AC_OP_HEARTBEAT:
            ESP_LOGD(TAG, "Received heartbeat from server 0x%04x", src_addr);
            /* 发送心跳包ACK响应 */
            esp_ble_mesh_msg_ctx_t ctx = {0};
            ctx.net_idx = prov_key.net_idx;
            ctx.app_idx = prov_key.app_idx;
            ctx.addr = src_addr;
            ctx.send_ttl = MSG_SEND_TTL;
            
            /* 使用服务器模型发送ACK响应 */
            esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0], &ctx, AC_OP_HEARTBEAT_ACK,
                                                             0, NULL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send heartbeat ACK to 0x%04x (err %d)", src_addr, err);
            } else {
                ESP_LOGD(TAG, "Heartbeat ACK sent to server 0x%04x", src_addr);
            }
            return; /* 心跳包不需要进一步处理 */
        case AC_OP_POWER_STATUS:
            if (data != NULL && len >= 1) {
                value = data[0];
                type = AC_STATUS_POWER;
                ESP_LOGI(TAG, "Received power status: %d", value);
            }
            break;
        case AC_OP_TEMPERATURE_STATUS:
            if (data != NULL && len >= 1) {
                value = data[0];
                type = AC_STATUS_TEMPERATURE;
                ESP_LOGI(TAG, "Received temperature status: %d", value);
            }
            break;
        case AC_OP_MODE_STATUS:
            if (data != NULL && len >= 1) {
                value = data[0];
                type = AC_STATUS_MODE;
                ESP_LOGI(TAG, "Received mode status: %d", value);
            }
            break;
        case AC_OP_FAN_SPEED_STATUS:
            if (data != NULL && len >= 1) {
                value = data[0];
                type = AC_STATUS_FAN_SPEED;
                ESP_LOGI(TAG, "Received fan speed status: %d", value);
            }
            break;
        default:
            return;
    }
    
    // if (ac_status_cb != NULL) {
    //     ac_status_cb(type, value);
    // }
}

/* 自定义模型回调 */
static void ac_client_model_cb(esp_ble_mesh_model_cb_event_t event,
                              esp_ble_mesh_model_cb_param_t *param)
{
    ESP_LOGW(TAG, "Into ac_client_model_cb");
    switch (event) {
        case ESP_BLE_MESH_MODEL_OPERATION_EVT:
            ESP_LOGI(TAG, "接收到消息: 操作码 0x%06" PRIx32 ", 来自节点 0x%04x", 
                    param->model_operation.opcode, param->model_operation.ctx->addr);
            handle_status_message(param->model_operation.opcode, 
                                  param->model_operation.msg, 
                                  param->model_operation.length,
                                  param->model_operation.ctx->addr); // Pass src_addr
            break;
        case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
            if (param->model_send_comp.err_code) {
                ESP_LOGW(TAG, "发送失败，错误码 0x%04x，操作码 0x%06" PRIx32, 
                         param->model_send_comp.err_code, param->model_send_comp.opcode);
            } else {
                ESP_LOGI(TAG, "发送完成，操作码 0x%06" PRIx32, 
                         param->model_send_comp.opcode);
            }
            break;
        case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
            ESP_LOGI(TAG, "接收到发布消息：操作码 0x%06" PRIx32 ", 来自节点 0x%04x", 
                     param->client_recv_publish_msg.opcode, param->client_recv_publish_msg.ctx->addr);
            handle_status_message(param->client_recv_publish_msg.opcode,
                                  param->client_recv_publish_msg.msg,
                                  param->client_recv_publish_msg.length,
                                  param->client_recv_publish_msg.ctx->addr); // Pass src_addr
            break;
        case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
            ESP_LOGW(TAG, "客户端消息超时，操作码 0x%06" PRIx32 ", 目标节点 0x%04x", 
                     param->client_send_timeout.opcode, param->client_send_timeout.ctx->addr);
            
            int timed_out_server_idx = _find_server_index(param->client_send_timeout.ctx->addr);
            if (timed_out_server_idx != -1) {
                store.servers[timed_out_server_idx].consecutive_timeouts++;
                ESP_LOGI(TAG, "Server 0x%04x timeout count: %u", 
                         store.servers[timed_out_server_idx].addr, 
                         store.servers[timed_out_server_idx].consecutive_timeouts);
                if (store.servers[timed_out_server_idx].consecutive_timeouts >= MAX_CONSECUTIVE_TIMEOUTS) {
                    if (store.servers[timed_out_server_idx].is_online) {
                         ESP_LOGW(TAG, "Server 0x%04x is now OFFLINE (timeouts: %u).", 
                                 store.servers[timed_out_server_idx].addr,
                                 store.servers[timed_out_server_idx].consecutive_timeouts);
                        store.servers[timed_out_server_idx].is_online = false;
                        // Optionally, you might want to reset consecutive_timeouts here or keep it
                        // to indicate it went offline due to N timeouts. For now, let's keep it.
                    }
                }
            }
            break;
        default:
            break;
    }
}

/* 设置电源状态 */
esp_err_t ac_client_set_power(uint16_t server_addr, uint8_t power_state)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint8_t msg = (power_state <= AC_POWER_ON) ? power_state : AC_POWER_OFF;
    esp_err_t err = ESP_OK;

    set_msg_common(&common, server_addr, AC_OP_SET_POWER);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send power set message");
        return err;
    }

    ESP_LOGI(TAG, "Send power control: %d", power_state);
    return ESP_OK;
}

/* 获取电源状态 */
esp_err_t ac_client_get_power(uint16_t server_addr)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    esp_err_t err = ESP_OK;

    set_msg_common(&common, server_addr, AC_OP_GET_POWER);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    ESP_LOGI(TAG, "准备发送电源状态查询，server: 0x%04x, net_idx: 0x%04x, app_idx: 0x%04x", 
             server_addr, ctx.net_idx, ctx.app_idx);

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "发送获取电源状态消息失败，错误码: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "发送获取电源状态请求成功");
    return ESP_OK;
}

/* 设置温度 */
esp_err_t ac_client_set_temperature(uint16_t server_addr, uint8_t temperature)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint8_t msg;
    esp_err_t err = ESP_OK;

    if (temperature < AC_TEMP_MIN) {
        temperature = AC_TEMP_MIN;
    } else if (temperature > AC_TEMP_MAX) {
        temperature = AC_TEMP_MAX;
    }
    msg = temperature;

    set_msg_common(&common, server_addr, AC_OP_SET_TEMPERATURE);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send temperature set message");
        return err;
    }

    ESP_LOGI(TAG, "Send temperature control: %d", temperature);
    return ESP_OK;
}

/* 获取温度状态 */
esp_err_t ac_client_get_temperature(uint16_t server_addr)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    esp_err_t err = ESP_OK;

    set_msg_common(&common, server_addr, AC_OP_GET_TEMPERATURE);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send temperature get message");
        return err;
    }

    ESP_LOGI(TAG, "Send get temperature status request");
    return ESP_OK;
}

/* 设置模式 */
esp_err_t ac_client_set_mode(uint16_t server_addr, uint8_t mode)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint8_t msg;
    esp_err_t err = ESP_OK;

    if (mode > AC_MODE_AUTO) {
        mode = AC_MODE_AUTO;
    }
    msg = mode;

    set_msg_common(&common, server_addr, AC_OP_SET_MODE);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send mode set message");
        return err;
    }

    ESP_LOGI(TAG, "Send mode control: %d", mode);
    return ESP_OK;
}

/* 获取模式状态 */
esp_err_t ac_client_get_mode(uint16_t server_addr)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    esp_err_t err = ESP_OK;

    set_msg_common(&common, server_addr, AC_OP_GET_MODE);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send mode get message");
        return err;
    }

    ESP_LOGI(TAG, "Send get mode status request");
    return ESP_OK;
}

/* 设置风速 */
esp_err_t ac_client_set_fan_speed(uint16_t server_addr, uint8_t fan_speed)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint8_t msg;
    esp_err_t err = ESP_OK;

    if (fan_speed > AC_FAN_SPEED_HIGH) {
        fan_speed = AC_FAN_SPEED_LOW;
    }
    msg = fan_speed;

    set_msg_common(&common, server_addr, AC_OP_SET_FAN_SPEED);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send fan speed set message");
        return err;
    }

    ESP_LOGI(TAG, "Send fan speed control: %d", fan_speed);
    return ESP_OK;
}

/* 获取风速状态 */
esp_err_t ac_client_get_fan_speed(uint16_t server_addr)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    esp_err_t err = ESP_OK;

    set_msg_common(&common, server_addr, AC_OP_GET_FAN_SPEED);
    
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send fan speed get message");
        return err;
    }

    ESP_LOGI(TAG, "Send get fan speed status request");
    return ESP_OK;
}

void ac_ble_mesh_store_info(void)
{
    ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &store, sizeof(store));
}

void ac_ble_mesh_restore_info(void)
{
    esp_err_t err = ESP_OK;
    bool exist = false;

    // Initialize server_addrs to unassigned before restoring
    for (int i = 0; i < MAX_AC_SERVERS; i++) {
        // store.server_addrs[i] = ESP_BLE_MESH_ADDR_UNASSIGNED; // Old way
        store.servers[i].addr = ESP_BLE_MESH_ADDR_UNASSIGNED;
        store.servers[i].is_online = false; // Default to offline until proven otherwise
        store.servers[i].consecutive_timeouts = 0;
    }
    store.num_servers = 0;

    err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &store, sizeof(store), &exist);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restore NVS info (err %d)", err);
        return;
    }

    if (exist) {
        ESP_LOGI(TAG, "Restored NVS: num_servers %u, vnd_tid 0x%04x", store.num_servers, store.vnd_tid);
        // 重启后强制所有服务器状态为离线，只有收到消息后才标记为在线
        for (uint8_t i = 0; i < store.num_servers; i++) {
            // 强制设置为离线状态，重置超时计数器
            store.servers[i].is_online = false;
            store.servers[i].consecutive_timeouts = 0;
            ESP_LOGI(TAG, "  Server[%u] addr 0x%04x, set to offline after restart", 
                     i, store.servers[i].addr);
        }
    } else {
        ESP_LOGI(TAG, "NVS info not found or empty.");
    }
}

// uint16_t ac_get_server_addr(void)
// {
//     if (store.num_servers > 0) {
//         // Return the first server as a default, if online, otherwise try to find an online one
//         if (store.servers[0].is_online) {
//             return store.servers[0].addr;
//         }
//         for (uint8_t i = 0; i < store.num_servers; i++) {
//             if (store.servers[i].is_online) {
//                 return store.servers[i].addr;
//             }
//         }
//         // If all are offline, return the first one's address anyway or unassigned
//         return store.servers[0].addr; 
//     }
//     return ESP_BLE_MESH_ADDR_UNASSIGNED;
// }

void ac_add_server_addr(uint16_t addr)
{
    if (addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        ESP_LOGW(TAG, "Cannot add unassigned address to server list.");
        return;
    }

    // Check if server already exists
    for (uint8_t i = 0; i < store.num_servers; i++) {
        if (store.servers[i].addr == addr) {
            ESP_LOGI(TAG, "Server address 0x%04x already in list. Resetting state.", addr);
            // If it exists, reset its state as it's being (re-)provisioned or re-added
            store.servers[i].is_online = true;
            store.servers[i].consecutive_timeouts = 0;
            return; 
        }
    }

    // Add new server if there is space
    if (store.num_servers < MAX_AC_SERVERS) {
        store.servers[store.num_servers].addr = addr;
        store.servers[store.num_servers].is_online = true; // Assume online when first added/provisioned
        store.servers[store.num_servers].consecutive_timeouts = 0;
        store.num_servers++;
        ESP_LOGI(TAG, "Added new server 0x%04x. Total servers: %u", addr, store.num_servers);
        // Optionally, immediately save to NVS if desired, though _prov_complete also calls store_info
        ac_ble_mesh_store_info();
    } else {
        ESP_LOGW(TAG, "Server list full. Cannot add new server 0x%04x.", addr);
    }
}

uint8_t ac_get_num_servers(void)
{
    return store.num_servers;
}

uint16_t ac_get_server_addr_by_index(uint8_t index)
{
    if (index < store.num_servers) {
        return store.servers[index].addr;
    }
    ESP_LOGW(TAG, "Index %u out of bounds for server list (num_servers: %u).", index, store.num_servers);
    return ESP_BLE_MESH_ADDR_UNASSIGNED;
}

bool ac_is_server_online(uint16_t server_addr)
{
    int server_idx = _find_server_index(server_addr);
    if (server_idx != -1) {
        return store.servers[server_idx].is_online;
    }
    ESP_LOGW(TAG, "Server 0x%04x not found in managed list for online check.", server_addr);
    return false; // Server not found, so not online in our list
}

// Moved BLE helper functions from main.c (internal implementations)
static void _example_ble_mesh_set_msg_common(esp_ble_mesh_client_common_param_t *common,
                                            esp_ble_mesh_node_t *node,
                                            esp_ble_mesh_model_t *model, uint32_t opcode)
{
    common->opcode = opcode;
    common->model = model;
    common->ctx.net_idx = prov_key.net_idx;
    common->ctx.app_idx = prov_key.app_idx;
    common->ctx.addr = node->unicast_addr;
    common->ctx.send_ttl = MSG_SEND_TTL;
    common->msg_timeout = MSG_TIMEOUT; // Using the define from original main.c context (0)
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    common->msg_role = MSG_ROLE;
#endif
}

static esp_err_t _prov_complete(uint16_t node_index, const esp_ble_mesh_octet16_t uuid,
                               uint16_t primary_addr, uint8_t element_num, uint16_t net_idx)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_get_state_t get = {0};
    esp_ble_mesh_node_t *node = NULL;
    char name[11] = {0}; 
    esp_err_t err;

    ESP_LOGI(TAG, "Node provisioned: Idx %u, PrimaryAddr 0x%04x, ElmNum %u, NetIdx 0x%03x",
        node_index, primary_addr, element_num, net_idx);
    ESP_LOG_BUFFER_HEX("Device UUID", uuid, ESP_BLE_MESH_OCTET16_LEN);

    ac_add_server_addr(primary_addr); // Use the helper
    ac_ble_mesh_store_info();      // Use the helper

    sprintf(name, "%s%02u", "NODE-", node_index);
    err = esp_ble_mesh_provisioner_set_node_name(node_index, name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set node name (err %d)", err);
        return err; 
    }

    node = esp_ble_mesh_provisioner_get_node_with_addr(primary_addr);
    if (node == NULL) {
        ESP_LOGE(TAG, "Failed to get node 0x%04x info", primary_addr);
        return ESP_FAIL; 
    }

    _example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET);
    get.comp_data_get.page = COMP_DATA_PAGE_0;
    err = esp_ble_mesh_config_client_get_state(&common, &get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Config Comp Data Get (err %d)", err);
        return err; 
    }

    return ESP_OK;
}

static void _recv_unprov_adv_pkt(uint8_t dev_uuid_match[ESP_BLE_MESH_OCTET16_LEN], uint8_t addr[BD_ADDR_LEN],
                                esp_ble_mesh_addr_type_t addr_type, uint16_t oob_info,
                                uint8_t adv_type, esp_ble_mesh_prov_bearer_t bearer)
{
    esp_ble_mesh_unprov_dev_add_t add_dev = {0};
    esp_err_t err;

    ESP_LOG_BUFFER_HEX("Device Address", addr, BD_ADDR_LEN);
    ESP_LOGI(TAG, "Address type 0x%02x, adv type 0x%02x", addr_type, adv_type);
    ESP_LOG_BUFFER_HEX("Received Device UUID for Provisioning", dev_uuid_match, ESP_BLE_MESH_OCTET16_LEN);
    ESP_LOGI(TAG, "OOB info 0x%04x, bearer %s", oob_info, (bearer & ESP_BLE_MESH_PROV_ADV) ? "PB-ADV" : "PB-GATT");

    memcpy(add_dev.addr, addr, BD_ADDR_LEN);
    add_dev.addr_type = addr_type;
    memcpy(add_dev.uuid, dev_uuid_match, ESP_BLE_MESH_OCTET16_LEN);
    add_dev.oob_info = oob_info;
    add_dev.bearer = bearer;
    err = esp_ble_mesh_provisioner_add_unprov_dev(&add_dev,
            ADD_DEV_RM_AFTER_PROV_FLAG | ADD_DEV_START_PROV_NOW_FLAG | ADD_DEV_FLUSHABLE_DEV_FLAG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning device (err %d)", err);
    }
}

static void _example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ProvRegisterComp: err %d", param->prov_register_comp.err_code);
        if(param->prov_register_comp.err_code == ESP_OK) {
            ac_ble_mesh_restore_info(); 
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ProvEnableComp: err %d", param->provisioner_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_DISABLE_COMP_EVT:
        ESP_LOGI(TAG, "ProvDisableComp: err %d", param->provisioner_prov_disable_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_RECV_UNPROV_ADV_PKT_EVT:
        _recv_unprov_adv_pkt(param->provisioner_recv_unprov_adv_pkt.dev_uuid, param->provisioner_recv_unprov_adv_pkt.addr,
                            param->provisioner_recv_unprov_adv_pkt.addr_type, param->provisioner_recv_unprov_adv_pkt.oob_info,
                            param->provisioner_recv_unprov_adv_pkt.adv_type, param->provisioner_recv_unprov_adv_pkt.bearer);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ProvLinkOpen: bearer %s",
            param->provisioner_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ProvLinkClose: bearer %s, reason 0x%02x",
            param->provisioner_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT", param->provisioner_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        _prov_complete(param->provisioner_prov_complete.node_idx, param->provisioner_prov_complete.device_uuid,
                      param->provisioner_prov_complete.unicast_addr, param->provisioner_prov_complete.element_num,
                      param->provisioner_prov_complete.netkey_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_UNPROV_DEV_COMP_EVT:
        ESP_LOGI(TAG, "AddUnprovDevComp: err %d", param->provisioner_add_unprov_dev_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_DEV_UUID_MATCH_COMP_EVT:
        ESP_LOGI(TAG, "SetDevUuidMatchComp: err %d", param->provisioner_set_dev_uuid_match_comp.err_code);
        break;
    case ESP_BLE_MESH_PROVISIONER_SET_NODE_NAME_COMP_EVT:
        ESP_LOGI(TAG, "SetNodeNameComp: err %d", param->provisioner_set_node_name_comp.err_code);
        if (param->provisioner_set_node_name_comp.err_code == 0) {
            const char *name = esp_ble_mesh_provisioner_get_node_name(param->provisioner_set_node_name_comp.node_index);
            if (name) {
                ESP_LOGI(TAG, "Node %d name set: %s", param->provisioner_set_node_name_comp.node_index, name);
            }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT:
        ESP_LOGI(TAG, "AddLocalAppKeyComp: err %d, AppIdx 0x%04x",
                     param->provisioner_add_app_key_comp.err_code, param->provisioner_add_app_key_comp.app_idx);
        if (param->provisioner_add_app_key_comp.err_code == 0) {
            prov_key.app_idx = param->provisioner_add_app_key_comp.app_idx;
            esp_err_t err = esp_ble_mesh_provisioner_bind_app_key_to_local_model(PROV_OWN_ADDR, prov_key.app_idx,
                    MY_MODEL_ID_AC_CLIENT, MY_COMPANY_ID);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to bind AppKey to AC client model (err %d)", err);
            }
        }
        break;
    case ESP_BLE_MESH_PROVISIONER_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "BindAppKeyToModelComp: err %d, Addr 0x%04x, ModelID 0x%04x, AppIdx 0x%04x",
            param->provisioner_bind_app_key_to_model_comp.err_code, param->provisioner_bind_app_key_to_model_comp.element_addr,
            param->provisioner_bind_app_key_to_model_comp.model_id, param->provisioner_bind_app_key_to_model_comp.app_idx);
        break;
    case ESP_BLE_MESH_PROVISIONER_STORE_NODE_COMP_DATA_COMP_EVT:
        ESP_LOGI(TAG, "StoreNodeCompDataComp: err %d", param->provisioner_store_node_comp_data_comp.err_code);
        break;
    default:
        ESP_LOGW(TAG, "Unhandled provisioning event: %d", event);
        break;
    }
}

static void _example_ble_mesh_parse_node_comp_data(const uint8_t *data, uint16_t length)
{
    uint16_t cid, pid, vid, crpl, feat;
    uint16_t loc, model_id, company_id;
    uint8_t nums, numv;
    uint16_t offset;
    int i;

    if (length < 10) { 
        ESP_LOGE(TAG, "Composition data too short (%d bytes)", length);
        return;
    }

    cid = COMP_DATA_2_OCTET(data, 0);
    pid = COMP_DATA_2_OCTET(data, 2);
    vid = COMP_DATA_2_OCTET(data, 4);
    crpl = COMP_DATA_2_OCTET(data, 6);
    feat = COMP_DATA_2_OCTET(data, 8);
    offset = 10;

    ESP_LOGI(TAG, "***** Composition Data For Node *****");
    ESP_LOGI(TAG, "* CID 0x%04x, PID 0x%04x, VID 0x%04x, CRPL 0x%04x, Feat 0x%04x *", cid, pid, vid, crpl, feat);
    for (; offset < length; ) {
        if (offset + 4 > length) { ESP_LOGW(TAG, "CompData: Short element header"); break; }
        loc = COMP_DATA_2_OCTET(data, offset);
        nums = COMP_DATA_1_OCTET(data, offset + 2);
        numv = COMP_DATA_1_OCTET(data, offset + 3);
        offset += 4;
        ESP_LOGI(TAG, "* Loc 0x%04x, NumS %u, NumV %u *", loc, nums, numv);
        for (i = 0; i < nums; i++) {
            if (offset + 2 > length) { ESP_LOGW(TAG, "CompData: Short SIG Model list"); break; }
            model_id = COMP_DATA_2_OCTET(data, offset);
            ESP_LOGI(TAG, "* SIG Model ID 0x%04x *", model_id);
            offset += 2;
        }
        if (i < nums) break; 
        for (i = 0; i < numv; i++) {
            if (offset + 4 > length) { ESP_LOGW(TAG, "CompData: Short Vendor Model list"); break; }
            company_id = COMP_DATA_2_OCTET(data, offset);
            model_id = COMP_DATA_2_OCTET(data, offset + 2);
            ESP_LOGI(TAG, "* VendorModel(CID 0x%04x, MID 0x%04x) *", company_id, model_id);
            offset += 4;
        }
        if (i < numv) break; 
    }
    ESP_LOGI(TAG, "***********************************");
}

static void _example_ble_mesh_config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                                              esp_ble_mesh_cfg_client_cb_param_t *param)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_cfg_client_set_state_t set = {0};
    esp_ble_mesh_node_t *node = NULL;
    esp_err_t err;

    ESP_LOGD(TAG, "ConfigClient: evt %u, err %d, addr 0x%04x, op 0x%04" PRIx32,
        event, param->error_code, param->params->ctx.addr, param->params->opcode);

    if (param->error_code) {
        ESP_LOGE(TAG, "ConfigClient: Op 0x%04" PRIx32 " failed (err %d)", param->params->opcode, param->error_code);
        return;
    }

    node = esp_ble_mesh_provisioner_get_node_with_addr(param->params->ctx.addr);
    if (!node) {
        ESP_LOGE(TAG, "ConfigClient: Node 0x%04x not found for cb", param->params->ctx.addr);
        return;
    }

    switch (event) {
    case ESP_BLE_MESH_CFG_CLIENT_GET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_COMPOSITION_DATA_GET) {
            ESP_LOG_BUFFER_HEX("Received Comp Data", param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            _example_ble_mesh_parse_node_comp_data(param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            err = esp_ble_mesh_provisioner_store_node_comp_data(param->params->ctx.addr,
                param->status_cb.comp_data_status.composition_data->data,
                param->status_cb.comp_data_status.composition_data->len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to store node comp data (err %d)", err);
                break; 
            }

            _example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD);
            set.app_key_add.net_idx = prov_key.net_idx;
            set.app_key_add.app_idx = prov_key.app_idx;
            memcpy(set.app_key_add.app_key, prov_key.app_key, ESP_BLE_MESH_OCTET16_LEN);
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config AppKey Add (err %d)", err);
            }
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_SET_STATE_EVT:
        if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD) {
            _example_ble_mesh_set_msg_common(&common, node, config_client.model, ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND);
            set.model_app_bind.element_addr = node->unicast_addr; 
            set.model_app_bind.model_app_idx = prov_key.app_idx;
            set.model_app_bind.model_id = MY_MODEL_ID_AC_SERVER; // Bind to AC Server model
            set.model_app_bind.company_id = MY_COMPANY_ID;
            err = esp_ble_mesh_config_client_set_state(&common, &set);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send Config Model App Bind (err %d)", err);
            }
        } else if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND) {
            ESP_LOGI(TAG, "Node 0x%04x provisioned & configured!", node->unicast_addr);
        }
        break;
    case ESP_BLE_MESH_CFG_CLIENT_PUBLISH_EVT: 
        ESP_LOGI(TAG, "ConfigClient: Publish from 0x%04x, op 0x%04" PRIx32, param->params->ctx.addr, param->params->opcode);
        break;
    case ESP_BLE_MESH_CFG_CLIENT_TIMEOUT_EVT:
        ESP_LOGW(TAG, "ConfigClient: Timeout for 0x%04x, op 0x%04" PRIx32, param->params->ctx.addr, param->params->opcode);
        // Add retry logic if needed, similar to main.c example if desired
        // For brevity, not fully reimplementing retry here.
        break;
    default:
        ESP_LOGE(TAG, "ConfigClient: Invalid event %u", event);
        break;
    }
}

// Main BLE initialization function for AC Client
// esp_err_t ac_ble_mesh_init(ac_status_callback_t status_cb)
static esp_err_t ac_ble_mesh_init(void)
{
    esp_err_t err;
    
    // Initialize NVS for storing BLE Mesh info
    // Note: Ensure ble_mesh_nvs_open is available in your project.
    // If not, you might need to implement or copy it from ESP-IDF examples.
    err = ble_mesh_nvs_open(&NVS_HANDLE); 
    if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to open NVS for AC client (err %d). Ensure NVS is initialized in main.", err);
        // Not returning here, as NVS might be optional for basic operation if no prior info exists.
        // However, storing/restoring will fail.
    }

    // Initialize device UUID. 
    // Using a common UUID pattern for AC devices or a board-specific one.
    // The original main.c used ble_mesh_get_dev_uuid(dev_uuid) and then matched on {0x32, 0x10}.
    // The original ac_control.c had dev_uuid[16] = {0xdd, 0xdd}.
    // Let's use the {0xdd, 0xdd} for the provisioner's own UUID and for matching, as it was in ac_control.c
    // and seems more specific to the AC client's role.
    dev_uuid[0] = 0xDD;
    dev_uuid[1] = 0xDD;
    // The rest of dev_uuid can be filled by esp_ble_mesh_init based on MAC or other unique info if not fully set.
    // For matching, we will use these first two bytes.
    uint8_t match_uuid_prefix[2] = {0xDD, 0xDD}; // Devices to look for (e.g. AC Server)


    // Initialize provisioning key data
    prov_key.net_idx = ESP_BLE_MESH_KEY_PRIMARY; // Use primary network key
    prov_key.app_idx = APP_KEY_IDX;             // Use defined AppKey index
    memset(prov_key.app_key, APP_KEY_OCTET, sizeof(prov_key.app_key)); // Set AppKey value

    // Register BLE Mesh callbacks (provisioning and config client)
    esp_ble_mesh_register_prov_callback(_example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_client_callback(_example_ble_mesh_config_client_cb);
    esp_ble_mesh_register_custom_model_callback(ac_client_model_cb);

    // Initialize BLE Mesh stack
    err = esp_ble_mesh_init(&provision, &composition); // provision.uuid uses global dev_uuid
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE mesh stack (err %d)", err);
        return err;
    }

    // Initialize client models. The config client model is part of root_models and initialized by esp_ble_mesh_init.
    // The vendor client model (ac_client) needs explicit initialization.
    // vnd_models[0] is the ac_client model.
    err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize AC vendor client model (err %d)", err);
        return err;
    }
    // Assign the initialized model pointer to the global ac_client structure
    ac_client.model = &vnd_models[0]; 

    // Set the device UUID match for the provisioner to scan for specific unprovisioned devices.
    // This uses the `match_uuid_prefix` (e.g. {0xDD,0xDD} or {0x32,0x10} from old main.c example)
    err = esp_ble_mesh_provisioner_set_dev_uuid_match(match_uuid_prefix, sizeof(match_uuid_prefix), 0x0, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set device UUID match (err %d)", err);
        return err;
    }

    // Enable provisioning functionality (both PB-ADV and PB-GATT bearers)
    err = esp_ble_mesh_provisioner_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh provisioner (err %d)", err);
        return err;
    }

    // Add the local AppKey to the provisioner's AppKey list.
    err = esp_ble_mesh_provisioner_add_local_app_key(prov_key.app_key, prov_key.net_idx, prov_key.app_idx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add local AppKey (err %d)", err);
        // This is critical, binding to local model will fail too.
        return err;
    }
    // Note: Binding of app key to the *local* ac_client model is handled in ESP_BLE_MESH_PROVISIONER_ADD_LOCAL_APP_KEY_COMP_EVT
    // in _example_ble_mesh_provisioning_cb.

    ESP_LOGI(TAG, "AC BLE Mesh Client initialized successfully.");
    return ESP_OK;
} 

/* AC客户端初始化 */
esp_err_t ac_client_init(void)
{
    esp_err_t err = ESP_OK;

    ac_ble_mesh_init();

    ESP_LOGI(TAG, "AC client initialized");
    return err;
}
