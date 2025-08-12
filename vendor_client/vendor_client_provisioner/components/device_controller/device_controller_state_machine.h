#pragma once

#include "device_controller_types.h"
#include "device_controller_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the device controller state machine
 * 
 * @param callbacks Callback functions for state changes and display updates
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_state_machine_init(const dc_callbacks_t *callbacks);

/**
 * @brief Deinitialize the device controller state machine
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_state_machine_deinit(void);

/**
 * @brief Process an event in the state machine
 * 
 * @param event Event to process
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_state_machine_process_event(dc_event_t event);

/**
 * @brief Get current state of the state machine
 * 
 * @return dc_state_t Current state
 */
dc_state_t dc_state_machine_get_current_state(void);

/**
 * @brief Get current context of the state machine
 * 
 * @return dc_context_t* Pointer to current context (read-only)
 */
const dc_context_t* dc_state_machine_get_context(void);

/**
 * @brief Force state machine to return to idle state
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_state_machine_force_idle(void);

/**
 * @brief Set device information
 * 
 * @param device_idx Device index
 * @param device_info Device information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_state_machine_set_device_info(uint8_t device_idx, const dc_device_info_t *device_info);

/**
 * @brief Get device information
 * 
 * @param device_idx Device index
 * @return const dc_device_info_t* Device information (read-only)
 */
const dc_device_info_t* dc_state_machine_get_device_info(uint8_t device_idx);

/**
 * @brief Get total number of devices
 * 
 * @return uint8_t Number of devices
 */
uint8_t dc_state_machine_get_device_count(void);

#ifdef __cplusplus
}
#endif 