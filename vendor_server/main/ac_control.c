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

#define TAG "AC_SERVER"

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

// extern struct esp_ble_mesh_key prov_key;

/* 全局变量 */
// static uint8_t dev_uuid[16] = {0xdd, 0xdd};

/* AC状态回调函数 */
// static ac_status_callback_t ac_status_cb = NULL;

/* 定义模型操作项 */
/* AC Server Model Operations */
// static esp_ble_mesh_model_op_t ac_server_op[] = {
esp_ble_mesh_model_op_t ac_server_op[] = {
    /* Power operations */
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_POWER, 1),       /* Payload: 1 byte for power state */
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_POWER, 0),       /* No payload */
    
    /* Temperature operations */
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_TEMPERATURE, 1), /* Payload: 1 byte for temperature */
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_TEMPERATURE, 0), /* No payload */
    
    /* Mode operations */
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_MODE, 1),        /* Payload: 1 byte for mode */
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_MODE, 0),        /* No payload */
    
    /* Fan speed operations */
    ESP_BLE_MESH_MODEL_OP(AC_OP_SET_FAN_SPEED, 1),   /* Payload: 1 byte for fan speed */
    ESP_BLE_MESH_MODEL_OP(AC_OP_GET_FAN_SPEED, 0),   /* No payload */

    ESP_BLE_MESH_MODEL_OP_END,
};

/**
 * @brief 初始化空调控制接口
 */
esp_err_t ac_server_init(void)
{
    esp_err_t err;
    
    ESP_LOGI(TAG, "空调控制初始化成功");
    return ESP_OK;
}

/**
 * @brief 设置空调电源
 */
esp_err_t ac_server_set_power(uint8_t power_state)
{
    // 检查参数有效性
    if (power_state > AC_POWER_ON) {
        ESP_LOGE(TAG, "无效的电源状态值: %d", power_state);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 更新空调状态
    ac_state.power = power_state;
    
    // 模拟空调操作（日志输出）
    if (power_state == AC_POWER_ON) {
        ESP_LOGI(TAG, "空调已开启");
    } else {
        ESP_LOGI(TAG, "空调已关闭");
    }
    
    return ESP_OK;
}

/**
 * @brief 设置空调温度
 */
esp_err_t ac_server_set_temperature(uint8_t temperature)
{
    // 检查参数有效性
    if (temperature < AC_TEMP_MIN || temperature > AC_TEMP_MAX) {
        ESP_LOGE(TAG, "无效的温度值: %d，有效范围: %d-%d", temperature, AC_TEMP_MIN, AC_TEMP_MAX);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 更新空调状态
    ac_state.temperature = temperature;
    
    // 模拟空调操作（日志输出）
    ESP_LOGI(TAG, "空调温度设置为: %d°C", temperature);
    
    return ESP_OK;
}

/**
 * @brief 设置空调模式
 */
esp_err_t ac_server_set_mode(uint8_t mode)
{
    // 检查参数有效性
    if (mode > AC_MODE_AUTO) {
        ESP_LOGE(TAG, "无效的模式值: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 更新空调状态
    ac_state.mode = mode;
    
    // 模拟空调操作（日志输出）
    const char *mode_str;
    switch (mode) {
        case AC_MODE_COOL: mode_str = "制冷"; break;
        case AC_MODE_HEAT: mode_str = "制热"; break;
        case AC_MODE_FAN:  mode_str = "送风"; break;
        case AC_MODE_DRY:  mode_str = "除湿"; break;
        case AC_MODE_AUTO: mode_str = "自动"; break;
        default: mode_str = "未知"; break;
    }
    
    ESP_LOGI(TAG, "空调模式设置为: %s", mode_str);
    
    return ESP_OK;
}

/**
 * @brief 设置空调风速
 */
esp_err_t ac_server_set_fan_speed(uint8_t fan_speed)
{
    // 检查参数有效性
    if (fan_speed > AC_FAN_SPEED_HIGH) {
        ESP_LOGE(TAG, "无效的风速值: %d", fan_speed);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 更新空调状态
    ac_state.fan_speed = fan_speed;
    
    // 模拟空调操作（日志输出）
    const char *speed_str;
    switch (fan_speed) {
        case AC_FAN_SPEED_AUTO:   speed_str = "自动"; break;
        case AC_FAN_SPEED_LOW:    speed_str = "低速"; break;
        case AC_FAN_SPEED_MEDIUM: speed_str = "中速"; break;
        case AC_FAN_SPEED_HIGH:   speed_str = "高速"; break;
        default: speed_str = "未知"; break;
    }
    
    ESP_LOGI(TAG, "空调风速设置为: %s", speed_str);
    
    return ESP_OK;
}

/**
 * @brief 设置所有空调参数
 */
esp_err_t ac_server_set_all(uint8_t power, uint8_t temperature, uint8_t mode, uint8_t fan_speed)
{
    esp_err_t err;
    
    // 设置电源
    err = ac_server_set_power(power);
    if (err != ESP_OK) {
        return err;
    }
    
    // 设置温度
    err = ac_server_set_temperature(temperature);
    if (err != ESP_OK) {
        return err;
    }
    
    // 设置模式
    err = ac_server_set_mode(mode);
    if (err != ESP_OK) {
        return err;
    }
    
    // 设置风速
    err = ac_server_set_fan_speed(fan_speed);
    if (err != ESP_OK) {
        return err;
    }
    
    ESP_LOGI(TAG, "空调所有参数设置完成");
    
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
