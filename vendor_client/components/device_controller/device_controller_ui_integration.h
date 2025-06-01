#pragma once

#include "device_controller_types.h"
#include "gui_guider.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI Screen types for device controller
 */
typedef enum {
    DC_UI_SCREEN_BOOT,          // Boot logo screen
    DC_UI_SCREEN_MAIN,          // Main device status display (screen_1)
    DC_UI_SCREEN_MENU,          // Menu navigation screen
    DC_UI_SCREEN_MESSAGE        // Temporary message display
} dc_ui_screen_t;

/**
 * @brief UI Integration callbacks
 */
typedef struct {
    void (*on_screen_changed)(dc_ui_screen_t screen);
    void (*on_boot_complete)(void);
} dc_ui_callbacks_t;

/**
 * @brief Initialize UI integration module
 * 
 * @param ui Pointer to LVGL UI structure
 * @param callbacks UI event callbacks
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_init(lv_ui *ui, const dc_ui_callbacks_t *callbacks);

/**
 * @brief Start the UI integration (shows boot screen)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_start(void);

/**
 * @brief Update UI display based on device controller context
 * 
 * @param context Current device controller context
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_update_display(const dc_context_t *context);

/**
 * @brief Show temporary message on UI
 * 
 * @param message Message to display
 * @param duration_ms Duration in milliseconds (0 for permanent)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_show_message(const char *message, uint32_t duration_ms);

/**
 * @brief Switch to main application screen after boot
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_show_main_screen(void);

/**
 * @brief Show menu navigation indicators
 * 
 * @param selected_param Currently selected parameter
 * @param is_blinking Whether the selection should blink
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_show_menu_navigation(dc_parameter_t selected_param, bool is_blinking);

/**
 * @brief Show value adjustment mode
 * 
 * @param param Parameter being adjusted
 * @param value Current value
 * @param is_blinking Whether the value should blink
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_show_value_adjustment(dc_parameter_t param, int32_t value, bool is_blinking);

/**
 * @brief Update device information display
 * 
 * @param device_info Device information to display
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_update_device_info(const dc_device_info_t *device_info);

/**
 * @brief Deinitialize UI integration
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dc_ui_integration_deinit(void);

/**
 * @brief Get current UI screen
 * 
 * @return dc_ui_screen_t Current screen type
 */
dc_ui_screen_t dc_ui_integration_get_current_screen(void);

#ifdef __cplusplus
}
#endif 