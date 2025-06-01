#pragma once

#include "device_controller_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the display module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_display_init(void);

/**
 * @brief Deinitialize the display module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_display_deinit(void);

/**
 * @brief Update display based on current context
 * 
 * @param context Current state machine context
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_display_update(const dc_context_t *context);

/**
 * @brief Show a message on the display
 * 
 * @param message Message to display
 * @param duration_ms Duration to show message (0 for permanent)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_display_show_message(const char *message, uint32_t duration_ms);

/**
 * @brief Clear the display
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_display_clear(void);

#ifdef __cplusplus
}
#endif 