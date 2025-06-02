#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the device controller application
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t device_controller_init(void);

/**
 * @brief Start the device controller application
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t device_controller_start(void);

/**
 * @brief Stop the device controller application
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t device_controller_stop(void);

/**
 * @brief Deinitialize the device controller application
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t device_controller_deinit(void);

/**
 * @brief Refresh status of a specific device by requesting current values from server
 * 
 * This function sends get requests for power, temperature, mode, and fan speed
 * to the specified device to synchronize the local state with the actual device state.
 * 
 * @param device_index Index of the device to refresh (0-based)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if device index invalid,
 *                   ESP_ERR_INVALID_STATE if device offline or controller not initialized
 */
esp_err_t device_controller_refresh_device_status(uint8_t device_index);

/**
 * @brief Refresh status of all online devices by requesting current values from servers
 * 
 * This function sends get requests for all status parameters to all online devices
 * to synchronize the local state with the actual device states.
 * 
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if controller not initialized
 */
esp_err_t device_controller_refresh_all_devices_status(void);

#ifdef __cplusplus
}
#endif 