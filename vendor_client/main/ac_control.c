/* ac_control.c - Air Conditioner Bluetooth Mesh Client Control Implementation */

#include "ac_control.h"
#include "esp_log.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "board.h"
#include <string.h>
#include <inttypes.h>

#include "mesh_common.h"

#define TAG "AC_CLIENT"

//TODO:不用添加心跳包，为了省电。判断设备是否离线可以通过发送失败次数，如果一定时间内多次发送失败，则判定为设备离线 ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT

extern struct esp_ble_mesh_key prov_key;

/* 全局变量 */
static uint8_t dev_uuid[16] = {0xdd, 0xdd};
static uint16_t client_primary_addr;

/* AC状态回调函数 */
static ac_status_callback_t ac_status_cb = NULL;

/* 定义模型操作项 */
// static esp_ble_mesh_model_op_t ac_client_op[] = {
esp_ble_mesh_model_op_t ac_client_op[] = {
    /* 状态消息响应处理器 */
    ESP_BLE_MESH_MODEL_OP(AC_OP_POWER_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_TEMPERATURE_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_MODE_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP(AC_OP_FAN_SPEED_STATUS, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

/* Vendor客户端模型ID */
// static esp_ble_mesh_client_op_pair_t ac_client_op_pair[] = {
esp_ble_mesh_client_op_pair_t ac_client_op_pair[] = {
    {AC_OP_SET_POWER, AC_OP_POWER_STATUS},
    {AC_OP_GET_POWER, AC_OP_POWER_STATUS},
    {AC_OP_SET_TEMPERATURE, AC_OP_TEMPERATURE_STATUS},
    {AC_OP_GET_TEMPERATURE, AC_OP_TEMPERATURE_STATUS},
    {AC_OP_SET_MODE, AC_OP_MODE_STATUS},
    {AC_OP_GET_MODE, AC_OP_MODE_STATUS},
    {AC_OP_SET_FAN_SPEED, AC_OP_FAN_SPEED_STATUS},
    {AC_OP_GET_FAN_SPEED, AC_OP_FAN_SPEED_STATUS},
};

// static esp_ble_mesh_client_t ac_client = {
esp_ble_mesh_client_t ac_client = {
    .op_pair_size = ARRAY_SIZE(ac_client_op_pair),
    .op_pair = ac_client_op_pair,
};

/* 模型发送消息的通用参数 */
static void set_msg_common(esp_ble_mesh_client_common_param_t *common, 
    uint16_t server_addr, uint32_t opcode)
{
common->opcode = opcode;
common->model = ac_client.model;
common->ctx.net_idx = prov_key.net_idx;  /* 使用存储的网络索引 */
common->ctx.app_idx = prov_key.app_idx;  /* 使用存储的应用索引 */
common->ctx.addr = server_addr;
common->ctx.send_ttl = 7; /* 使用最大TTL值 */
common->msg_timeout = 10000;  /* 增加超时时间到10秒，确保消息有充分时间送达 */
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
common->msg_role = ROLE_PROVISIONER;
#endif
}

/* 处理状态消息 */
static void handle_status_message(uint32_t opcode, const uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 1) {
        return;
    }

    uint8_t type = 0;
    uint8_t value = data[0];

    switch (opcode) {
        case AC_OP_POWER_STATUS:
            type = AC_STATUS_POWER; /* 电源状态 */
            ESP_LOGI(TAG, "Received power status: %d", value);
            break;
        case AC_OP_TEMPERATURE_STATUS:
            type = AC_STATUS_TEMPERATURE; /* 温度状态 */
            ESP_LOGI(TAG, "Received temperature status: %d", value);
            break;
        case AC_OP_MODE_STATUS:
            type = AC_STATUS_MODE; /* 模式状态 */
            ESP_LOGI(TAG, "Received mode status: %d", value);
            break;
        case AC_OP_FAN_SPEED_STATUS:
            type = AC_STATUS_FAN_SPEED; /* 风速状态 */
            ESP_LOGI(TAG, "Received fan speed status: %d", value);
            break;
        default:
            return;
    }
    
    if (ac_status_cb != NULL) {
        /* 调用回调函数 */
        ac_status_cb(type, value);
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
                                  param->model_operation.length);
            // 成功接收发布消息，重置失败计数
            // send_fail_count = 0;
            break;
        case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
            if (param->model_send_comp.err_code) {
                ESP_LOGW(TAG, "发送失败，错误码 0x%04x，操作码 0x%06" PRIx32, 
                         param->model_send_comp.err_code, param->model_send_comp.opcode);
            } else {
                ESP_LOGI(TAG, "发送完成，操作码 0x%06" PRIx32, 
                         param->model_send_comp.opcode);
                // 消息发送成功但尚未收到响应，不重置失败计数
            }
            break;
        case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
            ESP_LOGI(TAG, "接收到发布消息：操作码 0x%06" PRIx32 ", 来自节点 0x%04x", 
                     param->client_recv_publish_msg.opcode, param->client_recv_publish_msg.ctx->addr);
            handle_status_message(param->client_recv_publish_msg.opcode,
                                  param->client_recv_publish_msg.msg,
                                  param->client_recv_publish_msg.length);
            // 成功接收发布消息，重置失败计数
            // send_fail_count = 0;
            break;
        case ESP_BLE_MESH_CLIENT_MODEL_SEND_TIMEOUT_EVT:
            ESP_LOGW(TAG, "客户端消息超时，操作码 0x%06" PRIx32 ", 目标节点 0x%04x", 
                     param->client_send_timeout.opcode, param->client_send_timeout.ctx->addr);
            break;
        default:
            break;
    }
}

/* AC客户端初始化 */
esp_err_t ac_client_init(void)
{
    esp_err_t err = ESP_OK;

    /* 注册模型回调 */
    err = esp_ble_mesh_register_custom_model_callback(ac_client_model_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register custom model callback");
        return err;
    }

    ESP_LOGI(TAG, "AC client initialized");
    return ESP_OK;
}

/* 设置电源状态 */
esp_err_t ac_client_set_power(uint16_t server_addr, uint8_t power_state)
{
    esp_ble_mesh_client_common_param_t common = {0};
    esp_ble_mesh_msg_ctx_t ctx = {0};
    uint8_t msg = (power_state <= AC_POWER_ON) ? power_state : AC_POWER_OFF;
    esp_err_t err = ESP_OK;

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_SET_POWER);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, false, 
                                           ROLE_PROVISIONER);
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

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_GET_POWER);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送前额外日志 */
    ESP_LOGI(TAG, "准备发送电源状态查询，server: 0x%04x, net_idx: 0x%04x, app_idx: 0x%04x", 
             server_addr, ctx.net_idx, ctx.app_idx);

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           ROLE_PROVISIONER);
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

    /* 检查温度范围 */
    if (temperature < AC_TEMP_MIN) {
        temperature = AC_TEMP_MIN;
    } else if (temperature > AC_TEMP_MAX) {
        temperature = AC_TEMP_MAX;
    }
    msg = temperature;

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_SET_TEMPERATURE);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, true, 
                                           ROLE_PROVISIONER);
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

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_GET_TEMPERATURE);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           ROLE_PROVISIONER);
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

    /* 检查模式值 */
    if (mode > AC_MODE_AUTO) {
        mode = AC_MODE_AUTO;
    }
    msg = mode;

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_SET_MODE);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, true, 
                                           ROLE_PROVISIONER);
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

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_GET_MODE);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           ROLE_PROVISIONER);
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

    /* 检查风速值 */
    if (fan_speed > AC_FAN_SPEED_HIGH) {
        fan_speed = AC_FAN_SPEED_AUTO;
    }
    msg = fan_speed;

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_SET_FAN_SPEED);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           1, (uint8_t *)&msg, common.msg_timeout, true, 
                                           ROLE_PROVISIONER);
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

    /* 设置公共参数 */
    set_msg_common(&common, server_addr, AC_OP_GET_FAN_SPEED);
    
    /* 设置消息上下文 */
    ctx.net_idx = common.ctx.net_idx;
    ctx.app_idx = common.ctx.app_idx;
    ctx.addr = common.ctx.addr;
    ctx.send_ttl = common.ctx.send_ttl;

    /* 发送消息 */
    err = esp_ble_mesh_client_model_send_msg(ac_client.model, &ctx, common.opcode,
                                           0, NULL, common.msg_timeout, true, 
                                           ROLE_PROVISIONER);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send fan speed get message");
        return err;
    }

    ESP_LOGI(TAG, "Send get fan speed status request");
    return ESP_OK;
} 