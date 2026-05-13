/*
 * UI Update Module for Smart-home Server
 * Handles display updates for the currently registered node profile.
 */

#include "ui_update.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gui_guider.h"
#include "ac_control.h"
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

static void hide_fan_speed_display(void)
{
    if (guider_ui.screen_1_speed1) {
        lv_obj_add_flag(guider_ui.screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
    }
    if (guider_ui.screen_1_speed2) {
        lv_obj_add_flag(guider_ui.screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
    }
    if (guider_ui.screen_1_speed3) {
        lv_obj_add_flag(guider_ui.screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool read_feature_value(uint16_t feature_id, int32_t *value)
{
    sh_feature_state_t state;
    if (ac_server_get_feature(feature_id, &state) == ESP_OK) {
        if (value) {
            *value = state.value;
        }
        return true;
    }
    return false;
}

static const sh_feature_def_t *find_first_non_power_feature(const sh_device_profile_t *profile)
{
    if (!profile || !profile->features) {
        return NULL;
    }

    for (uint8_t i = 0; i < profile->feature_count; i++) {
        if (profile->features[i].feature_id != SH_FEATURE_ID_POWER) {
            return &profile->features[i];
        }
    }
    return NULL;
}

static const char *format_feature_value(const sh_feature_def_t *feature,
                                        int32_t value,
                                        char *buf,
                                        size_t buf_len)
{
    if (!feature) {
        return "---";
    }

    switch (feature->type) {
        case SH_FEATURE_TYPE_BOOL:
            return value ? "ON" : "OFF";
        case SH_FEATURE_TYPE_ENUM:
            if (value >= 0 && value < feature->constraints.enum_count &&
                feature->constraints.enum_labels) {
                return feature->constraints.enum_labels[value];
            }
            snprintf(buf, buf_len, "%ld", value);
            return buf;
        case SH_FEATURE_TYPE_INT:
            snprintf(buf, buf_len, "%ld", value);
            return buf;
        default:
            return "---";
    }
}

static void update_ac_profile_locked(void)
{
    int32_t temperature = 25;
    int32_t mode = AC_MODE_COOL;
    int32_t fan_speed = AC_FAN_SPEED_LOW;

    read_feature_value(SH_FEATURE_ID_TEMPERATURE, &temperature);
    read_feature_value(SH_FEATURE_ID_MODE, &mode);
    read_feature_value(SH_FEATURE_ID_FAN_SPEED, &fan_speed);

    if (guider_ui.screen_1_TempNum) {
        char temp_str[8];
        snprintf(temp_str, sizeof(temp_str), "%ld", temperature);
        lv_label_set_text(guider_ui.screen_1_TempNum, temp_str);
    }

    if (guider_ui.screen_1_Mode) {
        lv_label_set_text(guider_ui.screen_1_Mode, get_mode_string((uint8_t)mode));
    }

    update_fan_speed_display((uint8_t)fan_speed);
}

static void update_generic_profile_locked(const sh_device_profile_t *profile)
{
    const sh_feature_def_t *feature = find_first_non_power_feature(profile);
    int32_t value = 0;
    char value_buf[16];

    hide_fan_speed_display();

    if (feature && read_feature_value(feature->feature_id, &value)) {
        if (guider_ui.screen_1_TempNum) {
            lv_label_set_text(guider_ui.screen_1_TempNum,
                              format_feature_value(feature, value, value_buf, sizeof(value_buf)));
        }
        if (guider_ui.screen_1_Mode) {
            lv_label_set_text(guider_ui.screen_1_Mode, feature->name ? feature->name : "Feature");
        }
    } else {
        if (guider_ui.screen_1_TempNum) {
            lv_label_set_text(guider_ui.screen_1_TempNum, "--");
        }
        if (guider_ui.screen_1_Mode) {
            lv_label_set_text(guider_ui.screen_1_Mode,
                              profile && profile->display_name ? profile->display_name : "Node");
        }
    }
}

/* Update the smart-home node status display */
esp_err_t ui_update_node_status(void)
{
    if (!guider_ui.screen_1) {
        ESP_LOGW(TAG, "Screen not available for update");
        return ESP_ERR_INVALID_STATE;
    }

    const sh_device_profile_t *profile = ac_server_get_profile();

    // Lock LVGL before making changes
    if (!lvgl_port_lock(100)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex");
        return ESP_ERR_TIMEOUT;
    }

    if (guider_ui.screen_1_DeviceIndex && profile && profile->display_name) {
        lv_label_set_text(guider_ui.screen_1_DeviceIndex, profile->display_name);
    }

    // Update power status
    if (guider_ui.screen_1_OnOff) {
        int32_t power = 0;
        if (read_feature_value(SH_FEATURE_ID_POWER, &power)) {
            lv_label_set_text(guider_ui.screen_1_OnOff, power ? "ON" : "OFF");
        } else {
            lv_label_set_text(guider_ui.screen_1_OnOff, "--");
        }
    }

    if (profile && profile->profile_id == SH_PROFILE_ID_AC) {
        update_ac_profile_locked();
    } else {
        update_generic_profile_locked(profile);
    }

    lvgl_port_unlock();

    ESP_LOGD(TAG, "Node status updated for profile 0x%04x (%s)",
             profile ? profile->profile_id : 0,
             profile && profile->display_name ? profile->display_name : "unknown");

    return ESP_OK;
}

/* Update the air conditioner status display */
esp_err_t ui_update_ac_status(void)
{
    return ui_update_node_status();
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

/* Initialize UI with initial node state */
esp_err_t ui_update_init(void)
{
    ESP_LOGI(TAG, "Initializing UI update module");

    // Wait a bit for UI to be fully set up
    vTaskDelay(pdMS_TO_TICKS(500));

    // Set initial device name (before provisioning)
    ui_update_device_name("Unprovisioned");

    // Update with current profile status
    ui_update_node_status();

    // Set initial connection status as disconnected
    ui_update_connection_status(false);

    ESP_LOGI(TAG, "UI update module initialized");

    return ESP_OK;
}
