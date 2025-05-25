/* ac_control.h - Air Conditioner Bluetooth Mesh Client Control Interface */

#ifndef _AC_CONTROL_H_
#define _AC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>
#include "esp_ble_mesh_defs.h" // Added for BLE types
#include "ble_mesh_example_nvs.h" // Added for nvs_handle_t

/* AC状态类型枚举 */
typedef enum {
    AC_STATUS_POWER = 0,        /* 电源状态 */
    AC_STATUS_TEMPERATURE = 1,  /* 温度状态 */
    AC_STATUS_MODE = 2,         /* 模式状态 */
    AC_STATUS_FAN_SPEED = 3,    /* 风速状态 */
} ac_status_type_t;

/**
 * @brief Initialize the AC client control interface and BLE stack
 * 
 * @param status_cb Callback function for AC status updates
 * @return ESP_OK on success
 */
// esp_err_t ac_ble_mesh_init(ac_status_callback_t status_cb);

/**
 * @brief Initialize the AC client specific models and callbacks (legacy, might be merged into ac_ble_mesh_init)
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
 * @brief Stores relevant BLE mesh information to NVS.
 */
void ac_ble_mesh_store_info(void);

/**
 * @brief Restores relevant BLE mesh information from NVS.
 */
void ac_ble_mesh_restore_info(void);

/**
 * @brief Gets the currently stored server address.
 *
 * @return uint16_t Server unicast address or ESP_BLE_MESH_ADDR_UNASSIGNED.
 */
uint16_t ac_get_server_addr(void);

/**
 * @brief Sets the server address.
 *
 * @param addr Server unicast address.
 */
void ac_set_server_addr(uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif /* _AC_CONTROL_H_ */ 