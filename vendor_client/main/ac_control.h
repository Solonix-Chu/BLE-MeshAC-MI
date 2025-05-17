/* ac_control.h - Air Conditioner Bluetooth Mesh Client Control Interface */

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

/**
 * @brief Initialize the AC client control interface
 * 
 * @return ESP_OK on success
 */
esp_err_t ac_client_init(void);

/**
 * @brief Send power control message to server
 * 
 * @param server_addr Server address
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
 * @brief AC status callback function type
 */
typedef void (*ac_status_callback_t)(ac_status_type_t type, uint8_t value);

/**
 * @brief Register AC status callback
 * 
 * @param callback Callback function
 */
void ac_client_register_callback(ac_status_callback_t callback);

/**
 * @brief Set network parameters for client messages
 *
 * @param net_idx Network index
 * @param app_idx Application index
 */
void ac_client_set_network_params(uint16_t net_idx, uint16_t app_idx);

/**
 * @brief Reset fail count for network messages
 */
void ac_client_reset_fail_count(void);

#ifdef __cplusplus
}
#endif

#endif /* _AC_CONTROL_H_ */ 