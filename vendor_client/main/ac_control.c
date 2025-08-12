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
#include <stdio.h>
#include "esp_timer.h"
#include "esp_random.h"

#include "mesh_common.h"
#include "ble_mesh_example_nvs.h"

#define TAG "AC_CLIENT"

static uint16_t self_primary_addr = ESP_BLE_MESH_ADDR_UNASSIGNED;

static void handle_sync_resp_message(const uint8_t *data, uint16_t len);

// BLE related definitions from main.c
#define PROV_OWN_ADDR       0x0001
#define MSG_SEND_TTL        7
#define MSG_TIMEOUT         0
#define MSG_ROLE            ROLE_NODE
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
} prov_key = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
};

/* Global BLE variables from main.c */
static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = {0xdd,0xdd};
// static uint16_t client_primary_addr;

/* Structure to hold information about each managed AC server */
typedef struct {
    uint16_t addr;                          /* Server unicast address */
    bool is_online;                         /* Online status */
    bool is_configured;                     /* Configuration completed status */
    bool is_filtered;                       /* Whether device is in provisioning filter */
    bool is_manually_disconnected;         /* Whether device was manually disconnected */
    bool is_blacklisted;                    /* Whether device is in blacklist (truly removed from network) */
    bool is_in_group;                       /* Whether device is in multicast group */
    uint8_t consecutive_timeouts;           /* Count of consecutive send timeouts */
    uint8_t set_cmd_timeout_count;          /* Count of consecutive set command timeouts */
    bool is_set_cmd_unresponsive;           /* Whether device is unresponsive to set commands */
    uint8_t device_uuid[ESP_BLE_MESH_OCTET16_LEN]; /* Device UUID for blacklist checking */
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

/* ==================== Key Refresh状态管理 ==================== */
static bool key_refresh_in_progress = false;
static uint32_t key_refresh_start_time = 0;
#define KEY_REFRESH_TIMEOUT_MS 30000  /* 30秒超时 */

/* ==================== 消息队列相关变量 ==================== */
static ac_msg_queue_item_t msg_queue[AC_MSG_QUEUE_SIZE];
static uint8_t queue_head = 0;          /* 队列头指针 */
static uint8_t queue_tail = 0;          /* 队列尾指针 */
static uint8_t queue_count = 0;         /* 队列中消息数量 */
static ac_send_state_t send_state = AC_SEND_STATE_IDLE;
static ac_msg_queue_item_t current_msg; /* 当前正在发送的消息 */

/* ==================== 设备删除和网络管理API实现 ==================== */

// 全局变量跟踪断开连接ACK状态
static volatile bool disconnect_ack_received = false;
static volatile uint16_t disconnect_ack_device_addr = 0;

/* AC状态回调函数 */
// static ac_status_callback_t ac_status_cb = NULL;

/* 前向声明 - 消息队列管理函数 */
static esp_err_t _enqueue_message(ac_msg_type_t msg_type, uint16_t server_addr, uint8_t value);
static esp_err_t _dequeue_message(ac_msg_queue_item_t *msg);
static esp_err_t _send_ble_mesh_message(const ac_msg_queue_item_t *msg);
static void _process_next_message(void);

/* 前向声明 - 组播管理函数 */
static esp_err_t _send_group_message(ac_msg_type_t msg_type, uint8_t value);

static esp_err_t ac_send_sync_request(void);

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
    /* 断开连接处理器 */
    ESP_BLE_MESH_MODEL_OP(AC_OP_DISCONNECT_ACK, 0),
    /* 同步响应处理器 */
    ESP_BLE_MESH_MODEL_OP(AC_OP_SYNC_RESP, 0),
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
    /* 断开连接操作对 */
    {AC_OP_DISCONNECT_NOTIFY, AC_OP_DISCONNECT_ACK},
    /* 同步操作对 */
    {AC_OP_SYNC_REQ, AC_OP_SYNC_RESP},
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

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server_cfg),
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
    .uuid = dev_uuid,
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
    common->msg_timeout = 1000;
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

/* Helper function to check if message type needs response */
static bool _needs_response(ac_msg_type_t msg_type) {
    switch (msg_type) {
        case AC_MSG_TYPE_SET_POWER:
        // case AC_MSG_TYPE_GET_POWER:
        case AC_MSG_TYPE_SET_TEMPERATURE:
        // case AC_MSG_TYPE_GET_TEMPERATURE:
        case AC_MSG_TYPE_SET_MODE:
        // case AC_MSG_TYPE_GET_MODE:
        case AC_MSG_TYPE_SET_FAN_SPEED:
        // case AC_MSG_TYPE_GET_FAN_SPEED:
            return true;
        case AC_MSG_TYPE_HEARTBEAT_ACK:
        case AC_MSG_TYPE_DISCONNECT_NOTIFY:
        case AC_MSG_TYPE_GET_POWER:
        case AC_MSG_TYPE_GET_TEMPERATURE:
        case AC_MSG_TYPE_GET_MODE:
        case AC_MSG_TYPE_GET_FAN_SPEED:
        default:
            return false;
    }
}

/* Helper function to check if message type is a SET command */
static bool _is_set_command(ac_msg_type_t msg_type) {
    switch (msg_type) {
        case AC_MSG_TYPE_SET_POWER:
        case AC_MSG_TYPE_SET_TEMPERATURE:
        case AC_MSG_TYPE_SET_MODE:
        case AC_MSG_TYPE_SET_FAN_SPEED:
            return true;
        default:
            return false;
    }
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
    
    // Node 角色不在本端自动修改组播订阅，由 Provisioner 统一管理
    
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
    
    // 收到状态响应，重置set命令超时计数器，并标记为响应正常
    server->set_cmd_timeout_count = 0;
    server->is_set_cmd_unresponsive = false;
    
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
        ac_server_info_t *server = &store.servers[server_idx];
        bool was_online = server->is_online;
        bool was_configured = server->is_configured;
        
        // If device can send messages, it means it's configured
        if (!was_configured) {
            ESP_LOGI(TAG, "Device 0x%04x automatically marked as configured (received message)", src_addr);
            server->is_configured = true;
        }
        
        if (!was_online) {
            ESP_LOGI(TAG, "Server 0x%04x is back online.", src_addr);
            server->is_online = true;
            // 设备重新上线，重置所有超时计数器
            server->consecutive_timeouts = 0;
            server->set_cmd_timeout_count = 0;
            server->is_set_cmd_unresponsive = false;
            // 调用设备上线回调
            if (device_online_cb) {
                device_online_cb(src_addr, true);
            }
        }
        server->is_online = true;
        server->consecutive_timeouts = 0;
    }

    switch (opcode) {
        case AC_OP_HEARTBEAT:
            ESP_LOGD(TAG, "Received heartbeat from server 0x%04x", src_addr);
            /* 将心跳包ACK响应加入消息队列，使用统一的发送机制 */
            esp_err_t err = _enqueue_message(AC_MSG_TYPE_HEARTBEAT_ACK, src_addr, 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to enqueue heartbeat ACK to 0x%04x: %s", src_addr, esp_err_to_name(err));
            } else {
                ESP_LOGD(TAG, "Heartbeat ACK enqueued for server 0x%04x", src_addr);
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
        case AC_OP_DISCONNECT_ACK:
            ESP_LOGI(TAG, "Received disconnect ACK from server 0x%04x", src_addr);
            
            // 设置ACK接收标志
            if (disconnect_ack_device_addr == src_addr) {
                disconnect_ack_received = true;
                ESP_LOGI(TAG, "Disconnect ACK flag set for device 0x%04x", src_addr);
            }
            
            // Server已确认断开连接通知，可以继续删除流程
            if (server_idx != -1) {
                ESP_LOGI(TAG, "Server 0x%04x confirmed disconnect, proceeding with removal", src_addr);
                // 标记设备为已断开
                store.servers[server_idx].is_manually_disconnected = true;
                store.servers[server_idx].is_online = false;
                // 调用设备下线回调
                if (device_online_cb) {
                    device_online_cb(src_addr, false);
                }
            }
            return; /* 断开连接ACK不需要进一步处理 */
        case AC_OP_SYNC_RESP:
            handle_sync_resp_message(data, len);
            return;
        default:
            return;
    }
}

// 将解析函数体放在前向声明之后（文件头部已有声明）
// 删除重复实现，函数体在更前位置提供

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
                ac_server_info_t *server = &store.servers[timed_out_server_idx];
                server->consecutive_timeouts++;
                
                // 判断当前消息是否为set命令
                bool is_set_cmd = false;
                if (send_state == AC_SEND_STATE_SENDING) {
                    is_set_cmd = _is_set_command(current_msg.msg_type);
                }
                
                if (is_set_cmd) {
                    // Set命令超时处理
                    server->set_cmd_timeout_count++;
                    ESP_LOGW(TAG, "Set command timeout for server 0x%04x, count: %u", 
                             server->addr, server->set_cmd_timeout_count);
                    
                    if (server->set_cmd_timeout_count >= 2) {
                        if (!server->is_set_cmd_unresponsive) {
                            ESP_LOGW(TAG, "Server 0x%04x marked as SET command unresponsive (timeouts: %u)", 
                                     server->addr, server->set_cmd_timeout_count);
                            server->is_set_cmd_unresponsive = true;
                            // 调用设备状态变化回调，通知UI更新为空心图标
                            if (device_online_cb) {
                                device_online_cb(server->addr, false);
                            }
                        }
                    }
                } else {
                    // Get命令或其他命令的超时处理保持原逻辑
                    ESP_LOGD(TAG, "Server 0x%04x general timeout count: %u", 
                             server->addr, server->consecutive_timeouts);
                }
                
                // 一般超时处理（所有命令类型）
                if (server->consecutive_timeouts >= MAX_CONSECUTIVE_TIMEOUTS) {
                    if (server->is_online) {
                         ESP_LOGW(TAG, "Server 0x%04x is now OFFLINE (timeouts: %u).", 
                                 server->addr, server->consecutive_timeouts);
                        server->is_online = false;
                        // 调用设备下线回调
                        if (device_online_cb) {
                            device_online_cb(server->addr, false);
                        }
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
        store.servers[i].is_filtered = false; // Default to not filtered
        store.servers[i].is_manually_disconnected = false; // Default to not manually disconnected
        store.servers[i].is_blacklisted = false; // Default to not blacklisted
        store.servers[i].is_in_group = false; // Default to not in group
        store.servers[i].consecutive_timeouts = 0;
        store.servers[i].set_cmd_timeout_count = 0;
        store.servers[i].is_set_cmd_unresponsive = false;
        memset(store.servers[i].device_uuid, 0, ESP_BLE_MESH_OCTET16_LEN); // Clear UUID
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
            store.servers[i].set_cmd_timeout_count = 0;
            store.servers[i].is_set_cmd_unresponsive = false;
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
        new_server->is_filtered = false; // Default to not filtered
        new_server->is_manually_disconnected = false; // Default to not manually disconnected
        new_server->is_blacklisted = false; // Default to not blacklisted
        new_server->is_in_group = false; // Default to not in group
        new_server->consecutive_timeouts = 0;
        new_server->set_cmd_timeout_count = 0;
        new_server->is_set_cmd_unresponsive = false;
        memset(new_server->device_uuid, 0, ESP_BLE_MESH_OCTET16_LEN); // Clear UUID initially
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

/* 检查设备是否对set命令响应正常（用于UI图标显示） */
bool ac_is_device_set_cmd_responsive(uint16_t device_addr)
{
    int server_idx = _find_server_index(device_addr);
    if (server_idx != -1) {
        ac_server_info_t *server = &store.servers[server_idx];
        // 设备在线且配置完成且对set命令响应正常
        return server->is_online && server->is_configured && !server->is_set_cmd_unresponsive;
    }
    ESP_LOGW(TAG, "Device 0x%04x not found in managed list for responsiveness check.", device_addr);
    return false;
}

// Moved BLE helper functions from main.c (internal implementations)

static void _example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ProvRegisterComp: err %d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "NodeProvEnableComp: err %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "NodeProvLinkOpen: bearer %s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "NodeProvLinkClose: reason 0x%02x", param->node_prov_link_close.reason);
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "Node provisioned: addr 0x%04x, net_idx 0x%03x",
                 param->node_prov_complete.addr, param->node_prov_complete.net_idx);
        prov_key.net_idx = param->node_prov_complete.net_idx;
        self_primary_addr = param->node_prov_complete.addr;
        // Node-only：不在本地添加或绑定 AppKey，由外部 Provisioner 完成
        break;
    case ESP_BLE_MESH_NODE_ADD_LOCAL_APP_KEY_COMP_EVT:
        // Node-only：不期望触发本事件，若触发仅记录日志
        ESP_LOGW(TAG, "Unexpected local AppKey add comp: err %d, app_idx 0x%04x",
                 param->node_add_app_key_comp.err_code, param->node_add_app_key_comp.app_idx);
        break;
    case ESP_BLE_MESH_NODE_BIND_APP_KEY_TO_MODEL_COMP_EVT:
        ESP_LOGI(TAG, "Local model bind comp: err %d, elem 0x%04x, model 0x%04x, cid 0x%04x",
                 param->node_bind_app_key_to_model_comp.err_code,
                 param->node_bind_app_key_to_model_comp.element_addr,
                 param->node_bind_app_key_to_model_comp.model_id,
                 param->node_bind_app_key_to_model_comp.company_id);
        if (param->node_bind_app_key_to_model_comp.err_code == 0 &&
            param->node_bind_app_key_to_model_comp.model_id == MY_MODEL_ID_AC_CLIENT &&
            param->node_bind_app_key_to_model_comp.company_id == MY_COMPANY_ID) {
            ac_send_sync_request();
        }
        break;
    default:
        break;
    }
}

// Main BLE initialization function for AC Client
// esp_err_t ac_ble_mesh_init(ac_status_callback_t status_cb)
static esp_err_t ac_ble_mesh_init(void)
{
    esp_err_t err;

    err = ble_mesh_nvs_open(&NVS_HANDLE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %d", err);
    }

    // 注册回调
    esp_ble_mesh_register_prov_callback(_example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_custom_model_callback(ac_client_model_cb);

    // 初始化 Node 形态的 composition（保留 CFG_SRV，去掉 CFG_CLI）已在上方定义
    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ble_mesh_init failed %d", err);
        return err;
    }

    // 初始化 vendor client 模型并赋值
    err = esp_ble_mesh_client_model_init(&vnd_models[0]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "vendor client init failed %d", err);
        return err;
    }
    ac_client.model = &vnd_models[0];

    // // 启用前强制恢复为未配网状态（每次启动均未配网）
    // {
    //     esp_err_t reset_err = esp_ble_mesh_node_local_reset();
    //     if (reset_err != ESP_OK) {
    //         ESP_LOGW(TAG, "force local reset (unprovisioned) returned %d", reset_err);
    //     } else {
    //         ESP_LOGI(TAG, "force local reset done, node is now unprovisioned");
    //     }
    // }

    // 启用 Node 的 PB-ADV/PB-GATT 握手
    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "node prov enable failed %d", err);
        return err;
    }

    ESP_LOGI(TAG, "AC BLE Mesh Node initialized, waiting for provisioning...");
    return ESP_OK;
}

/* ==================== UI接口函数实现 ==================== */

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
        device_list[i].is_filtered = server->is_filtered;
        device_list[i].is_manually_disconnected = server->is_manually_disconnected;
        device_list[i].is_blacklisted = server->is_blacklisted;
        device_list[i].is_in_group = server->is_in_group;
        device_list[i].is_set_cmd_unresponsive = server->is_set_cmd_unresponsive;
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
    device_info->is_filtered = server->is_filtered;
    device_info->is_manually_disconnected = server->is_manually_disconnected;
    device_info->is_blacklisted = server->is_blacklisted;
    device_info->is_in_group = server->is_in_group;
    device_info->is_set_cmd_unresponsive = server->is_set_cmd_unresponsive;
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
    
    // 检查目标设备是否已配置完成（心跳ACK和断开连接通知除外）
    if (msg_type != AC_MSG_TYPE_HEARTBEAT_ACK && msg_type != AC_MSG_TYPE_DISCONNECT_NOTIFY) {
        int server_idx = _find_server_index(server_addr);
        if (server_idx == -1) {
            ESP_LOGE(TAG, "Target device 0x%04x not found in server list", server_addr);
            return ESP_ERR_NOT_FOUND;
        }
        
        if (!store.servers[server_idx].is_configured) {
            ESP_LOGW(TAG, "Target device 0x%04x is not configured yet, cannot send message", server_addr);
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // 创建消息项
    ac_msg_queue_item_t *item = &msg_queue[queue_tail];
    item->msg_type = msg_type;
    item->server_addr = server_addr;
    item->value = value;
    item->timestamp = _get_current_timestamp();
    
    queue_tail = (queue_tail + 1) % AC_MSG_QUEUE_SIZE;
    queue_count++;
    
    ESP_LOGD(TAG, "Enqueued message type %d to 0x%04x, queue size: %d", msg_type, server_addr, queue_count);
    
    // 如果当前没有正在发送的消息，立即处理
    if (send_state == AC_SEND_STATE_IDLE) {
        _process_next_message();
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
    
    // 检查目标设备是否已配置完成（心跳ACK和断开连接通知除外）
    if (msg->msg_type != AC_MSG_TYPE_HEARTBEAT_ACK && msg->msg_type != AC_MSG_TYPE_DISCONNECT_NOTIFY) {
        int server_idx = _find_server_index(msg->server_addr);
        if (server_idx == -1) {
            ESP_LOGE(TAG, "Target device 0x%04x not found in server list", msg->server_addr);
            return ESP_ERR_NOT_FOUND;
        }
        
        // 检查设备是否在黑名单中
        if (store.servers[server_idx].is_blacklisted) {
            ESP_LOGW(TAG, "Target device 0x%04x is blacklisted, cannot send message", msg->server_addr);
            return ESP_ERR_INVALID_STATE;
        }
        
        if (!store.servers[server_idx].is_configured) {
            ESP_LOGW(TAG, "Target device 0x%04x is not configured yet, cannot send message", msg->server_addr);
            return ESP_ERR_INVALID_STATE;
        }
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
        case AC_MSG_TYPE_HEARTBEAT_ACK:
            opcode = AC_OP_HEARTBEAT_ACK;
            data = NULL;
            length = 0;
            break;
        case AC_MSG_TYPE_DISCONNECT_NOTIFY:
            opcode = AC_OP_DISCONNECT_NOTIFY;
            data = NULL;
            length = 0;
            ESP_LOGI(TAG, "Sending disconnect notification to device 0x%04x", msg->server_addr);
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
    
    // Node-only：若外部未完成本地模型绑定或未设置有效的 AppKey/NetKey，则拒绝发送
    if (prov_key.app_idx == ESP_BLE_MESH_KEY_UNUSED || prov_key.net_idx == ESP_BLE_MESH_KEY_UNUSED) {
        ESP_LOGW(TAG, "Unicast send aborted: AppKey/NetKey not ready (bound by external provisioner)");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Attempting to send BLE mesh message type %d to 0x%04x (opcode: 0x%06" PRIx32 ")", 
             msg->msg_type, msg->server_addr, opcode);
    
    // 根据消息类型决定是否需要响应
    bool need_rsp = _needs_response(msg->msg_type);
    if (need_rsp) {
        ESP_LOGD(TAG, "Message needs response, need_rsp=true");
    } else {
        ESP_LOGD(TAG, "Message doesn't need response, need_rsp=false");
    }
    
    // 发送消息
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, opcode,
                                           length, data, common.msg_timeout, need_rsp, 
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

/* ==================== 组播管理函数 ==================== */

/* 内部函数：发送组播消息 */
static esp_err_t _send_group_message(ac_msg_type_t msg_type, uint8_t value) {
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    esp_err_t err = ESP_OK;
    uint32_t opcode = 0;
    uint8_t *data = NULL;
    uint16_t length = 0;
    uint8_t msg_data = 0;
    
    ESP_LOGI(TAG, "Sending group message type %d with value %d to group 0x%04x", msg_type, value, AC_GROUP_ADDR);
    
    // 根据消息类型设置操作码和数据
    switch (msg_type) {
        case AC_MSG_TYPE_SET_POWER:
            opcode = AC_OP_SET_POWER;
            msg_data = (value <= AC_POWER_ON) ? value : AC_POWER_OFF;
            data = &msg_data;
            length = 1;
            break;
        case AC_MSG_TYPE_SET_TEMPERATURE:
            opcode = AC_OP_SET_TEMPERATURE;
            msg_data = (value < AC_TEMP_MIN) ? AC_TEMP_MIN : 
                      (value > AC_TEMP_MAX) ? AC_TEMP_MAX : value;
            data = &msg_data;
            length = 1;
            break;
        case AC_MSG_TYPE_SET_MODE:
            opcode = AC_OP_SET_MODE;
            msg_data = (value > AC_MODE_AUTO) ? AC_MODE_AUTO : value;
            data = &msg_data;
            length = 1;
            break;
        case AC_MSG_TYPE_SET_FAN_SPEED:
            opcode = AC_OP_SET_FAN_SPEED;
            msg_data = (value > AC_FAN_SPEED_HIGH) ? AC_FAN_SPEED_LOW : value;
            data = &msg_data;
            length = 1;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported group message type: %d", msg_type);
            return ESP_ERR_INVALID_ARG;
    }
    
    // 设置组播消息参数
    set_msg_common(&common, AC_GROUP_ADDR, opcode);
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = AC_GROUP_ADDR; // 使用组播地址
    ctx.send_ttl = common.ctx.send_ttl;

    if (prov_key.app_idx == ESP_BLE_MESH_KEY_UNUSED || prov_key.net_idx == ESP_BLE_MESH_KEY_UNUSED) {
        ESP_LOGW(TAG, "Group send aborted: AppKey/NetKey not ready (bound by external provisioner)");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Sending group command: opcode 0x%06" PRIx32 " to group 0x%04x", opcode, AC_GROUP_ADDR);
    
    // 发送组播消息
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, opcode,
                                           length, data, common.msg_timeout, false, 
                                           MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send group message: %s (0x%d)", esp_err_to_name(err), err);
        return err;
    }
    
    ESP_LOGI(TAG, "Successfully sent group message type %d to group 0x%04x", msg_type, AC_GROUP_ADDR);
    return ESP_OK;
}

/* ==================== 组播控制公共API ==================== */

/* 向组播地址发送控制指令（群控） */
esp_err_t ac_send_group_command(ac_status_type_t command_type, uint8_t value) {
    ac_msg_type_t msg_type;
    
    // 将状态类型转换为消息类型
    switch (command_type) {
        case AC_STATUS_POWER:
            msg_type = AC_MSG_TYPE_SET_POWER;
            break;
        case AC_STATUS_TEMPERATURE:
            msg_type = AC_MSG_TYPE_SET_TEMPERATURE;
            break;
        case AC_STATUS_MODE:
            msg_type = AC_MSG_TYPE_SET_MODE;
            break;
        case AC_STATUS_FAN_SPEED:
            msg_type = AC_MSG_TYPE_SET_FAN_SPEED;
            break;
        default:
            ESP_LOGE(TAG, "Invalid command type for group control: %d", command_type);
            return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Sending group command type %d with value %d", command_type, value);
    
    // 直接发送组播消息，不使用队列（组播消息通常不需要响应）
    esp_err_t err = _send_group_message(msg_type, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send group command: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Group command sent successfully");
    return ESP_OK;
}

/* 获取组播地址 */
uint16_t ac_get_group_address(void) {
    return AC_GROUP_ADDR;
}

/* 检查设备是否在组播组中 */
bool ac_is_device_in_group(uint16_t device_addr) {
    int server_idx = _find_server_index(device_addr);
    if (server_idx == -1) {
        return false; // 未找到设备，认为不在组播组中
    }
    
    return store.servers[server_idx].is_in_group;
}

// 发送同步请求到 Hub(0x0001)
static esp_err_t ac_send_sync_request(void)
{
    const uint16_t hub_addr = 0x0001;
    esp_ble_mesh_client_common_param_t common = {0};
    set_msg_common(&common, hub_addr, AC_OP_SYNC_REQ);

    uint8_t payload = 0; // 预留版本/保留字段
    esp_ble_mesh_msg_ctx_t *ctx = &common.ctx;
    uint32_t opcode = common.opcode;

    esp_err_t err = esp_ble_mesh_client_model_send_msg(ac_client.model, ctx, opcode,
                                                       sizeof(payload), &payload, MSG_TIMEOUT, true, MSG_ROLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send SYNC_REQ to 0x%04x: %s", hub_addr, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SYNC_REQ sent to 0x%04x", hub_addr);
    return ESP_OK;
}

static void handle_sync_resp_message(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 5) {
        ESP_LOGW(TAG, "SYNC_RESP too short");
        return;
    }
    uint8_t total = data[0];
    uint8_t offset = data[1];
    uint8_t count = data[2];
    uint16_t group_addr = data[3] | (data[4] << 8);
    uint8_t version = (len >= 6) ? data[5] : 0;

    ESP_LOGI(TAG, "SYNC_RESP: total=%u offset=%u count=%u group=0x%04x ver=%u", total, offset, count, group_addr, version);

    const uint16_t header_len = (len >= 6) ? 6 : 5;
    const uint16_t rec_len = 23;
    const uint8_t *p = data + header_len;
    uint16_t remain = (len > header_len) ? (len - header_len) : 0;

    for (uint8_t i = 0; i < count && remain >= rec_len; i++) {
        uint16_t addr = p[0] | (p[1] << 8);
        uint8_t flags = p[2];
        char name[17] = {0};
        memcpy(name, &p[3], 16);
        uint8_t power = p[19];
        uint8_t temperature = p[20];
        uint8_t mode = p[21];
        uint8_t fan = p[22];

        int idx = _find_server_index(addr);
        if (idx == -1) {
            if (store.num_servers < MAX_AC_SERVERS) {
                idx = store.num_servers++;
                store.servers[idx].addr = addr;
            } else {
                ESP_LOGW(TAG, "Server list full, skip 0x%04x", addr);
                p += rec_len; remain -= rec_len; continue;
            }
        }
        ac_server_info_t *sv = &store.servers[idx];
        sv->is_online = (flags & 0x01) != 0;
        sv->is_in_group = (flags & 0x02) != 0;
        sv->is_configured = true;
        sv->power_state = power;
        sv->temperature = temperature;
        sv->mode = mode;
        sv->fan_speed = fan;
        strncpy(sv->device_name, name, sizeof(sv->device_name) - 1); sv->device_name[sizeof(sv->device_name) - 1] = '\0';
        sv->last_update_time = _get_current_timestamp();

        ESP_LOGI(TAG, "SYNC item: 0x%04x %s online=%d in_group=%d P=%u T=%u M=%u F=%u",
                 addr, sv->device_name, sv->is_online, sv->is_in_group, power, temperature, mode, fan);

        p += rec_len;
        remain -= rec_len;
    }

    ac_ble_mesh_store_info();
}
