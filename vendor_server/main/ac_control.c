/* ac_control.c - Air Conditioner Bluetooth Mesh Client Control Implementation */

#include "ac_control.h"
#include "esp_log.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"
#include "ble_mesh_example_init.h"
#include "board.h"
#include <string.h>
#include <inttypes.h>

#include "mesh_common.h"
#include "esp_timer.h"

#define TAG_AC_CTRL "AC_SERVER_CTRL"

/* BLE Mesh Specific Definitions (Moved from main.c) */
#define CID_ESP     MY_COMPANY_ID

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = {0xdd,0xdd};

static esp_ble_mesh_cfg_srv_t config_server = {
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
    .default_ttl = 7,
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

/* 心跳包配置 */
#define HEARTBEAT_INTERVAL_MS    2000   /* 2秒发送一次心跳包 */
#define MAX_HEARTBEAT_TIMEOUTS   3      /* 最大超时次数 */

/* 心跳包状态管理 */
static struct {
    uint16_t client_addr;               /* 客户端地址 */
    esp_timer_handle_t timer_handle;    /* 定时器句柄 */
    uint8_t timeout_count;              /* 超时计数 */
    bool is_connected;                  /* 连接状态 */
    bool heartbeat_enabled;             /* 心跳包是否启用 */
} heartbeat_state = {
    .client_addr = 0,
    .timer_handle = NULL,
    .timeout_count = 0,
    .is_connected = false,
    .heartbeat_enabled = false
};

/* 当前空调状态 */
static struct {
    uint8_t power;       /* 0: OFF, 1: ON */
    uint8_t temperature; /* 16-30°C */
    uint8_t mode;        /* 0: COOL, 1: HEAT, 2: FAN, 3: DRY, 4: AUTO */
    uint8_t fan_speed;   /* 0: AUTO, 1: LOW, 2: MEDIUM, 3: HIGH */
} ac_state = {
    .power = AC_POWER_OFF,
    .temperature = 25,
    .mode = AC_MODE_COOL,
    .fan_speed = AC_FAN_SPEED_AUTO
};

/* 定义模型操作项 */
esp_ble_mesh_model_op_t ac_server_op[] = {
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_POWER, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_POWER, 0),
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_TEMPERATURE, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_TEMPERATURE, 0),
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_MODE, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_MODE, 0),
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_FAN_SPEED, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_FAN_SPEED, 0),
    ESP_BLE_MESH_MODEL_OP(AC_OP_HEARTBEAT_ACK, 0),
    ESP_BLE_MESH_MODEL_OP_END,
};

esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, MY_MODEL_ID_AC_SERVER,
    ac_server_op, NULL, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

/* BLE Mesh Callback functions (Moved from main.c and made static) */

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG_AC_CTRL, "Provisioning complete: net_idx 0x%03x, addr 0x%04x", net_idx, addr);
    ESP_LOGI(TAG_AC_CTRL, "flags 0x%02x, iv_index 0x%08" PRIx32, flags, iv_index);
    board_led_operation(LED_G, LED_OFF);
    ESP_LOGI(TAG_AC_CTRL, "Waiting for app key binding before starting heartbeat");
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
            param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
            param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
            param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}

static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG_AC_CTRL, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG_AC_CTRL, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG_AC_CTRL, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            
            if (param->value.state_change.mod_app_bind.company_id == MY_COMPANY_ID &&
                param->value.state_change.mod_app_bind.model_id == MY_MODEL_ID_AC_SERVER) {
                ESP_LOGI(TAG_AC_CTRL, "AC Server model app key bound.");
                uint16_t provisioner_addr = param->ctx.addr;
                ESP_LOGI(TAG_AC_CTRL, "Provisioner address is 0x%04x", provisioner_addr);
                esp_err_t err = ac_server_start_heartbeat(provisioner_addr);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG_AC_CTRL, "Failed to start heartbeat mechanism (err %d)", err);
                } else {
                    ESP_LOGI(TAG_AC_CTRL, "Heartbeat mechanism started for client 0x%04x", provisioner_addr);
                }
            }
            break;
        default:
            break;
        }
    }
}

static void example_ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                             esp_ble_mesh_model_cb_param_t *param)
{
    esp_err_t err = ESP_OK;
    uint8_t status_payload = 0;
    uint32_t status_opcode = 0;
    uint8_t value_received = 0;

    switch (event) {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        ESP_LOGI(TAG_AC_CTRL, "Received MSG: opcode 0x%06" PRIx32 ", src 0x%04x, dst 0x%04x, len %d",
                 param->model_operation.opcode, param->model_operation.ctx->addr,
                 param->model_operation.ctx->recv_dst, param->model_operation.length);

        switch (param->model_operation.opcode) {
            case AC_OP_SET_POWER:
                if (param->model_operation.length == 1) {
                    value_received = param->model_operation.msg[0];
                    ESP_LOGI(TAG_AC_CTRL, "AC_OP_SET_POWER: value %d", value_received);
                    err = ac_server_set_power(value_received);
                    status_payload = ac_server_get_current_power();
                    status_opcode = AC_OP_POWER_STATUS;
                } else {
                    ESP_LOGE(TAG_AC_CTRL, "AC_OP_SET_POWER: Invalid message length %d, expected 1", param->model_operation.length);
                    err = ESP_ERR_INVALID_ARG;
                }
                break;
            case AC_OP_GET_POWER:
                ESP_LOGI(TAG_AC_CTRL, "AC_OP_GET_POWER");
                status_payload = ac_server_get_current_power();
                status_opcode = AC_OP_POWER_STATUS;
                break;
            case AC_OP_SET_TEMPERATURE:
                if (param->model_operation.length == 1) {
                    value_received = param->model_operation.msg[0];
                    ESP_LOGI(TAG_AC_CTRL, "AC_OP_SET_TEMPERATURE: value %d", value_received);
                    err = ac_server_set_temperature(value_received);
                    status_payload = ac_server_get_current_temperature();
                    status_opcode = AC_OP_TEMPERATURE_STATUS;
                } else {
                    ESP_LOGE(TAG_AC_CTRL, "AC_OP_SET_TEMPERATURE: Invalid message length %d, expected 1", param->model_operation.length);
                    err = ESP_ERR_INVALID_ARG;
                }
                break;
            case AC_OP_GET_TEMPERATURE:
                ESP_LOGI(TAG_AC_CTRL, "AC_OP_GET_TEMPERATURE");
                status_payload = ac_server_get_current_temperature();
                status_opcode = AC_OP_TEMPERATURE_STATUS;
                break;
            case AC_OP_SET_MODE:
                if (param->model_operation.length == 1) {
                    value_received = param->model_operation.msg[0];
                    ESP_LOGI(TAG_AC_CTRL, "AC_OP_SET_MODE: value %d", value_received);
                    err = ac_server_set_mode(value_received);
                    status_payload = ac_server_get_current_mode();
                    status_opcode = AC_OP_MODE_STATUS;
                } else {
                    ESP_LOGE(TAG_AC_CTRL, "AC_OP_SET_MODE: Invalid message length %d, expected 1", param->model_operation.length);
                    err = ESP_ERR_INVALID_ARG;
                }
                break;
            case AC_OP_GET_MODE:
                ESP_LOGI(TAG_AC_CTRL, "AC_OP_GET_MODE");
                status_payload = ac_server_get_current_mode();
                status_opcode = AC_OP_MODE_STATUS;
                break;
            case AC_OP_SET_FAN_SPEED:
                if (param->model_operation.length == 1) {
                    value_received = param->model_operation.msg[0];
                    ESP_LOGI(TAG_AC_CTRL, "AC_OP_SET_FAN_SPEED: value %d", value_received);
                    err = ac_server_set_fan_speed(value_received);
                    status_payload = ac_server_get_current_fan_speed();
                    status_opcode = AC_OP_FAN_SPEED_STATUS;
                } else {
                    ESP_LOGE(TAG_AC_CTRL, "AC_OP_SET_FAN_SPEED: Invalid message length %d, expected 1", param->model_operation.length);
                    err = ESP_ERR_INVALID_ARG;
                }
                break;
            case AC_OP_GET_FAN_SPEED:
                ESP_LOGI(TAG_AC_CTRL, "AC_OP_GET_FAN_SPEED");
                status_payload = ac_server_get_current_fan_speed();
                status_opcode = AC_OP_FAN_SPEED_STATUS;
                break;
            case AC_OP_HEARTBEAT_ACK:
                ESP_LOGI(TAG_AC_CTRL, "AC_OP_HEARTBEAT_ACK received");
                ac_server_handle_heartbeat_ack();
                status_opcode = 0;
                break;
            default:
                ESP_LOGW(TAG_AC_CTRL, "Unknown opcode 0x%06" PRIx32, param->model_operation.opcode);
                break;
        }

        if (err == ESP_OK && status_opcode != 0) {
            ESP_LOGI(TAG_AC_CTRL, "Sending Status Opcode 0x%06" PRIx32 " with payload 0x%02x", status_opcode, status_payload);
            esp_err_t send_err = esp_ble_mesh_server_model_send_msg(&vnd_models[0],
                                                                    param->model_operation.ctx,
                                                                    status_opcode,
                                                                    sizeof(status_payload),
                                                                    &status_payload);
            if (send_err) {
                ESP_LOGE(TAG_AC_CTRL, "Failed to send status message 0x%06" PRIx32 " (err %d)", status_opcode, send_err);
            }
        } else if (err != ESP_OK && status_opcode != 0) {
             ESP_LOGE(TAG_AC_CTRL, "Error processing SET operation or invalid length (err %d for opcode 0x%06" PRIx32 "), not sending status.", err, param->model_operation.opcode);
        } else if (err != ESP_OK && status_opcode == 0) { 
            ESP_LOGE(TAG_AC_CTRL, "Error processing SET operation (err %d from ac_server_set_xxx), not sending status for opcode 0x%06" PRIx32 ".", err, param->model_operation.opcode);
        }
        break;
    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code) {
            ESP_LOGE(TAG_AC_CTRL, "Failed to send message 0x%06" PRIx32 " (err_code %d)", 
                     param->model_send_comp.opcode, param->model_send_comp.err_code);
        } else {
            ESP_LOGI(TAG_AC_CTRL, "Successfully sent message 0x%06" PRIx32, param->model_send_comp.opcode);
        }
        break;
    case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
        ESP_LOGW(TAG_AC_CTRL, "Client model send timeout for opcode 0x%06" PRIx32 ", src 0x%04x, dst 0x%04x",
                 param->client_send_timeout.opcode,
                 param->client_send_timeout.ctx->addr,
                 param->client_send_timeout.ctx->recv_dst);
        if (param->client_send_timeout.opcode == AC_OP_HEARTBEAT) {
            ESP_LOGW(TAG_AC_CTRL, "Heartbeat send timeout detected (should not happen for server sending to client)");
        }
        break;
    default:
        ESP_LOGW(TAG_AC_CTRL, "Unhandled model event: %d", event);
        break;
    }
}

/**
 * @brief Initializes the AC Server BLE Mesh Module.
 */
esp_err_t ac_server_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG_AC_CTRL, "Initializing AC BLE Mesh Server Module...");

    ble_mesh_get_dev_uuid(dev_uuid);

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_custom_model_callback(example_ble_mesh_custom_model_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AC_CTRL, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AC_CTRL, "Failed to enable mesh node (err %d)", err);
        return err;
    }

    board_led_operation(LED_G, LED_ON);

    ESP_LOGI(TAG_AC_CTRL, "AC BLE Mesh Server Module initialized, node enabled for provisioning.");

    return ESP_OK;
}

/**
 * @brief 设置空调电源
 */
esp_err_t ac_server_set_power(uint8_t power_state)
{
    if (power_state > AC_POWER_ON) {
        ESP_LOGE(TAG_AC_CTRL, "无效的电源状态值: %d", power_state);
        return ESP_ERR_INVALID_ARG;
    }
    ac_state.power = power_state;
    ESP_LOGI(TAG_AC_CTRL, "空调电源设置为: %s", power_state == AC_POWER_ON ? "开启" : "关闭");
    return ESP_OK;
}

/**
 * @brief 设置空调温度
 */
esp_err_t ac_server_set_temperature(uint8_t temperature)
{
    if (temperature < AC_TEMP_MIN || temperature > AC_TEMP_MAX) {
        ESP_LOGE(TAG_AC_CTRL, "无效的温度值: %d，有效范围: %d-%d", temperature, AC_TEMP_MIN, AC_TEMP_MAX);
        return ESP_ERR_INVALID_ARG;
    }
    ac_state.temperature = temperature;
    ESP_LOGI(TAG_AC_CTRL, "空调温度设置为: %d°C", temperature);
    return ESP_OK;
}

/**
 * @brief 设置空调模式
 */
esp_err_t ac_server_set_mode(uint8_t mode)
{
    if (mode > AC_MODE_AUTO) {
        ESP_LOGE(TAG_AC_CTRL, "无效的模式值: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
    ac_state.mode = mode;
    const char *mode_str;
    switch (mode) {
        case AC_MODE_COOL: mode_str = "制冷"; break;
        case AC_MODE_HEAT: mode_str = "制热"; break;
        case AC_MODE_FAN:  mode_str = "送风"; break;
        case AC_MODE_DRY:  mode_str = "除湿"; break;
        case AC_MODE_AUTO: mode_str = "自动"; break;
        default: mode_str = "未知"; break;
    }
    ESP_LOGI(TAG_AC_CTRL, "空调模式设置为: %s", mode_str);
    return ESP_OK;
}

/**
 * @brief 设置空调风速
 */
esp_err_t ac_server_set_fan_speed(uint8_t fan_speed)
{
    if (fan_speed > AC_FAN_SPEED_HIGH) {
        ESP_LOGE(TAG_AC_CTRL, "无效的风速值: %d", fan_speed);
        return ESP_ERR_INVALID_ARG;
    }
    ac_state.fan_speed = fan_speed;
    const char *speed_str;
    switch (fan_speed) {
        case AC_FAN_SPEED_AUTO:   speed_str = "自动"; break;
        case AC_FAN_SPEED_LOW:    speed_str = "低速"; break;
        case AC_FAN_SPEED_MEDIUM: speed_str = "中速"; break;
        case AC_FAN_SPEED_HIGH:   speed_str = "高速"; break;
        default: speed_str = "未知"; break;
    }
    ESP_LOGI(TAG_AC_CTRL, "空调风速设置为: %s", speed_str);
    return ESP_OK;
}

/**
 * @brief 设置所有空调参数
 */
esp_err_t ac_server_set_all(uint8_t power, uint8_t temperature, uint8_t mode, uint8_t fan_speed)
{
    esp_err_t err;
    err = ac_server_set_power(power);
    if (err != ESP_OK) return err;
    err = ac_server_set_temperature(temperature);
    if (err != ESP_OK) return err;
    err = ac_server_set_mode(mode);
    if (err != ESP_OK) return err;
    err = ac_server_set_fan_speed(fan_speed);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG_AC_CTRL, "空调所有参数设置完成");
    return ESP_OK;
}

/* Getter function implementations */

uint8_t ac_server_get_current_power(void)
{
    return ac_state.power;
}

uint8_t ac_server_get_current_temperature(void)
{
    return ac_state.temperature;
}

uint8_t ac_server_get_current_mode(void)
{
    return ac_state.mode;
}

uint8_t ac_server_get_current_fan_speed(void)
{
    return ac_state.fan_speed;
}

/* 心跳包定时器回调函数 */
static void heartbeat_timer_callback(void* arg)
{
    if (heartbeat_state.heartbeat_enabled && heartbeat_state.is_connected) {
        esp_err_t err = ac_server_send_heartbeat();
        if (err != ESP_OK) {
            ESP_LOGE(TAG_AC_CTRL, "Failed to send heartbeat packet (err %d)", err);
        }
    }
}

/**
 * @brief 启动心跳包机制
 */
esp_err_t ac_server_start_heartbeat(uint16_t client_addr)
{
    esp_err_t err;
    if (heartbeat_state.timer_handle != NULL) {
        ESP_LOGW(TAG_AC_CTRL, "Heartbeat timer already exists, stopping first");
        ac_server_stop_heartbeat();
    }
    heartbeat_state.client_addr = client_addr;
    heartbeat_state.timeout_count = 0;
    heartbeat_state.is_connected = true;
    heartbeat_state.heartbeat_enabled = true;
    const esp_timer_create_args_t timer_args = {
        .callback = &heartbeat_timer_callback,
        .name = "heartbeat_timer"
    };
    err = esp_timer_create(&timer_args, &heartbeat_state.timer_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AC_CTRL, "Failed to create heartbeat timer (err %d)", err);
        return err;
    }
    err = esp_timer_start_periodic(heartbeat_state.timer_handle, HEARTBEAT_INTERVAL_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AC_CTRL, "Failed to start heartbeat timer (err %d)", err);
        esp_timer_delete(heartbeat_state.timer_handle);
        heartbeat_state.timer_handle = NULL;
        return err;
    }
    ESP_LOGI(TAG_AC_CTRL, "Heartbeat started for client 0x%04x", client_addr);
    return ESP_OK;
}

/**
 * @brief 停止心跳包机制
 */
esp_err_t ac_server_stop_heartbeat(void)
{
    if (heartbeat_state.timer_handle != NULL) {
        esp_timer_stop(heartbeat_state.timer_handle);
        esp_timer_delete(heartbeat_state.timer_handle);
        heartbeat_state.timer_handle = NULL;
    }
    heartbeat_state.heartbeat_enabled = false;
    heartbeat_state.is_connected = false;
    heartbeat_state.timeout_count = 0;
    ESP_LOGI(TAG_AC_CTRL, "Heartbeat stopped");
    return ESP_OK;
}

/**
 * @brief 发送心跳包
 */
esp_err_t ac_server_send_heartbeat(void)
{
    if (!heartbeat_state.heartbeat_enabled || heartbeat_state.client_addr == ESP_BLE_MESH_ADDR_UNASSIGNED || heartbeat_state.client_addr == 0) {
        ESP_LOGW(TAG_AC_CTRL, "Heartbeat send skipped: not enabled or invalid client address (0x%04x)", heartbeat_state.client_addr);
        return ESP_ERR_INVALID_STATE;
    }
    esp_ble_mesh_msg_ctx_t ctx = {0};
    ctx.net_idx = 0;
    ctx.app_idx = 0;
    ctx.addr = heartbeat_state.client_addr;
    ctx.send_ttl = 7;
    esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0], &ctx, AC_OP_HEARTBEAT, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AC_CTRL, "Failed to send heartbeat to 0x%04x (err %d)", heartbeat_state.client_addr, err);
    } else {
        ESP_LOGD(TAG_AC_CTRL, "Heartbeat sent to client 0x%04x", heartbeat_state.client_addr);
    }
    return err;
}

/**
 * @brief 处理心跳包超时 (Server perspective: ACK not received in time)
 */
void ac_server_handle_heartbeat_timeout(void)
{
    if (!heartbeat_state.heartbeat_enabled || !heartbeat_state.is_connected) return;

    heartbeat_state.timeout_count++;
    ESP_LOGW(TAG_AC_CTRL, "Heartbeat ACK not received from client 0x%04x, timeout #%d", 
             heartbeat_state.client_addr, heartbeat_state.timeout_count);
    
    if (heartbeat_state.timeout_count >= MAX_HEARTBEAT_TIMEOUTS) {
        ESP_LOGE(TAG_AC_CTRL, "Client 0x%04x disconnected (failed to ACK %d heartbeats). Stopping heartbeat.", 
                 heartbeat_state.client_addr, MAX_HEARTBEAT_TIMEOUTS);
        
        ac_server_stop_heartbeat();
        ESP_LOGI(TAG_AC_CTRL, "Device may need to be re-provisioned if connection lost.");
    } else {
    }
}

/**
 * @brief 处理收到的心跳包ACK
 */
void ac_server_handle_heartbeat_ack(void)
{
    if (heartbeat_state.heartbeat_enabled && heartbeat_state.is_connected) {
        heartbeat_state.timeout_count = 0;
        ESP_LOGI(TAG_AC_CTRL, "Heartbeat ACK received from client 0x%04x. Connection healthy.", heartbeat_state.client_addr);
    } else {
        ESP_LOGW(TAG_AC_CTRL, "Heartbeat ACK received but heartbeat not active/connected for client 0x%04x", heartbeat_state.client_addr);
    }
}
