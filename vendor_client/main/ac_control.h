/* ac_control.h - Air Conditioner Bluetooth Mesh Client Control Interface */

#ifndef _AC_CONTROL_H_
#define _AC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_ble_mesh_defs.h" // Added for BLE types

/* ==================== 消息队列相关定义 ==================== */
#define AC_MSG_QUEUE_SIZE 16  /* 消息队列最大长度 */

/* BLE Mesh消息类型 */
typedef enum {
    AC_MSG_TYPE_SET_POWER,
    AC_MSG_TYPE_GET_POWER,
    AC_MSG_TYPE_SET_TEMPERATURE,
    AC_MSG_TYPE_GET_TEMPERATURE,
    AC_MSG_TYPE_SET_MODE,
    AC_MSG_TYPE_GET_MODE,
    AC_MSG_TYPE_SET_FAN_SPEED,
    AC_MSG_TYPE_GET_FAN_SPEED,
    AC_MSG_TYPE_HEARTBEAT_ACK,  /* 新增：心跳ACK类型 */
} ac_msg_type_t;

/* 消息队列项结构 */
typedef struct {
    ac_msg_type_t msg_type;     /* 消息类型 */
    uint16_t server_addr;       /* 目标服务器地址 */
    uint8_t value;              /* 消息值（对于GET类型消息无效） */
    uint32_t timestamp;         /* 消息时间戳 */
} ac_msg_queue_item_t;

/* 消息发送状态 */
typedef enum {
    AC_SEND_STATE_IDLE,         /* 空闲状态，可以发送消息 */
    AC_SEND_STATE_SENDING,      /* 正在发送消息 */
    AC_SEND_STATE_WAITING_ACK,  /* 等待响应 */
} ac_send_state_t;

/**
 * @brief 获取当前消息发送状态
 * 
 * @return ac_send_state_t 当前发送状态
 */
ac_send_state_t ac_get_send_state(void);

/**
 * @brief 获取消息队列中等待发送的消息数量
 * 
 * @return uint8_t 队列中的消息数量
 */
uint8_t ac_get_queue_size(void);

/**
 * @brief 清空消息队列
 */
void ac_clear_msg_queue(void);

/* AC状态类型枚举 */
typedef enum {
    AC_STATUS_POWER = 0,        /* 电源状态 */
    AC_STATUS_TEMPERATURE = 1,  /* 温度状态 */
    AC_STATUS_MODE = 2,         /* 模式状态 */
    AC_STATUS_FAN_SPEED = 3,    /* 风速状态 */
} ac_status_type_t;

/* 设备信息结构体，用于UI显示 */
typedef struct {
    uint16_t addr;              /* 设备地址 */
    bool is_online;             /* 在线状态 */
    bool is_configured;         /* 配置完成状态 */
    uint8_t power_state;        /* 电源状态 (0:关, 1:开) */
    uint8_t temperature;        /* 设定温度 */
    uint8_t mode;               /* 运行模式 */
    uint8_t fan_speed;          /* 风速 */
    char device_name[16];       /* 设备名称 */
    uint32_t last_update_time;  /* 最后更新时间戳 */
} ac_device_info_t;

/* 设备状态变化回调函数类型 */
typedef void (*ac_device_status_callback_t)(uint16_t device_addr, ac_status_type_t status_type, uint8_t value);

/* 设备上线/下线状态变化回调函数类型 */
typedef void (*ac_device_online_callback_t)(uint16_t device_addr, bool is_online);

/* 新设备被发现和配网完成的回调函数类型 */
typedef void (*ac_device_provisioned_callback_t)(uint16_t device_addr);

/**
 * @brief Initialize the AC client control interface and BLE stack
 * 
 * @return ESP_OK on success
 */
esp_err_t ac_client_init(void);

/**
 * @brief 注册设备状态变化回调函数
 * 
 * @param status_cb 状态变化回调函数
 * @param online_cb 设备上线/下线回调函数
 * @param provisioned_cb 新设备配网完成回调函数
 */
void ac_client_register_callbacks(ac_device_status_callback_t status_cb, 
                                 ac_device_online_callback_t online_cb,
                                 ac_device_provisioned_callback_t provisioned_cb);

/**
 * @brief 获取当前管理的设备数量
 * 
 * @return uint8_t 设备数量
 */
uint8_t ac_get_device_count(void);

/**
 * @brief 获取在线设备数量
 * 
 * @return uint8_t 在线设备数量
 */
uint8_t ac_get_online_device_count(void);

/**
 * @brief 获取设备列表信息（用于UI显示）
 * 
 * @param device_list 设备信息数组指针
 * @param max_devices 最大设备数量
 * @return uint8_t 实际返回的设备数量
 */
uint8_t ac_get_device_list(ac_device_info_t *device_list, uint8_t max_devices);

/**
 * @brief 根据索引获取设备信息
 * 
 * @param index 设备索引
 * @param device_info 设备信息结构体指针
 * @return esp_err_t ESP_OK成功，ESP_FAIL失败
 */
esp_err_t ac_get_device_info_by_index(uint8_t index, ac_device_info_t *device_info);

/**
 * @brief 根据设备地址获取设备信息
 * 
 * @param device_addr 设备地址
 * @param device_info 设备信息结构体指针
 * @return esp_err_t ESP_OK成功，ESP_FAIL失败
 */
esp_err_t ac_get_device_info_by_addr(uint16_t device_addr, ac_device_info_t *device_info);

/**
 * @brief 发送控制指令到指定设备（根据索引）
 * 
 * @param device_index 设备索引
 * @param command_type 控制类型 (AC_STATUS_POWER, AC_STATUS_TEMPERATURE等)
 * @param value 控制值
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_send_command_by_index(uint8_t device_index, ac_status_type_t command_type, uint8_t value);

/**
 * @brief 发送控制指令到指定设备（根据地址）
 * 
 * @param device_addr 设备地址
 * @param command_type 控制类型
 * @param value 控制值
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_send_command_by_addr(uint16_t device_addr, ac_status_type_t command_type, uint8_t value);

/**
 * @brief 获取指定设备的状态（根据索引）
 * 
 * @param device_index 设备索引
 * @param status_type 状态类型
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_get_status_by_index(uint8_t device_index, ac_status_type_t status_type);

/**
 * @brief 获取指定设备的状态（根据地址）
 * 
 * @param device_addr 设备地址
 * @param status_type 状态类型
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_get_status_by_addr(uint16_t device_addr, ac_status_type_t status_type);

/**
 * @brief 向所有在线设备发送控制指令
 * 
 * @param command_type 控制类型
 * @param value 控制值
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_send_command_to_all_online(ac_status_type_t command_type, uint8_t value);

/**
 * @brief 刷新所有设备状态（发送获取状态指令）
 * 
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_refresh_all_device_status(void);

/**
 * @brief 设置设备名称
 * 
 * @param device_addr 设备地址
 * @param name 设备名称
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_set_device_name(uint16_t device_addr, const char *name);

/**
 * @brief Send power control message to server
 * 
 * @param server_addr Target server unicast address
 * @param power_state 0: OFF, 1: ON
 * @return ESP_OK on success
 */
esp_err_t ac_client_set_power(uint16_t server_addr, uint8_t power_state);

/**
 * @brief Get power status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
esp_err_t ac_client_get_power(uint16_t server_addr);

/**
 * @brief Send temperature control message to server
 * 
 * @param server_addr Server address
 * @param temperature Temperature value (16-30°C)
 * @return ESP_OK on success
 */
esp_err_t ac_client_set_temperature(uint16_t server_addr, uint8_t temperature);

/**
 * @brief Get temperature status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
esp_err_t ac_client_get_temperature(uint16_t server_addr);

/**
 * @brief Send mode control message to server
 * 
 * @param server_addr Server address
 * @param mode Mode value (0: Cool, 1: Heat, 2: Fan, 3: Dry, 4: Auto)
 * @return ESP_OK on success
 */
esp_err_t ac_client_set_mode(uint16_t server_addr, uint8_t mode);

/**
 * @brief Get mode status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
esp_err_t ac_client_get_mode(uint16_t server_addr);

/**
 * @brief Send fan speed control message to server
 * 
 * @param server_addr Server address
 * @param fan_speed Fan speed value (0: Auto, 1: Low, 2: Medium, 3: High)
 * @return ESP_OK on success
 */
esp_err_t ac_client_set_fan_speed(uint16_t server_addr, uint8_t fan_speed);

/**
 * @brief Get fan speed status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
esp_err_t ac_client_get_fan_speed(uint16_t server_addr);

/**
 * @brief Stores relevant BLE mesh information (including server list) to NVS.
 */
void ac_ble_mesh_store_info(void);

/**
 * @brief Restores relevant BLE mesh information (including server list) from NVS.
 */
void ac_ble_mesh_restore_info(void);

/**
 * @brief Adds a new AC server's unicast address to the list of managed devices if not already present and space is available.
 *        Typically called after a new device is provisioned.
 *
 * @param addr Server unicast address to add.
 */
void ac_add_server_addr(uint16_t addr);

/**
 * @brief Gets the number of currently managed AC servers.
 *
 * @return uint8_t Number of servers.
 */
uint8_t ac_get_num_servers(void);

/**
 * @brief Gets the unicast address of a managed AC server by its index in the list.
 *
 * @param index The index of the server in the list (0 to ac_get_num_servers() - 1).
 * @return uint16_t Server unicast address or ESP_BLE_MESH_ADDR_UNASSIGNED if index is out of bounds.
 */
uint16_t ac_get_server_addr_by_index(uint8_t index);

/**
 * @brief Checks if a specific AC server is currently considered online.
 *
 * @param server_addr The unicast address of the server to check.
 * @return true if the server is considered online, false otherwise (including if server not found).
 */
bool ac_is_server_online(uint16_t server_addr);

/* ==================== 使用示例 ==================== */
/*
 * UI层使用示例：
 *
 * 1. 初始化和注册回调：
 *    void ui_init(void) {
 *        ac_client_init();
 *        ac_client_register_callbacks(ui_device_status_callback, 
 *                                     ui_device_online_callback,
 *                                     ui_device_provisioned_callback);
 *    }
 *
 * 2. 获取设备列表用于页面显示：
 *    void ui_update_device_list(void) {
 *        ac_device_info_t devices[MAX_AC_SERVERS];
 *        uint8_t count = ac_get_device_list(devices, MAX_AC_SERVERS);
 *        
 *        for (uint8_t i = 0; i < count; i++) {
 *            // 显示设备信息：地址、名称、在线状态、各项设置等
 *            printf("Device %d: %s (0x%04x) - %s\n", 
 *                   i, devices[i].device_name, devices[i].addr,
 *                   devices[i].is_online ? "ONLINE" : "OFFLINE");
 *            printf("  Power: %s, Temp: %d°C, Mode: %d, Fan: %d\n",
 *                   devices[i].power_state ? "ON" : "OFF",
 *                   devices[i].temperature, devices[i].mode, devices[i].fan_speed);
 *        }
 *    }
 *
 * 3. 控制特定设备：
 *    void ui_control_device(uint8_t device_index, uint8_t power_state) {
 *        esp_err_t err = ac_send_command_by_index(device_index, AC_STATUS_POWER, power_state);
 *        if (err != ESP_OK) {
 *            printf("Failed to control device %d\n", device_index);
 *        }
 *    }
 *
 * 4. 控制所有设备：
 *    void ui_turn_off_all_devices(void) {
 *        ac_send_command_to_all_online(AC_STATUS_POWER, AC_POWER_OFF);
 *    }
 *
 * 5. 刷新设备状态：
 *    void ui_refresh_status(void) {
 *        ac_refresh_all_device_status();
 *        // 状态更新会通过回调函数异步返回
 *    }
 *
 * 6. 状态变化回调实现：
 *    void ui_device_status_callback(uint16_t device_addr, ac_status_type_t status_type, uint8_t value) {
 *        printf("Device 0x%04x status updated: type %d = %d\n", device_addr, status_type, value);
 *        // 更新UI显示
 *    }
 *
 *    void ui_device_online_callback(uint16_t device_addr, bool is_online) {
 *        printf("Device 0x%04x is now %s\n", device_addr, is_online ? "ONLINE" : "OFFLINE");
 *        // 更新设备列表显示
 *    }
 *
 *    void ui_device_provisioned_callback(uint16_t device_addr) {
 *        printf("New device 0x%04x provisioned!\n", device_addr);
 *        // 刷新设备列表
 *    }
 */

#ifdef __cplusplus
}
#endif

#endif /* _AC_CONTROL_H_ */ 