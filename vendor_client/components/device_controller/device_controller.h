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

#ifdef __cplusplus
}
#endif 