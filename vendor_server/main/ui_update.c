/*
 * UI Update Module for AC Server
 * Handles display updates for the air conditioner server device
 */

#include "ui_update.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gui_guider.h"
#include "ac_control.h"
#include "mesh_common.h"
#include <stdio.h>

static const char *TAG = "UI_UPDATE";

extern lv_ui guider_ui;

/* Helper functions to convert AC state to display strings */
static const char* get_mode_string(uint8_t mode)
{
    switch (mode) {
        case AC_MODE_COOL:
            return "COOL";
        case AC_MODE_HEAT:
            return "HEAT";
        case AC_MODE_FAN:
            return "FAN";
        case AC_MODE_DRY:
            return "DRY";
        case AC_MODE_AUTO:
            return "AUTO";
        default:
            return "COOL";
    }
}

static void update_fan_speed_display(uint8_t fan_speed)
{
    if (!guider_ui.screen_1) {
        return;
    }

    // Hide all fan speed icons first
    if (guider_ui.screen_1_speed1) {
        lv_obj_add_flag(guider_ui.screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
    }
    if (guider_ui.screen_1_speed2) {
        lv_obj_add_flag(guider_ui.screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
    }
    if (guider_ui.screen_1_speed3) {
        lv_obj_add_flag(guider_ui.screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
    }

    // Show the appropriate fan speed icon
    switch (fan_speed) {
        case AC_FAN_SPEED_LOW:
            if (guider_ui.screen_1_speed1) {
                lv_obj_clear_flag(guider_ui.screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case AC_FAN_SPEED_MEDIUM:
            if (guider_ui.screen_1_speed2) {
                lv_obj_clear_flag(guider_ui.screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case AC_FAN_SPEED_HIGH:
            if (guider_ui.screen_1_speed3) {
                lv_obj_clear_flag(guider_ui.screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        default:
            // For AUTO mode, all icons remain hidden
            break;
    }
}

/* Update the air conditioner status display */
esp_err_t ui_update_ac_status(void)
{
    if (!guider_ui.screen_1) {
        ESP_LOGW(TAG, "Screen not available for update");
        return ESP_ERR_INVALID_STATE;
    }

    // Lock LVGL before making changes
    if (!lvgl_port_lock(100)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Update power status
    if (guider_ui.screen_1_OnOff) {
        uint8_t power = ac_server_get_current_power();
        lv_label_set_text(guider_ui.screen_1_OnOff, power ? "ON" : "OFF");
    }

    // Update temperature
    if (guider_ui.screen_1_TempNum) {
        uint8_t temperature = ac_server_get_current_temperature();
        char temp_str[8];
        snprintf(temp_str, sizeof(temp_str), "%d", temperature);
        lv_label_set_text(guider_ui.screen_1_TempNum, temp_str);
    }

    // Update mode
    if (guider_ui.screen_1_Mode) {
        uint8_t mode = ac_server_get_current_mode();
        lv_label_set_text(guider_ui.screen_1_Mode, get_mode_string(mode));
    }

    // Update fan speed display
    uint8_t fan_speed = ac_server_get_current_fan_speed();
    update_fan_speed_display(fan_speed);

    lvgl_port_unlock();

    ESP_LOGD(TAG, "AC status updated - Power: %s, Temp: %d°C, Mode: %s, Fan: %d",
             ac_server_get_current_power() ? "ON" : "OFF",
             ac_server_get_current_temperature(),
             get_mode_string(ac_server_get_current_mode()),
             ac_server_get_current_fan_speed());

    return ESP_OK;
}

/* Update connection status indicator (heartbeat) */
esp_err_t ui_update_connection_status(bool is_connected)
{
    if (!guider_ui.screen_1) {
        ESP_LOGW(TAG, "Screen not available for update");
        return ESP_ERR_INVALID_STATE;
    }

    // Lock LVGL before making changes
    if (!lvgl_port_lock(100)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Update heart icon based on connection status
    if (guider_ui.screen_1_HeartReal && guider_ui.screen_1_HeartEmpty) {
        if (is_connected) {
            // Show filled heart (connected)
            lv_obj_clear_flag(guider_ui.screen_1_HeartReal, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(guider_ui.screen_1_HeartEmpty, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Show empty heart (disconnected)
            lv_obj_add_flag(guider_ui.screen_1_HeartReal, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(guider_ui.screen_1_HeartEmpty, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lvgl_port_unlock();

    ESP_LOGD(TAG, "Connection status updated: %s", is_connected ? "Connected" : "Disconnected");

    return ESP_OK;
}

/* Set device name/index display */
esp_err_t ui_update_device_name(const char* device_name)
{
    if (!device_name || !guider_ui.screen_1 || !guider_ui.screen_1_DeviceIndex) {
        return ESP_ERR_INVALID_ARG;
    }

    // Lock LVGL before making changes
    if (!lvgl_port_lock(100)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex");
        return ESP_ERR_TIMEOUT;
    }

    lv_label_set_text(guider_ui.screen_1_DeviceIndex, device_name);

    lvgl_port_unlock();

    ESP_LOGD(TAG, "Device name updated to: %s", device_name);

    return ESP_OK;
}

/* Initialize UI with initial AC state */
esp_err_t ui_update_init(void)
{
    ESP_LOGI(TAG, "Initializing UI update module");
    
    // Wait a bit for UI to be fully set up
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Set initial device name
    ui_update_device_name("AC Server");
    
    // Update with current AC status
    ui_update_ac_status();
    
    // Set initial connection status as disconnected
    ui_update_connection_status(false);
    
    ESP_LOGI(TAG, "UI update module initialized");
    
    return ESP_OK;
} 