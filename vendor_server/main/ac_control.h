/* ac_control.h - Air Conditioner Bluetooth Mesh server Control Interface */

#ifndef _AC_CONTROL_H_
#define _AC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>

/* AC状态类型枚举 */
typedef enum {
    AC_STATUS_POWER = 0,        /* 电源状态 */
    AC_STATUS_TEMPERATURE = 1,  /* 温度状态 */
    AC_STATUS_MODE = 2,         /* 模式状态 */
    AC_STATUS_FAN_SPEED = 3,    /* 风速状态 */
} ac_status_type_t;

struct esp_ble_mesh_key {
    uint16_t net_idx;
    uint16_t app_idx;
    // uint8_t  app_key[ESP_BLE_MESH_OCTET16_LEN];
    uint8_t  app_key[16]; // 使用16字节的app_key
};

/**
 * @brief Initialize the AC server control interface
 * 
 * @return ESP_OK on success
 */
esp_err_t ac_server_init(void);

/**
 * @brief Send power control message to server
 * 
 * @param server_addr Server address
 * @param power_state 0: OFF, 1: ON
 * @return ESP_OK on success
 */
esp_err_t ac_server_set_power(uint8_t power_state);

/**
 * @brief Get power status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
// esp_err_t ac_server_get_power(uint16_t server_addr);

/**
 * @brief Send temperature control message to server
 * 
 * @param server_addr Server address
 * @param temperature Temperature value (16-30°C)
 * @return ESP_OK on success
 */
esp_err_t ac_server_set_temperature(uint8_t temperature);

/**
 * @brief Get temperature status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
// esp_err_t ac_server_get_temperature(uint16_t server_addr);

/**
 * @brief Send mode control message to server
 * 
 * @param server_addr Server address
 * @param mode Mode value (0: Cool, 1: Heat, 2: Fan, 3: Dry, 4: Auto)
 * @return ESP_OK on success
 */
esp_err_t ac_server_set_mode(uint8_t mode);

/**
 * @brief Get mode status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
// esp_err_t ac_server_get_mode(uint16_t server_addr);

/**
 * @brief Send fan speed control message to server
 * 
 * @param server_addr Server address
 * @param fan_speed Fan speed value (0: Auto, 1: Low, 2: Medium, 3: High)
 * @return ESP_OK on success
 */
esp_err_t ac_server_set_fan_speed(uint8_t fan_speed);

/**
 * @brief Get fan speed status from server
 * 
 * @param server_addr Server address
 * @return ESP_OK on success
 */
// esp_err_t ac_server_get_fan_speed(uint16_t server_addr);

/**
 * @brief AC status callback function type
 */
typedef void (*ac_status_callback_t)(uint8_t value);

/**
 * @brief Send all AC settings at once via Bluetooth
 * 
 * @param power Power state
 * @param temperature Temperature value
 * @param mode Mode value
 * @param fan_speed Fan speed value
 * @return ESP_OK on success
 */
esp_err_t ac_server_set_all(uint8_t power, uint8_t temperature, uint8_t mode, uint8_t fan_speed);

/* Getter functions for current AC state */

/** @brief Get current power state. */
uint8_t ac_server_get_current_power(void);

/** @brief Get current temperature setting. */
uint8_t ac_server_get_current_temperature(void);

/** @brief Get current AC mode. */
uint8_t ac_server_get_current_mode(void);

/** @brief Get current fan speed. */
uint8_t ac_server_get_current_fan_speed(void);

#ifdef __cplusplus
}
#endif

#endif /* _AC_CONTROL_H_ */ 