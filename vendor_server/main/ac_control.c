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
#include "esp_timer.h"

#define TAG "AC_SERVER"

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

    /* Heartbeat operations */
    ESP_BLE_MESH_MODEL_OP(AC_OP_HEARTBEAT_ACK, 0),   /* No payload for heartbeat ACK */

    ESP_BLE_MESH_MODEL_OP_END,
};

/* 外部模型引用 */
extern esp_ble_mesh_model_t vnd_models[];

/**
 * @brief 初始化空调控制接口
 */
esp_err_t ac_server_init(void)
{
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

/* 心跳包定时器回调函数 */
static void heartbeat_timer_callback(void* arg)
{
    if (heartbeat_state.heartbeat_enabled && heartbeat_state.is_connected) {
        esp_err_t err = ac_server_send_heartbeat();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send heartbeat packet (err %d)", err);
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
        ESP_LOGW(TAG, "Heartbeat timer already exists, stopping first");
        ac_server_stop_heartbeat();
    }
    
    heartbeat_state.client_addr = client_addr;
    heartbeat_state.timeout_count = 0;
    heartbeat_state.is_connected = true;
    heartbeat_state.heartbeat_enabled = true;
    
    /* 创建定时器 */
    const esp_timer_create_args_t timer_args = {
        .callback = &heartbeat_timer_callback,
        .arg = NULL,
        .name = "heartbeat_timer"
    };
    
    err = esp_timer_create(&timer_args, &heartbeat_state.timer_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create heartbeat timer (err %d)", err);
        return err;
    }
    
    /* 启动定时器 */
    err = esp_timer_start_periodic(heartbeat_state.timer_handle, HEARTBEAT_INTERVAL_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start heartbeat timer (err %d)", err);
        esp_timer_delete(heartbeat_state.timer_handle);
        heartbeat_state.timer_handle = NULL;
        return err;
    }
    
    ESP_LOGI(TAG, "Heartbeat started for client 0x%04x", client_addr);
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
    
    ESP_LOGI(TAG, "Heartbeat stopped");
    return ESP_OK;
}

/**
 * @brief 发送心跳包
 */
esp_err_t ac_server_send_heartbeat(void)
{
    if (!heartbeat_state.heartbeat_enabled || heartbeat_state.client_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_ble_mesh_msg_ctx_t ctx = {0};
    ctx.net_idx = 0;  /* 使用主网络密钥 */
    ctx.app_idx = 0;  /* 使用主应用密钥 */
    ctx.addr = heartbeat_state.client_addr;
    ctx.send_ttl = 7;
    
    /* 发送心跳包，使用正确的模型指针 */
    esp_err_t err = esp_ble_mesh_server_model_send_msg(&vnd_models[0], &ctx, AC_OP_HEARTBEAT, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send heartbeat to 0x%04x (err %d)", heartbeat_state.client_addr, err);
        return err;
    }
    
    ESP_LOGD(TAG, "Heartbeat sent to client 0x%04x", heartbeat_state.client_addr);
    return ESP_OK;
}

/**
 * @brief 处理心跳包超时
 */
void ac_server_handle_heartbeat_timeout(void)
{
    heartbeat_state.timeout_count++;
    ESP_LOGW(TAG, "Heartbeat timeout #%d for client 0x%04x", 
             heartbeat_state.timeout_count, heartbeat_state.client_addr);
    
    if (heartbeat_state.timeout_count >= MAX_HEARTBEAT_TIMEOUTS) {
        ESP_LOGE(TAG, "Client 0x%04x disconnected after %d timeouts, entering re-provisioning mode", 
                 heartbeat_state.client_addr, MAX_HEARTBEAT_TIMEOUTS);
        
        /* 停止心跳包 */
        ac_server_stop_heartbeat();
        
        /* 进入重新配网状态 */
        esp_err_t err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to re-enable provisioning (err %d)", err);
        } else {
            ESP_LOGI(TAG, "Re-provisioning mode enabled");
            /* 可以在这里添加LED指示或其他状态指示 */
            board_led_operation(LED_G, LED_ON);  /* 绿灯常亮表示等待配网 */
        }
    }
}

/**
 * @brief 处理收到的心跳包ACK
 */
void ac_server_handle_heartbeat_ack(void)
{
    if (heartbeat_state.heartbeat_enabled) {
        heartbeat_state.timeout_count = 0;  /* 重置超时计数 */
        ESP_LOGD(TAG, "Heartbeat ACK received from client 0x%04x", heartbeat_state.client_addr);
    }
}
