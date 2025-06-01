#ifndef UI_UPDATE_H
#define UI_UPDATE_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the UI update module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_update_init(void);

/**
 * @brief Update the air conditioner status display
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_update_ac_status(void);

/**
 * @brief Update the connection status indicator
 * 
 * @param is_connected true if connected to client, false otherwise
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_update_connection_status(bool is_connected);

/**
 * @brief Set the device name/index display
 * 
 * @param device_name Device name to display
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_update_device_name(const char* device_name);

#ifdef __cplusplus
}
#endif

#endif // UI_UPDATE_H 