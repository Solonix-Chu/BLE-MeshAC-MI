#pragma once

#include "device_controller_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the button manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_buttons_init(void);

/**
 * @brief Deinitialize the button manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_buttons_deinit(void);

/**
 * @brief Register a button event callback
 * 
 * @param callback Callback function to be called when button events occur
 * @param user_data User data to be passed to callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_buttons_register_callback(void (*callback)(dc_event_t event, void *user_data), void *user_data);

#ifdef __cplusplus
}
#endif 