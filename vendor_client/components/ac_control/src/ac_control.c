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
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_timer.h"

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
    bool is_configured;                     /* Configuration completed status */
    uint8_t consecutive_timeouts;           /* Count of consecutive send timeouts */
    /* 扩展设备状态信息 */
    uint8_t power_state;                    /* 电源状态 */
    uint8_t temperature;                    /* 设定温度 */
    uint8_t mode;                           /* 运行模式 */
    uint8_t fan_speed;                      /* 风速 */
    char device_name[16];                   /* 设备名称 */
    uint32_t last_update_time;              /* 最后更新时间戳 */
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

/* 回调函数指针 */
static ac_device_status_callback_t device_status_cb = NULL;
static ac_device_online_callback_t device_online_cb = NULL;
static ac_device_provisioned_callback_t device_provisioned_cb = NULL;

/* ==================== 消息队列相关变量 ==================== */
static ac_msg_queue_item_t msg_queue[AC_MSG_QUEUE_SIZE];
static uint8_t queue_head = 0;          /* 队列头指针 */
static uint8_t queue_tail = 0;          /* 队列尾指针 */
static uint8_t queue_count = 0;         /* 队列中消息数量 */
static ac_send_state_t send_state = AC_SEND_STATE_IDLE;
static ac_msg_queue_item_t current_msg; /* 当前正在发送的消息 */

/* AC状态回调函数 */
// static ac_status_callback_t ac_status_cb = NULL;

/* 前向声明 - 消息队列管理函数 */
static esp_err_t _enqueue_message(ac_msg_type_t msg_type, uint16_t server_addr, uint8_t value);
static esp_err_t _dequeue_message(ac_msg_queue_item_t *msg);
static esp_err_t _send_ble_mesh_message(const ac_msg_queue_item_t *msg);
static void _process_next_message(void);

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

/* Helper function to get current timestamp */
static uint32_t _get_current_timestamp(void) {
    return (uint32_t)(esp_timer_get_time() / 1000); // Convert to milliseconds
}

/* Helper function called when device configuration is completed */
static void _on_device_configured(uint16_t device_addr) {
    int server_idx = _find_server_index(device_addr);
    if (server_idx == -1) {
        ESP_LOGW(TAG, "Device 0x%04x not found in server list when marking as configured", device_addr);
        return;
    }
    
    ac_server_info_t *server = &store.servers[server_idx];
    server->is_configured = true;
    server->is_online = true;  // Now ready to communicate
    server->consecutive_timeouts = 0;
    server->last_update_time = _get_current_timestamp();
    
    ESP_LOGI(TAG, "Device 0x%04x is now configured and ready for communication!", device_addr);
    
    // Call device online callback
    if (device_online_cb) {
        device_online_cb(device_addr, true);
    }
    
    // Trigger status synchronization by requesting all status types
    ESP_LOGI(TAG, "Triggering status sync for newly configured device 0x%04x", device_addr);
    for (ac_status_type_t status = AC_STATUS_POWER; status <= AC_STATUS_FAN_SPEED; status++) {
        esp_err_t err = ac_get_status_by_addr(device_addr, status);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to request status type %d from newly configured device 0x%04x", status, device_addr);
        }
    }
}

/* Helper function to update device status */
static void _update_device_status(int server_idx, ac_status_type_t status_type, uint8_t value) {
    if (server_idx < 0 || server_idx >= store.num_servers) {
        return;
    }
    
    ac_server_info_t *server = &store.servers[server_idx];
    server->last_update_time = _get_current_timestamp();
    
    switch (status_type) {
        case AC_STATUS_POWER:
            server->power_state = value;
            break;
        case AC_STATUS_TEMPERATURE:
            server->temperature = value;
            break;
        case AC_STATUS_MODE:
            server->mode = value;
            break;
        case AC_STATUS_FAN_SPEED:
            server->fan_speed = value;
            break;
        default:
            break;
    }
    
    // 调用状态变化回调
    if (device_status_cb) {
        device_status_cb(server->addr, status_type, value);
    }
}

/* 处理状态消息 */
static void handle_status_message(uint32_t opcode, const uint8_t *data, uint16_t len, uint16_t src_addr)
{
    uint8_t type = 0;
    uint8_t value = 0;

    // Mark server as online and reset timeout count upon receiving any status message
    int server_idx = _find_server_index(src_addr);
    if (server_idx != -1) {
        bool was_online = store.servers[server_idx].is_online;
        bool was_configured = store.servers[server_idx].is_configured;
        
        // If device can send messages, it means it's configured
        if (!was_configured) {
            ESP_LOGI(TAG, "Device 0x%04x automatically marked as configured (received message)", src_addr);
            store.servers[server_idx].is_configured = true;
        }
        
        if (!was_online) {
            ESP_LOGI(TAG, "Server 0x%04x is back online.", src_addr);
            store.servers[server_idx].is_online = true;
            // 调用设备上线回调
            if (device_online_cb) {
                device_online_cb(src_addr, true);
            }
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
                if (server_idx != -1) {
                    _update_device_status(server_idx, type, value);
                }
            }
            break;
        case AC_OP_TEMPERATURE_STATUS:
            if (data != NULL && len >= 1) {
                value = data[0];
                type = AC_STATUS_TEMPERATURE;
                ESP_LOGI(TAG, "Received temperature status: %d", value);
                if (server_idx != -1) {
                    _update_device_status(server_idx, type, value);
                }
            }
            break;
        case AC_OP_MODE_STATUS:
            if (data != NULL && len >= 1) {
                value = data[0];
                type = AC_STATUS_MODE;
                ESP_LOGI(TAG, "Received mode status: %d", value);
                if (server_idx != -1) {
                    _update_device_status(server_idx, type, value);
                }
            }
            break;
        case AC_OP_FAN_SPEED_STATUS:
            if (data != NULL && len >= 1) {
                value = data[0];
                type = AC_STATUS_FAN_SPEED;
                ESP_LOGI(TAG, "Received fan speed status: %d", value);
                if (server_idx != -1) {
                    _update_device_status(server_idx, type, value);
                }
            }
            break;
        default:
            return;
    }
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
            
            // 消息发送完成，处理队列中的下一个消息
            if (send_state == AC_SEND_STATE_SENDING) {
                send_state = AC_SEND_STATE_IDLE;
                ESP_LOGD(TAG, "Message send completed, processing next message in queue");
                _process_next_message();
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
                        // 调用设备下线回调
                        if (device_online_cb) {
                            device_online_cb(store.servers[timed_out_server_idx].addr, false);
                        }
                        // Optionally, you might want to reset consecutive_timeouts here or keep it
                        // to indicate it went offline due to N timeouts. For now, let's keep it.
                    }
                }
            }
            
            // 消息发送超时，处理队列中的下一个消息
            if (send_state == AC_SEND_STATE_SENDING) {
                send_state = AC_SEND_STATE_IDLE;
                ESP_LOGD(TAG, "Message send timeout, processing next message in queue");
                _process_next_message();
            }
            break;
        default:
            break;
    }
}

/* 设置电源状态 */
esp_err_t ac_client_set_power(uint16_t server_addr, uint8_t power_state)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_SET_POWER, server_addr, power_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue power set message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued power control: %d to 0x%04x", power_state, server_addr);
    
    return ESP_OK;
}

/* 获取电源状态 */
esp_err_t ac_client_get_power(uint16_t server_addr)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_GET_POWER, server_addr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue power get message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued power status request to 0x%04x", server_addr);
    
    return ESP_OK;
}

/* 设置温度 */
esp_err_t ac_client_set_temperature(uint16_t server_addr, uint8_t temperature)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_SET_TEMPERATURE, server_addr, temperature);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue temperature set message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued temperature control: %d to 0x%04x", temperature, server_addr);
    
    return ESP_OK;
}

/* 获取温度状态 */
esp_err_t ac_client_get_temperature(uint16_t server_addr)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_GET_TEMPERATURE, server_addr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue temperature get message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued temperature status request to 0x%04x", server_addr);
    
    return ESP_OK;
}

/* 设置模式 */
esp_err_t ac_client_set_mode(uint16_t server_addr, uint8_t mode)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_SET_MODE, server_addr, mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue mode set message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued mode control: %d to 0x%04x", mode, server_addr);
    
    return ESP_OK;
}

/* 获取模式状态 */
esp_err_t ac_client_get_mode(uint16_t server_addr)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_GET_MODE, server_addr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue mode get message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued mode status request to 0x%04x", server_addr);
    
    return ESP_OK;
}

/* 设置风速 */
esp_err_t ac_client_set_fan_speed(uint16_t server_addr, uint8_t fan_speed)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_SET_FAN_SPEED, server_addr, fan_speed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue fan speed set message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued fan speed control: %d to 0x%04x", fan_speed, server_addr);
    
    return ESP_OK;
}

/* 获取风速状态 */
esp_err_t ac_client_get_fan_speed(uint16_t server_addr)
{
    esp_err_t err = _enqueue_message(AC_MSG_TYPE_GET_FAN_SPEED, server_addr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enqueue fan speed get message: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Enqueued fan speed status request to 0x%04x", server_addr);
    
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
        store.servers[i].is_configured = false; // Default to not configured
        store.servers[i].consecutive_timeouts = 0;
        // 初始化扩展字段
        store.servers[i].power_state = AC_POWER_OFF;
        store.servers[i].temperature = 25;
        store.servers[i].mode = AC_MODE_AUTO;
        store.servers[i].fan_speed = AC_FAN_SPEED_LOW;
        store.servers[i].last_update_time = 0;
        snprintf(store.servers[i].device_name, sizeof(store.servers[i].device_name), "AC_%04X", store.servers[i].addr);
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
            store.servers[i].is_configured = false; // Reset configuration status after restart
            store.servers[i].consecutive_timeouts = 0;
            // 如果设备名称为空，设置默认名称
            if (strlen(store.servers[i].device_name) == 0) {
                snprintf(store.servers[i].device_name, sizeof(store.servers[i].device_name), "AC_%04X", store.servers[i].addr);
            }
            ESP_LOGI(TAG, "  Server[%u] addr 0x%04x, name: %s, set to offline after restart", 
                     i, store.servers[i].addr, store.servers[i].device_name);
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
            store.servers[i].is_online = false;   // Will be set online when config completes
            store.servers[i].is_configured = false; // Reset configuration status
            store.servers[i].consecutive_timeouts = 0;
            store.servers[i].last_update_time = _get_current_timestamp();
            // 调用设备配网完成回调
            if (device_provisioned_cb) {
                device_provisioned_cb(addr);
            }
            return; 
        }
    }

    // Add new server if there is space
    if (store.num_servers < MAX_AC_SERVERS) {
        ac_server_info_t *new_server = &store.servers[store.num_servers];
        new_server->addr = addr;
        new_server->is_online = false;   // Will be set online when config completes and ready to communicate
        new_server->is_configured = false; // Configuration not completed yet
        new_server->consecutive_timeouts = 0;
        // 初始化设备状态
        new_server->power_state = AC_POWER_OFF;
        new_server->temperature = 25; // 默认温度
        new_server->mode = AC_MODE_AUTO;
        new_server->fan_speed = AC_FAN_SPEED_LOW;
        new_server->last_update_time = _get_current_timestamp();
        snprintf(new_server->device_name, sizeof(new_server->device_name), "AC_%04X", addr);
        
        store.num_servers++;
        ESP_LOGI(TAG, "Added new server 0x%04x. Total servers: %u", addr, store.num_servers);
        
        // 调用设备配网完成回调
        if (device_provisioned_cb) {
            device_provisioned_cb(addr);
        }
        
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
        // Only consider device online if it's both online and configured
        return store.servers[server_idx].is_online && store.servers[server_idx].is_configured;
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
            _on_device_configured(node->unicast_addr);
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

/* ==================== UI接口函数实现 ==================== */

/* 注册回调函数 */
void ac_client_register_callbacks(ac_device_status_callback_t status_cb, 
                                 ac_device_online_callback_t online_cb,
                                 ac_device_provisioned_callback_t provisioned_cb)
{
    device_status_cb = status_cb;
    device_online_cb = online_cb;
    device_provisioned_cb = provisioned_cb;
    ESP_LOGI(TAG, "Callbacks registered for device status, online status, and provisioning events.");
}

/* 获取设备数量 */
uint8_t ac_get_device_count(void)
{
    return store.num_servers;
}

/* 获取在线设备数量 */
uint8_t ac_get_online_device_count(void)
{
    uint8_t online_count = 0;
    for (uint8_t i = 0; i < store.num_servers; i++) {
        if (store.servers[i].is_online && store.servers[i].is_configured) {
            online_count++;
        }
    }
    return online_count;
}

/* 获取设备列表 */
uint8_t ac_get_device_list(ac_device_info_t *device_list, uint8_t max_devices)
{
    if (device_list == NULL || max_devices == 0) {
        return 0;
    }
    
    uint8_t count = (store.num_servers < max_devices) ? store.num_servers : max_devices;
    
    for (uint8_t i = 0; i < count; i++) {
        ac_server_info_t *server = &store.servers[i];
        device_list[i].addr = server->addr;
        device_list[i].is_online = server->is_online;
        device_list[i].is_configured = server->is_configured;
        device_list[i].power_state = server->power_state;
        device_list[i].temperature = server->temperature;
        device_list[i].mode = server->mode;
        device_list[i].fan_speed = server->fan_speed;
        device_list[i].last_update_time = server->last_update_time;
        strncpy(device_list[i].device_name, server->device_name, sizeof(device_list[i].device_name) - 1);
        device_list[i].device_name[sizeof(device_list[i].device_name) - 1] = '\0';
    }
    
    return count;
}

/* 根据索引获取设备信息 */
esp_err_t ac_get_device_info_by_index(uint8_t index, ac_device_info_t *device_info)
{
    if (device_info == NULL || index >= store.num_servers) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ac_server_info_t *server = &store.servers[index];
    device_info->addr = server->addr;
    device_info->is_online = server->is_online;
    device_info->is_configured = server->is_configured;
    device_info->power_state = server->power_state;
    device_info->temperature = server->temperature;
    device_info->mode = server->mode;
    device_info->fan_speed = server->fan_speed;
    device_info->last_update_time = server->last_update_time;
    strncpy(device_info->device_name, server->device_name, sizeof(device_info->device_name) - 1);
    device_info->device_name[sizeof(device_info->device_name) - 1] = '\0';
    
    return ESP_OK;
}

/* 根据地址获取设备信息 */
esp_err_t ac_get_device_info_by_addr(uint16_t device_addr, ac_device_info_t *device_info)
{
    if (device_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int server_idx = _find_server_index(device_addr);
    if (server_idx == -1) {
        return ESP_ERR_NOT_FOUND;
    }
    
    return ac_get_device_info_by_index(server_idx, device_info);
}

/* 根据索引发送控制指令 */
esp_err_t ac_send_command_by_index(uint8_t device_index, ac_status_type_t command_type, uint8_t value)
{
    if (device_index >= store.num_servers) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t device_addr = store.servers[device_index].addr;
    return ac_send_command_by_addr(device_addr, command_type, value);
}

/* 根据地址发送控制指令 */
esp_err_t ac_send_command_by_addr(uint16_t device_addr, ac_status_type_t command_type, uint8_t value)
{
    esp_err_t err = ESP_OK;
    
    switch (command_type) {
        case AC_STATUS_POWER:
            err = ac_client_set_power(device_addr, value);
            break;
        case AC_STATUS_TEMPERATURE:
            err = ac_client_set_temperature(device_addr, value);
            break;
        case AC_STATUS_MODE:
            err = ac_client_set_mode(device_addr, value);
            break;
        case AC_STATUS_FAN_SPEED:
            err = ac_client_set_fan_speed(device_addr, value);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent command type %d with value %d to device 0x%04x", command_type, value, device_addr);
    } else {
        ESP_LOGE(TAG, "Failed to send command type %d to device 0x%04x (err %d)", command_type, device_addr, err);
    }
    
    return err;
}

/* 根据索引获取设备状态 */
esp_err_t ac_get_status_by_index(uint8_t device_index, ac_status_type_t status_type)
{
    if (device_index >= store.num_servers) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t device_addr = store.servers[device_index].addr;
    return ac_get_status_by_addr(device_addr, status_type);
}

/* 根据地址获取设备状态 */
esp_err_t ac_get_status_by_addr(uint16_t device_addr, ac_status_type_t status_type)
{
    esp_err_t err = ESP_OK;
    
    switch (status_type) {
        case AC_STATUS_POWER:
            err = ac_client_get_power(device_addr);
            break;
        case AC_STATUS_TEMPERATURE:
            err = ac_client_get_temperature(device_addr);
            break;
        case AC_STATUS_MODE:
            err = ac_client_get_mode(device_addr);
            break;
        case AC_STATUS_FAN_SPEED:
            err = ac_client_get_fan_speed(device_addr);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Requested status type %d from device 0x%04x", status_type, device_addr);
    } else {
        ESP_LOGE(TAG, "Failed to request status type %d from device 0x%04x (err %d)", status_type, device_addr, err);
    }
    
    return err;
}

/* 向所有在线设备发送控制指令 */
esp_err_t ac_send_command_to_all_online(ac_status_type_t command_type, uint8_t value)
{
    esp_err_t result = ESP_OK;
    uint8_t success_count = 0;
    uint8_t total_online = 0;
    
    for (uint8_t i = 0; i < store.num_servers; i++) {
        if (store.servers[i].is_online && store.servers[i].is_configured) {
            total_online++;
            esp_err_t err = ac_send_command_by_addr(store.servers[i].addr, command_type, value);
            if (err == ESP_OK) {
                success_count++;
            } else {
                result = err; // 记录最后一个错误
            }
        }
    }
    
    ESP_LOGI(TAG, "Sent command type %d (value %d) to %u/%u online devices", 
             command_type, value, success_count, total_online);
    
    return (success_count > 0) ? ESP_OK : result;
}

/* 刷新所有设备状态 */
esp_err_t ac_refresh_all_device_status(void)
{
    esp_err_t result = ESP_OK;
    uint8_t success_count = 0;
    uint8_t total_online = 0;
    
    // 对每个在线设备获取所有状态
    for (uint8_t i = 0; i < store.num_servers; i++) {
        if (store.servers[i].is_online && store.servers[i].is_configured) {
            total_online++;
            bool device_success = true;
            
            // 获取所有状态类型
            for (ac_status_type_t status = AC_STATUS_POWER; status <= AC_STATUS_FAN_SPEED; status++) {
                esp_err_t err = ac_get_status_by_addr(store.servers[i].addr, status);
                if (err != ESP_OK) {
                    device_success = false;
                    result = err;
                }
            }
            
            if (device_success) {
                success_count++;
            }
        }
    }
    
    ESP_LOGI(TAG, "Refreshed status for %u/%u online devices", success_count, total_online);
    
    return (success_count > 0) ? ESP_OK : result;
}

/* 设置设备名称 */
esp_err_t ac_set_device_name(uint16_t device_addr, const char *name)
{
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int server_idx = _find_server_index(device_addr);
    if (server_idx == -1) {
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(store.servers[server_idx].device_name, name, sizeof(store.servers[server_idx].device_name) - 1);
    store.servers[server_idx].device_name[sizeof(store.servers[server_idx].device_name) - 1] = '\0';
    
    // 保存到NVS
    ac_ble_mesh_store_info();
    
    ESP_LOGI(TAG, "Set device 0x%04x name to: %s", device_addr, name);
    
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

/* ==================== 消息队列管理函数 ==================== */

/* 向队列添加消息 */
static esp_err_t _enqueue_message(ac_msg_type_t msg_type, uint16_t server_addr, uint8_t value) {
    if (queue_count >= AC_MSG_QUEUE_SIZE) {
        ESP_LOGW(TAG, "Message queue full, dropping message type %d to 0x%04x", msg_type, server_addr);
        return ESP_ERR_NO_MEM;
    }
    
    bool queue_was_empty = (queue_count == 0);
    
    msg_queue[queue_tail].msg_type = msg_type;
    msg_queue[queue_tail].server_addr = server_addr;
    msg_queue[queue_tail].value = value;
    msg_queue[queue_tail].timestamp = _get_current_timestamp();
    
    queue_tail = (queue_tail + 1) % AC_MSG_QUEUE_SIZE;
    queue_count++;
    
    ESP_LOGD(TAG, "Enqueued message type %d to 0x%04x, queue size: %d", msg_type, server_addr, queue_count);
    
    // 只有当队列之前为空且当前没有消息在发送时，才立即处理
    if (queue_was_empty && send_state == AC_SEND_STATE_IDLE) {
        ESP_LOGD(TAG, "Queue was empty, processing message immediately");
        _process_next_message();
    } else {
        ESP_LOGD(TAG, "Message queued, waiting for current transmission to complete");
    }
    
    return ESP_OK;
}

/* 从队列取出消息 */
static esp_err_t _dequeue_message(ac_msg_queue_item_t *msg) {
    if (queue_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (msg) {
        *msg = msg_queue[queue_head];
    }
    
    queue_head = (queue_head + 1) % AC_MSG_QUEUE_SIZE;
    queue_count--;
    
    ESP_LOGD(TAG, "Dequeued message, remaining queue size: %d", queue_count);
    return ESP_OK;
}

/* 清空消息队列 */
void ac_clear_msg_queue(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    send_state = AC_SEND_STATE_IDLE;
    ESP_LOGI(TAG, "Message queue cleared");
}

/* 获取队列大小 */
uint8_t ac_get_queue_size(void) {
    return queue_count;
}

/* 获取发送状态 */
ac_send_state_t ac_get_send_state(void) {
    return send_state;
}

/* 实际发送消息的底层函数 */
static esp_err_t _send_ble_mesh_message(const ac_msg_queue_item_t *msg) {
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    esp_err_t err = ESP_OK;
    uint32_t opcode = 0;
    uint8_t *data = NULL;
    uint16_t length = 0;
    uint8_t msg_data = 0;
    
    if (!msg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查发送状态
    if (send_state != AC_SEND_STATE_SENDING) {
        ESP_LOGE(TAG, "Invalid send state %d when trying to send message", send_state);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查目标设备是否已配置完成
    int server_idx = _find_server_index(msg->server_addr);
    if (server_idx == -1) {
        ESP_LOGE(TAG, "Target device 0x%04x not found in server list", msg->server_addr);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (!store.servers[server_idx].is_configured) {
        ESP_LOGW(TAG, "Target device 0x%04x is not configured yet, cannot send message", msg->server_addr);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 根据消息类型设置操作码和数据
    switch (msg->msg_type) {
        case AC_MSG_TYPE_SET_POWER:
            opcode = AC_OP_SET_POWER;
            msg_data = (msg->value <= AC_POWER_ON) ? msg->value : AC_POWER_OFF;
            data = &msg_data;
            length = 1;
            break;
        case AC_MSG_TYPE_GET_POWER:
            opcode = AC_OP_GET_POWER;
            data = NULL;
            length = 0;
            break;
        case AC_MSG_TYPE_SET_TEMPERATURE:
            opcode = AC_OP_SET_TEMPERATURE;
            msg_data = (msg->value < AC_TEMP_MIN) ? AC_TEMP_MIN : 
                      (msg->value > AC_TEMP_MAX) ? AC_TEMP_MAX : msg->value;
            data = &msg_data;
            length = 1;
            break;
        case AC_MSG_TYPE_GET_TEMPERATURE:
            opcode = AC_OP_GET_TEMPERATURE;
            data = NULL;
            length = 0;
            break;
        case AC_MSG_TYPE_SET_MODE:
            opcode = AC_OP_SET_MODE;
            msg_data = (msg->value > AC_MODE_AUTO) ? AC_MODE_AUTO : msg->value;
            data = &msg_data;
            length = 1;
            break;
        case AC_MSG_TYPE_GET_MODE:
            opcode = AC_OP_GET_MODE;
            data = NULL;
            length = 0;
            break;
        case AC_MSG_TYPE_SET_FAN_SPEED:
            opcode = AC_OP_SET_FAN_SPEED;
            msg_data = (msg->value > AC_FAN_SPEED_HIGH) ? AC_FAN_SPEED_LOW : msg->value;
            data = &msg_data;
            length = 1;
            break;
        case AC_MSG_TYPE_GET_FAN_SPEED:
            opcode = AC_OP_GET_FAN_SPEED;
            data = NULL;
            length = 0;
            break;
        default:
            ESP_LOGE(TAG, "Unknown message type: %d", msg->msg_type);
            return ESP_ERR_INVALID_ARG;
    }
    
    // 设置消息参数
    set_msg_common(&common, msg->server_addr, opcode);
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;
    
    ESP_LOGD(TAG, "Attempting to send BLE mesh message type %d to 0x%04x (opcode: 0x%06" PRIx32 ")", 
             msg->msg_type, msg->server_addr, opcode);
    
    // 发送消息
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, opcode,
                                           length, data, common.msg_timeout, false, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send BLE mesh message type %d to 0x%04x: %s (0x%d)", 
                msg->msg_type, msg->server_addr, esp_err_to_name(err), err);
        
        // 对于TIMEOUT错误，表示BLE Mesh栈繁忙，我们应该重新排队这个消息
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "BLE Mesh stack busy, will retry message later");
        }
        return err;
    }
    
    ESP_LOGI(TAG, "Successfully sent BLE mesh message type %d to 0x%04x (opcode: 0x%06" PRIx32 ")", 
             msg->msg_type, msg->server_addr, opcode);
    return ESP_OK;
}

/* 处理队列中的下一个消息 */
static void _process_next_message(void) {
    if (send_state != AC_SEND_STATE_IDLE) {
        ESP_LOGD(TAG, "Send state not idle (%d), cannot process next message", send_state);
        return;
    }
    
    if (queue_count == 0) {
        ESP_LOGD(TAG, "Message queue empty, nothing to process");
        return;
    }
    
    esp_err_t err = _dequeue_message(&current_msg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to dequeue message: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGD(TAG, "Processing message type %d to 0x%04x from queue", 
             current_msg.msg_type, current_msg.server_addr);
    
    send_state = AC_SEND_STATE_SENDING;
    err = _send_ble_mesh_message(&current_msg);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send message type %d to 0x%04x: %s", 
                current_msg.msg_type, current_msg.server_addr, esp_err_to_name(err));
        
        send_state = AC_SEND_STATE_IDLE;
        
        // 对于BLE Mesh栈繁忙的情况，将消息重新放回队列头部进行重试
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGI(TAG, "Re-queueing message due to BLE Mesh busy, queue size: %d", queue_count);
            
            // 将消息放回队列头部
            queue_head = (queue_head == 0) ? (AC_MSG_QUEUE_SIZE - 1) : (queue_head - 1);
            msg_queue[queue_head] = current_msg;
            queue_count++;
            
            ESP_LOGD(TAG, "Message re-queued, will retry after short delay");
            // 注意：在实际系统中，这里可能需要定时器来延迟重试
            // 目前先不立即重试，等待下次合适的时机
            return;
        } else {
            // 对于其他错误，丢弃消息并继续处理下一个
            ESP_LOGE(TAG, "Dropping message due to non-recoverable error: %s", esp_err_to_name(err));
            // 立即尝试处理下一个消息
            _process_next_message();
        }
    } else {
        ESP_LOGD(TAG, "Message sent successfully, waiting for completion callback");
        // 发送成功，等待ESP_BLE_MESH_MODEL_SEND_COMP_EVT或超时事件
    }
}
