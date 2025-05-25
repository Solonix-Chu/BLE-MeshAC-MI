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
 * @brief Gets the unicast address of the first/default managed server.
 * @note For controlling specific multiple servers, use ac_get_server_addr_by_index() and iterate.
 *
 * @return uint16_t Server unicast address or ESP_BLE_MESH_ADDR_UNASSIGNED if no servers are managed.
 */
// uint16_t ac_get_server_addr(void);

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

#ifdef __cplusplus
}
#endif

#endif /* _AC_CONTROL_H_ */ 