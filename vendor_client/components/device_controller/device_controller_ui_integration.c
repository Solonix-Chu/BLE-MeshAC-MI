#include "device_controller_ui_integration.h"
#include "device_controller_config.h"
#include "device_controller_state_machine.h"
#include "gui_guider.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DC_UI_INTEGRATION";

// Internal state
static struct {
    lv_ui *ui;                              // LVGL UI structure
    dc_ui_callbacks_t callbacks;           // UI callbacks
    dc_ui_screen_t current_screen;          // Current screen type
    esp_timer_handle_t boot_timer;          // Boot screen timer
    esp_timer_handle_t message_timer;       // Message display timer
    esp_timer_handle_t blink_timer;         // Blinking animation timer
    bool is_initialized;                    // Initialization flag
    bool is_blinking;                       // Current blink state
    dc_parameter_t blinking_param;          // Currently blinking parameter
    lv_obj_t *message_label;                // Temporary message label
} s_ui_state = {0};

// Forward declarations
static void boot_timer_callback(void *arg);
static void message_timer_callback(void *arg);
static void blink_timer_callback(void *arg);
static esp_err_t show_boot_screen(void);
static esp_err_t update_main_screen_values(const dc_context_t *context);
static esp_err_t update_device_status_display(const dc_device_info_t *device);
static const char* get_parameter_display_string(const dc_device_info_t *device, dc_parameter_t param);
static const char* get_mode_string(uint8_t mode);
static const char* get_fan_speed_string(uint8_t speed);
static const sh_feature_def_t *get_profile_feature(const dc_device_info_t *device, dc_parameter_t param);
static int32_t get_profile_feature_value(const dc_device_info_t *device, const sh_feature_def_t *feature);
static const char *format_profile_feature_value(const sh_feature_def_t *feature, int32_t value);
static void display_settings_menu(const dc_context_t *context);
static void display_value_adjustment(const dc_context_t *context);
static void display_device_status(const dc_device_info_t *device);
static esp_err_t update_editing_value_display(const dc_context_t *context);
static void force_refresh_ui_display(const dc_context_t *context);
static void clear_all_hidden_flags(void);

// Boot timer callback - switch to main screen after boot timeout
static void boot_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "Boot timeout reached, switching to main screen");
    dc_ui_integration_show_main_screen();

    if (s_ui_state.callbacks.on_boot_complete) {
        s_ui_state.callbacks.on_boot_complete();
    }
}

// 安全的消息删除回调函数，通过LVGL异步调用执行
static void safe_message_delete_callback(void *user_data)
{
    if (s_ui_state.message_label) {
        if (lv_obj_is_valid(s_ui_state.message_label)) {
            ESP_LOGD(TAG, "Safely deleting message label via async call");
            lv_obj_del_async(s_ui_state.message_label);
        } else {
            ESP_LOGW(TAG, "Message label is no longer valid during async deletion");
        }
        s_ui_state.message_label = NULL;
    }
}

// Message timer callback - hide temporary message
static void message_timer_callback(void *arg)
{
    // 使用LVGL异步调用确保在LVGL任务上下文中执行UI操作
    // 这样可以避免从定时器回调中直接操作UI对象的线程安全问题
    if (s_ui_state.message_label) {
        ESP_LOGD(TAG, "Scheduling message label deletion via async call");
        // 使用LVGL的异步调用机制，确保UI操作在正确的上下文中执行
        lv_async_call(safe_message_delete_callback, NULL);
    }
}

// Blink timer callback - toggle visibility for blinking elements
static void blink_timer_callback(void *arg)
{
    // Remove screen type check that was preventing blinking
    s_ui_state.is_blinking = !s_ui_state.is_blinking;

    // Get current context to access editing values
    const dc_context_t *context = dc_state_machine_get_context();

    // Toggle visibility based on blinking parameter
    lv_obj_t *target_obj = NULL;

    switch (s_ui_state.blinking_param) {
        case DC_PARAM_POWER:
            target_obj = s_ui_state.ui->screen_1_OnOff;
            // 在值调整状态下，确保显示最新的编辑值
            if (context && context->current_state == DC_STATE_VALUE_ADJUST &&
                context->selected_parameter == DC_PARAM_POWER) {
                if (target_obj) {
                    lv_label_set_text(target_obj, (context->editing_value != 0) ? "ON" : "OFF");
                }
            }
            break;

        case DC_PARAM_TEMPERATURE:
            target_obj = s_ui_state.ui->screen_1_TempNum;
            // 在值调整状态下，确保显示最新的编辑值
            if (context && context->current_state == DC_STATE_VALUE_ADJUST &&
                context->selected_parameter == DC_PARAM_TEMPERATURE) {
                if (target_obj) {
                    char temp_str[16];
                    snprintf(temp_str, sizeof(temp_str), "%ld", context->editing_value);
                    lv_label_set_text(target_obj, temp_str);
                }
            }
            break;

        case DC_PARAM_MODE:
            target_obj = s_ui_state.ui->screen_1_Mode;
            // 在值调整状态下，确保显示最新的编辑值
            if (context && context->current_state == DC_STATE_VALUE_ADJUST &&
                context->selected_parameter == DC_PARAM_MODE) {
                if (target_obj) {
                    lv_label_set_text(target_obj, get_mode_string(context->editing_value));
                }
            }
            break;

        case DC_PARAM_FAN_SPEED:
            // For fan speed blinking, we need to get current fan speed to know which icon should blink
            if (context) {
                uint8_t current_fan_speed;

                // Get current fan speed based on state
                if (context->current_state == DC_STATE_VALUE_ADJUST) {
                    current_fan_speed = context->editing_value;
                } else {
                    const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
                    current_fan_speed = device ? device->status.fan_speed : DEVICE_CONTROLLER_FAN_SPEED_LOW;
                }

                // First ensure all are hidden
                if (s_ui_state.ui->screen_1_speed1) {
                    lv_obj_add_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
                }
                if (s_ui_state.ui->screen_1_speed2) {
                    lv_obj_add_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
                }
                if (s_ui_state.ui->screen_1_speed3) {
                    lv_obj_add_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
                }

                // Then show/hide the appropriate icon based on blink state
                if (!s_ui_state.is_blinking) {
                    switch (current_fan_speed) {
                        case DEVICE_CONTROLLER_FAN_SPEED_LOW:
                            if (s_ui_state.ui->screen_1_speed1) {
                                lv_obj_clear_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
                            }
                            break;
                        case DEVICE_CONTROLLER_FAN_SPEED_MEDIUM:
                            if (s_ui_state.ui->screen_1_speed2) {
                                lv_obj_clear_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
                            }
                            break;
                        case DEVICE_CONTROLLER_FAN_SPEED_HIGH:
                            if (s_ui_state.ui->screen_1_speed3) {
                                lv_obj_clear_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
                            }
                            break;
                        default: // AUTO or unknown
                            // All icons remain hidden
                            break;
                    }
                }
                // When blinking, icons stay hidden (done above)
            }
            ESP_LOGD(TAG, "Fan speed blinking: %s", s_ui_state.is_blinking ? "hidden" : "visible");
            return;
        default:
            break;
    }

    if (target_obj) {
        // Use show/hide instead of opacity for single color OLED
        if (s_ui_state.is_blinking) {
            lv_obj_add_flag(target_obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(target_obj, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGD(TAG, "Parameter %d blinking: %s", s_ui_state.blinking_param,
                 s_ui_state.is_blinking ? "hidden" : "visible");
    }
}

// Show boot screen with logo
static esp_err_t show_boot_screen(void)
{
    if (!s_ui_state.ui) {
        return ESP_ERR_INVALID_STATE;
    }

    // Create boot screen if it doesn't exist
    if (!s_ui_state.ui->screen) {
        ESP_LOGI(TAG, "Boot screen not found, creating it...");
        setup_scr_screen(s_ui_state.ui);
        if (!s_ui_state.ui->screen) {
            ESP_LOGE(TAG, "Failed to create boot screen");
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI(TAG, "Boot screen created successfully");
    }

    // Load boot screen if not already active
    if (lv_scr_act() != s_ui_state.ui->screen) {
        lv_scr_load_anim(s_ui_state.ui->screen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
        ESP_LOGI(TAG, "Boot screen loaded with fade-in animation");
    }

    s_ui_state.current_screen = DC_UI_SCREEN_BOOT;

    // Start boot timer
    const esp_timer_create_args_t boot_timer_args = {
        .callback = boot_timer_callback,
        .name = "boot_timer"
    };

    if (s_ui_state.boot_timer) {
        esp_timer_stop(s_ui_state.boot_timer);
        esp_timer_delete(s_ui_state.boot_timer);
    }

    esp_err_t ret = esp_timer_create(&boot_timer_args, &s_ui_state.boot_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create boot timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_start_once(s_ui_state.boot_timer, DEVICE_CONTROLLER_BOOT_DISPLAY_TIME_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start boot timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Boot screen displayed, will switch to main in %d ms",
             DEVICE_CONTROLLER_BOOT_DISPLAY_TIME_MS);

    return ESP_OK;
}

// Update main screen with current values
static esp_err_t update_main_screen_values(const dc_context_t *context)
{
    if (!s_ui_state.ui || !context) {
        ESP_LOGE(TAG, "Invalid parameters: ui=%p, context=%p", s_ui_state.ui, context);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Getting device info for device %d", context->current_device_idx);

    // Get device information from state machine
    const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
    if (!device) {
        ESP_LOGW(TAG, "No device information available for device %d", context->current_device_idx);

        // Try to get device count to see if the state machine is working
        uint8_t device_count = dc_state_machine_get_device_count();
        ESP_LOGI(TAG, "Total device count: %d", device_count);

        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Found device info: id=%d, online=%d, power=%d, temp=%ld",
             device->device_id, device->is_online, device->status.power, device->status.temperature);

    return update_device_status_display(device);
}

// Update device status display on screen_1
static esp_err_t update_device_status_display(const dc_device_info_t *device)
{
    if (!s_ui_state.ui || !device) {
        return ESP_ERR_INVALID_ARG;
    }

    char temp_str[16];

    // Update device index - show full device name instead of just number
    if (s_ui_state.ui->screen_1_DeviceIndex) {
        lv_label_set_text(s_ui_state.ui->screen_1_DeviceIndex, device->device_name);
    }

    const sh_feature_def_t *feature0 = get_profile_feature(device, 0);
    const sh_feature_def_t *feature1 = get_profile_feature(device, 1);
    const sh_feature_def_t *feature2 = get_profile_feature(device, 2);

    // Update primary numeric slot
    if (s_ui_state.ui->screen_1_TempNum) {
        if (feature1) {
            snprintf(temp_str, sizeof(temp_str), "%s",
                     format_profile_feature_value(feature1, get_profile_feature_value(device, feature1)));
        } else {
            snprintf(temp_str, sizeof(temp_str), "%ld", device->status.temperature);
        }
        lv_label_set_text(s_ui_state.ui->screen_1_TempNum, temp_str);
    }

    // Update primary boolean/status slot
    if (s_ui_state.ui->screen_1_OnOff) {
        if (feature0) {
            lv_label_set_text(s_ui_state.ui->screen_1_OnOff,
                              format_profile_feature_value(feature0, get_profile_feature_value(device, feature0)));
        } else {
            lv_label_set_text(s_ui_state.ui->screen_1_OnOff, device->status.power ? "ON" : "OFF");
        }
    }

    // Update secondary mode/text slot
    if (s_ui_state.ui->screen_1_Mode) {
        if (feature2) {
            lv_label_set_text(s_ui_state.ui->screen_1_Mode,
                              format_profile_feature_value(feature2, get_profile_feature_value(device, feature2)));
        } else {
            lv_label_set_text(s_ui_state.ui->screen_1_Mode, get_mode_string(device->status.mode));
        }
    }

    // Update fan speed indicators
    uint8_t fan_level = device->status.fan_speed;

    // First, hide all fan speed indicators
    if (s_ui_state.ui->screen_1_speed1) {
        lv_obj_add_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui_state.ui->screen_1_speed2) {
        lv_obj_add_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui_state.ui->screen_1_speed3) {
        lv_obj_add_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
    }

    // Then show appropriate indicators based on fan speed
    switch (device->status.fan_speed) {
        case DEVICE_CONTROLLER_FAN_SPEED_LOW:
            // Only show speed1 for low speed
            if (s_ui_state.ui->screen_1_speed1) {
                lv_obj_clear_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case DEVICE_CONTROLLER_FAN_SPEED_MEDIUM:
            // Only show speed2 for medium speed
            if (s_ui_state.ui->screen_1_speed2) {
                lv_obj_clear_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case DEVICE_CONTROLLER_FAN_SPEED_HIGH:
            // Only show speed3 for high speed
            if (s_ui_state.ui->screen_1_speed3) {
                lv_obj_clear_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        default:
            // All indicators hidden (already done above)
            break;
    }

    // Update connection status with heart icons
    if (s_ui_state.ui->screen_1_HeartReal && s_ui_state.ui->screen_1_HeartEmpty) {
        if (device->is_online) {
            lv_obj_clear_flag(s_ui_state.ui->screen_1_HeartReal, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_ui_state.ui->screen_1_HeartEmpty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui_state.ui->screen_1_HeartReal, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_ui_state.ui->screen_1_HeartEmpty, LV_OBJ_FLAG_HIDDEN);
        }
    }

    return ESP_OK;
}

// Helper functions for string conversion
static const char* get_mode_string(uint8_t mode)
{
    switch (mode) {
        case DEVICE_CONTROLLER_MODE_COOL: return "COOL";
        case DEVICE_CONTROLLER_MODE_HEAT: return "HEAT";
        case DEVICE_CONTROLLER_MODE_FAN: return "FAN";
        case DEVICE_CONTROLLER_MODE_DRY: return "DRY";
        case DEVICE_CONTROLLER_MODE_AUTO: return "AUTO";
        default: return "---";
    }
}

static const char* get_fan_speed_string(uint8_t speed)
{
    switch (speed) {
        case DEVICE_CONTROLLER_FAN_SPEED_LOW: return "LOW";
        case DEVICE_CONTROLLER_FAN_SPEED_MEDIUM: return "MED";
        case DEVICE_CONTROLLER_FAN_SPEED_HIGH: return "HIGH";
        default: return "---";
    }
}

static const sh_feature_def_t *get_profile_feature(const dc_device_info_t *device, dc_parameter_t param)
{
    if (!device || !device->profile || param >= device->profile->feature_count) {
        return NULL;
    }
    return &device->profile->features[param];
}

static int32_t get_profile_feature_value(const dc_device_info_t *device, const sh_feature_def_t *feature)
{
    if (!device || !feature) {
        return 0;
    }
    const sh_feature_state_t *state = sh_model_find_const_state(device->feature_states,
                                                                device->feature_state_count,
                                                                feature->feature_id);
    return state ? state->value : feature->default_value;
}

static const char *format_profile_feature_value(const sh_feature_def_t *feature, int32_t value)
{
    static char str_buffer[32];

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
            snprintf(str_buffer, sizeof(str_buffer), "%ld", value);
            return str_buffer;
        case SH_FEATURE_TYPE_INT:
            snprintf(str_buffer, sizeof(str_buffer), "%ld", value);
            return str_buffer;
        default:
            return "---";
    }
}

static const char* get_parameter_display_string(const dc_device_info_t *device, dc_parameter_t param)
{
    static char str_buffer[32];

    const sh_feature_def_t *feature = get_profile_feature(device, param);
    if (feature) {
        return format_profile_feature_value(feature, get_profile_feature_value(device, feature));
    }

    switch (param) {
        case DC_PARAM_POWER:
            return device->status.power ? "ON" : "OFF";

        case DC_PARAM_TEMPERATURE:
            snprintf(str_buffer, sizeof(str_buffer), "%ld°C", device->status.temperature);
            return str_buffer;

        case DC_PARAM_FAN_SPEED:
            return get_fan_speed_string(device->status.fan_speed);

        case DC_PARAM_MODE:
            return get_mode_string(device->status.mode);

        default:
            return "Unknown";
    }
}

// Console display functions for detailed menu navigation
static void display_settings_menu(const dc_context_t *context)
{
    if (!context) return;

    const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
    if (!device) return;

    ESP_LOGI(TAG, "=== SETTINGS MENU ===");

    uint8_t param_count = (device->profile && device->profile->feature_count) ?
        device->profile->feature_count : DC_PARAM_MAX;

    // Display all profile features with selection indicator
    for (int i = 0; i < param_count; i++) {
        const char *indicator = (i == context->current_selection) ? ">>>" : "   ";
        const char *param_name;
        const char *param_value;
        static char temp_str[16];

        // In VALUE_ADJUST state, show editing_value for the selected parameter
        bool show_editing_value = (context->current_state == DC_STATE_VALUE_ADJUST &&
                                 context->selected_parameter == (dc_parameter_t)i);

        const sh_feature_def_t *feature = get_profile_feature(device, (dc_parameter_t)i);
        if (feature) {
            param_name = feature->name;
            int32_t value = show_editing_value ?
                context->editing_value : get_profile_feature_value(device, feature);
            param_value = format_profile_feature_value(feature, value);
        } else switch ((dc_parameter_t)i) {
            case DC_PARAM_POWER:
                param_name = "Power";
                if (show_editing_value) {
                    param_value = (context->editing_value != 0) ? "ON" : "OFF";
                } else {
                    param_value = device->status.power ? "ON" : "OFF";
                }
                break;
            case DC_PARAM_TEMPERATURE:
                param_name = "Temp";
                if (show_editing_value) {
                    snprintf(temp_str, sizeof(temp_str), "%ld°C", context->editing_value);
                } else {
                    snprintf(temp_str, sizeof(temp_str), "%ld°C", device->status.temperature);
                }
                param_value = temp_str;
                break;
            case DC_PARAM_FAN_SPEED:
                param_name = "Fan";
                if (show_editing_value) {
                    param_value = get_fan_speed_string(context->editing_value);
                } else {
                    param_value = get_fan_speed_string(device->status.fan_speed);
                }
                break;
            case DC_PARAM_MODE:
                param_name = "Mode";
                if (show_editing_value) {
                    param_value = get_mode_string(context->editing_value);
                } else {
                    param_value = get_mode_string(device->status.mode);
                }
                break;
            default:
                continue;
        }

        ESP_LOGI(TAG, "%s %-8s: %s", indicator, param_name, param_value);
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "UP/DOWN: Navigate");
    ESP_LOGI(TAG, "CENTER: Select");
    ESP_LOGI(TAG, "DOUBLE-CLICK: Exit");
    ESP_LOGI(TAG, "====================");
}

static void display_value_adjustment(const dc_context_t *context)
{
    if (!context) return;

    const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
    if (!device) return;

    const char *param_name;
    const char *param_value;

    const sh_feature_def_t *feature = get_profile_feature(device, context->selected_parameter);
    if (feature) {
        param_name = feature->name;
        param_value = format_profile_feature_value(feature, context->editing_value);
    } else {

    switch (context->selected_parameter) {
        case DC_PARAM_POWER:
            param_name = "Power";
            param_value = (context->editing_value != 0) ? "ON" : "OFF";
            break;
        case DC_PARAM_TEMPERATURE:
            param_name = "Temperature";
            static char temp_str[16];
            snprintf(temp_str, sizeof(temp_str), "%ld°C", context->editing_value);
            param_value = temp_str;
            break;
        case DC_PARAM_FAN_SPEED:
            param_name = "Fan Speed";
            param_value = get_fan_speed_string(context->editing_value);
            break;
        case DC_PARAM_MODE:
            param_name = "Mode";
            param_value = get_mode_string(context->editing_value);
            break;
        default:
            return;
    }
    }

    ESP_LOGI(TAG, "=== VALUE ADJUSTMENT ===");
    ESP_LOGI(TAG, "Parameter: %s", param_name);
    ESP_LOGI(TAG, "Value: >>> %s <<<", param_value);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "UP/DOWN: Adjust");
    ESP_LOGI(TAG, "CENTER: Confirm");
    ESP_LOGI(TAG, "DOUBLE-CLICK: Cancel");
    ESP_LOGI(TAG, "========================");
}

static void display_device_status(const dc_device_info_t *device)
{
    if (!device) return;

    ESP_LOGI(TAG, "=== DEVICE STATUS ===");
    ESP_LOGI(TAG, "Device: %s (#%d)", device->device_name, device->device_id + 1);
    ESP_LOGI(TAG, "Status: %s", device->is_online ? "ONLINE" : "OFFLINE");
    ESP_LOGI(TAG, "Power:  %s", device->status.power ? "ON" : "OFF");
    ESP_LOGI(TAG, "Temp:   %ld°C", device->status.temperature);
    ESP_LOGI(TAG, "Mode:   %s", get_mode_string(device->status.mode));
    ESP_LOGI(TAG, "Fan:    %s", get_fan_speed_string(device->status.fan_speed));
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "DOUBLE-CLICK: Enter Menu");
    ESP_LOGI(TAG, "===================");
}

// Update screen display with editing value during value adjustment
static esp_err_t update_editing_value_display(const dc_context_t *context)
{
    if (!s_ui_state.ui || !context) {
        return ESP_ERR_INVALID_ARG;
    }

    char temp_str[16];

    // 检查是否正在闪烁当前参数，如果是，则不直接操作显示状态
    bool is_param_blinking = (s_ui_state.blink_timer != NULL &&
                             s_ui_state.blinking_param == context->selected_parameter);

    switch (context->selected_parameter) {
        case DC_PARAM_POWER:
            if (s_ui_state.ui->screen_1_OnOff) {
                lv_label_set_text(s_ui_state.ui->screen_1_OnOff,
                                 (context->editing_value != 0) ? "ON" : "OFF");
                // 如果正在闪烁，不要修改显示状态，让闪烁定时器控制
                // The blinking timer will handle visibility
            }
            break;

        case DC_PARAM_TEMPERATURE:
            if (s_ui_state.ui->screen_1_TempNum) {
                snprintf(temp_str, sizeof(temp_str), "%ld", context->editing_value);
                lv_label_set_text(s_ui_state.ui->screen_1_TempNum, temp_str);
                // 如果正在闪烁，不要修改显示状态，让闪烁定时器控制
                // The blinking timer will handle visibility
            }
            break;

        case DC_PARAM_MODE:
            if (s_ui_state.ui->screen_1_Mode) {
                lv_label_set_text(s_ui_state.ui->screen_1_Mode, get_mode_string(context->editing_value));
                // 如果正在闪烁，不要修改显示状态，让闪烁定时器控制
                // The blinking timer will handle visibility
            }
            break;

        case DC_PARAM_FAN_SPEED:
            // 如果该参数正在闪烁，不要直接操作UI元素的显示状态
            // 让闪烁定时器处理显示/隐藏逻辑，我们只负责更新值
            if (!is_param_blinking) {
                // 只有在不闪烁时才直接操作显示状态
                // First, hide all fan speed indicators
                if (s_ui_state.ui->screen_1_speed1) {
                    lv_obj_add_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
                }
                if (s_ui_state.ui->screen_1_speed2) {
                    lv_obj_add_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
                }
                if (s_ui_state.ui->screen_1_speed3) {
                    lv_obj_add_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
                }

                // Then show appropriate indicators based on editing value
                switch (context->editing_value) {
                    case DEVICE_CONTROLLER_FAN_SPEED_LOW:
                        // Only show speed1 for low speed
                        if (s_ui_state.ui->screen_1_speed1) {
                            lv_obj_clear_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
                        }
                        break;
                    case DEVICE_CONTROLLER_FAN_SPEED_MEDIUM:
                        // Only show speed2 for medium speed
                        if (s_ui_state.ui->screen_1_speed2) {
                            lv_obj_clear_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
                        }
                        break;
                    case DEVICE_CONTROLLER_FAN_SPEED_HIGH:
                        // Only show speed3 for high speed
                        if (s_ui_state.ui->screen_1_speed3) {
                            lv_obj_clear_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
                        }
                        break;
                    default:
                        // All indicators hidden (already done above)
                        break;
                }
            }
            // 如果正在闪烁，blink_timer_callback会根据最新的editing_value来处理显示逻辑
            break;

        default:
            break;
    }

    ESP_LOGD(TAG, "Updated editing value display for param %d, value %ld (blinking: %s)",
             context->selected_parameter, context->editing_value, is_param_blinking ? "yes" : "no");

    return ESP_OK;
}

// Helper function to clear hidden flags from all UI elements
static void clear_all_hidden_flags(void)
{
    if (!s_ui_state.ui) {
        return;
    }

    // Clear hidden flags for all text elements
    if (s_ui_state.ui->screen_1_TempNum) {
        lv_obj_clear_flag(s_ui_state.ui->screen_1_TempNum, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui_state.ui->screen_1_OnOff) {
        lv_obj_clear_flag(s_ui_state.ui->screen_1_OnOff, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui_state.ui->screen_1_Mode) {
        lv_obj_clear_flag(s_ui_state.ui->screen_1_Mode, LV_OBJ_FLAG_HIDDEN);
    }

    // For fan speed, we need to show the correct one based on current state
    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        uint8_t current_fan_speed;

        // Get current fan speed based on state
        if (context->current_state == DC_STATE_VALUE_ADJUST) {
            current_fan_speed = context->editing_value;
        } else {
            const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
            current_fan_speed = device ? device->status.fan_speed : DEVICE_CONTROLLER_FAN_SPEED_LOW;
        }

        // First hide all fan speed indicators
        if (s_ui_state.ui->screen_1_speed1) {
            lv_obj_add_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_ui_state.ui->screen_1_speed2) {
            lv_obj_add_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_ui_state.ui->screen_1_speed3) {
            lv_obj_add_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
        }

        // Then show appropriate one
        switch (current_fan_speed) {
            case DEVICE_CONTROLLER_FAN_SPEED_LOW:
                if (s_ui_state.ui->screen_1_speed1) {
                    lv_obj_clear_flag(s_ui_state.ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);
                }
                break;
            case DEVICE_CONTROLLER_FAN_SPEED_MEDIUM:
                if (s_ui_state.ui->screen_1_speed2) {
                    lv_obj_clear_flag(s_ui_state.ui->screen_1_speed2, LV_OBJ_FLAG_HIDDEN);
                }
                break;
            case DEVICE_CONTROLLER_FAN_SPEED_HIGH:
                if (s_ui_state.ui->screen_1_speed3) {
                    lv_obj_clear_flag(s_ui_state.ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);
                }
                break;
            default:
                // All indicators hidden (already done above)
                break;
        }
    }

    ESP_LOGD(TAG, "Cleared all hidden flags from UI elements");
}

// Public API Implementation

esp_err_t dc_ui_integration_init(lv_ui *ui, const dc_ui_callbacks_t *callbacks)
{
    if (!ui) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ui_state.is_initialized) {
        ESP_LOGW(TAG, "UI integration already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ui_state, 0, sizeof(s_ui_state));
    s_ui_state.ui = ui;

    if (callbacks) {
        s_ui_state.callbacks = *callbacks;
    }

    s_ui_state.current_screen = DC_UI_SCREEN_BOOT;
    s_ui_state.is_initialized = true;

    ESP_LOGI(TAG, "UI integration initialized");
    return ESP_OK;
}

esp_err_t dc_ui_integration_start(void)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting UI integration with boot screen");
    return show_boot_screen();
}

esp_err_t dc_ui_integration_update_display(const dc_context_t *context)
{
    if (!s_ui_state.is_initialized) {
        ESP_LOGE(TAG, "UI integration not initialized");
        return ESP_ERR_INVALID_ARG;
    }

    if (!context) {
        ESP_LOGE(TAG, "Context is NULL in update_display");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Updating display for screen %d, device %d, state %d",
             s_ui_state.current_screen, context->current_device_idx, context->current_state);

    // 在值调整状态下，只更新值内容，不重新设置整个显示状态
    // 这样可以避免干扰闪烁定时器的工作
    if (context->current_state == DC_STATE_VALUE_ADJUST) {
        // 只更新编辑值的内容，不改变显示/隐藏状态
        ESP_LOGD(TAG, "Updating editing value without interfering with blinking");
        return update_editing_value_display(context);
    }

    // 对于非值调整状态，按正常流程更新
    switch (s_ui_state.current_screen) {
        case DC_UI_SCREEN_MAIN:
            // Update with device actual values
            return update_main_screen_values(context);

        case DC_UI_SCREEN_MENU:
            // In menu mode, show console menu and handle blinking
            display_settings_menu(context);
            esp_err_t ret = dc_ui_integration_show_menu_navigation((dc_parameter_t)context->current_selection, true);
            // 在菜单导航模式下，确保非闪烁的元素都可见
            // Clear hidden flags for non-blinking elements in menu navigation
            if (ret == ESP_OK && context->current_state == DC_STATE_MENU_NAVIGATE) {
                // Force refresh after setting up blinking to ensure correct display
                force_refresh_ui_display(context);
            }
            return ret;

        case DC_UI_SCREEN_BOOT:
        case DC_UI_SCREEN_MESSAGE:
        default:
            ESP_LOGD(TAG, "No update needed for screen type %d", s_ui_state.current_screen);
            break;
    }

    return ESP_OK;
}

esp_err_t dc_ui_integration_show_message(const char *message, uint32_t duration_ms)
{
    if (!s_ui_state.is_initialized || !message) {
        return ESP_ERR_INVALID_ARG;
    }

    // 先停止现有的消息定时器，避免竞态条件
    if (s_ui_state.message_timer) {
        esp_timer_stop(s_ui_state.message_timer);
        esp_timer_delete(s_ui_state.message_timer);
        s_ui_state.message_timer = NULL;
    }

    // Remove existing message if any
    if (s_ui_state.message_label) {
        if (lv_obj_is_valid(s_ui_state.message_label)) {
            lv_obj_del_async(s_ui_state.message_label);
        }
        s_ui_state.message_label = NULL;
    }

    // Create message label overlay
    s_ui_state.message_label = lv_label_create(lv_scr_act());
    if (!s_ui_state.message_label) {
        ESP_LOGE(TAG, "Failed to create message label");
        return ESP_ERR_NO_MEM;
    }

    lv_label_set_text(s_ui_state.message_label, message);
    lv_obj_set_style_text_color(s_ui_state.message_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(s_ui_state.message_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_ui_state.message_label, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(s_ui_state.message_label, 10, 0);
    lv_obj_center(s_ui_state.message_label);

    // Set timer to hide message
    if (duration_ms > 0) {
        const esp_timer_create_args_t timer_args = {
            .callback = message_timer_callback,
            .name = "message_timer"
        };

        esp_err_t ret = esp_timer_create(&timer_args, &s_ui_state.message_timer);
        if (ret == ESP_OK) {
            ret = esp_timer_start_once(s_ui_state.message_timer, duration_ms * 1000);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start message timer: %s", esp_err_to_name(ret));
                esp_timer_delete(s_ui_state.message_timer);
                s_ui_state.message_timer = NULL;
            }
        } else {
            ESP_LOGE(TAG, "Failed to create message timer: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Showing message: %s (duration: %lu ms)", message, duration_ms);
    return ESP_OK;
}

esp_err_t dc_ui_integration_show_main_screen(void)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Stop boot timer if running
    if (s_ui_state.boot_timer) {
        esp_timer_stop(s_ui_state.boot_timer);
        esp_timer_delete(s_ui_state.boot_timer);
        s_ui_state.boot_timer = NULL;
    }

    // Stop blink timer
    if (s_ui_state.blink_timer) {
        esp_timer_stop(s_ui_state.blink_timer);
        esp_timer_delete(s_ui_state.blink_timer);
        s_ui_state.blink_timer = NULL;
    }

    // Setup main screen if not already done
    if (!s_ui_state.ui->screen_1) {
        setup_scr_screen_1(s_ui_state.ui);
    }

    // Load main screen with simple fade-in animation
    if (s_ui_state.ui->screen_1) {
        lv_scr_load_anim(s_ui_state.ui->screen_1, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, true);
    } else {
        ESP_LOGE(TAG, "Main screen (ui->screen_1) is not available");
        return ESP_ERR_INVALID_STATE;
    }

    s_ui_state.current_screen = DC_UI_SCREEN_MAIN;

    // 恢复到空闲主界面时，确保所有UI元素都正确显示
    clear_all_hidden_flags();

    // Force refresh UI display with current context
    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        force_refresh_ui_display(context);
    }

    if (s_ui_state.callbacks.on_screen_changed) {
        s_ui_state.callbacks.on_screen_changed(DC_UI_SCREEN_MAIN);
    }

    ESP_LOGI(TAG, "Switched to main screen");
    return ESP_OK;
}

esp_err_t dc_ui_integration_show_device_switch(uint8_t old_device_idx, uint8_t new_device_idx)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Device switched: %d → %d", old_device_idx + 1, new_device_idx + 1);

    // Simply update the display with new device information
    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        force_refresh_ui_display(context);
    }

    return ESP_OK;
}

esp_err_t dc_ui_integration_show_menu_navigation(dc_parameter_t selected_param, bool is_blinking)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ui_state.current_screen = DC_UI_SCREEN_MENU;

    // If switching to a different parameter while already blinking, clear previous element's hidden flag
    if (s_ui_state.blink_timer && s_ui_state.blinking_param != selected_param) {
        ESP_LOGD(TAG, "Switching blinking parameter from %d to %d", s_ui_state.blinking_param, selected_param);

        // Clear hidden flag for previous blinking parameter
        switch (s_ui_state.blinking_param) {
            case DC_PARAM_POWER:
                if (s_ui_state.ui->screen_1_OnOff) {
                    lv_obj_clear_flag(s_ui_state.ui->screen_1_OnOff, LV_OBJ_FLAG_HIDDEN);
                }
                break;
            case DC_PARAM_TEMPERATURE:
                if (s_ui_state.ui->screen_1_TempNum) {
                    lv_obj_clear_flag(s_ui_state.ui->screen_1_TempNum, LV_OBJ_FLAG_HIDDEN);
                }
                break;
            case DC_PARAM_MODE:
                if (s_ui_state.ui->screen_1_Mode) {
                    lv_obj_clear_flag(s_ui_state.ui->screen_1_Mode, LV_OBJ_FLAG_HIDDEN);
                }
                break;
            case DC_PARAM_FAN_SPEED:
                // For fan speed, ensure correct icon is shown
                clear_all_hidden_flags();
                break;
            default:
                break;
        }
    }

    s_ui_state.blinking_param = selected_param;

    if (is_blinking) {
        // Start blink timer for selected parameter
        const esp_timer_create_args_t timer_args = {
            .callback = blink_timer_callback,
            .name = "blink_timer"
        };

        if (s_ui_state.blink_timer) {
            esp_timer_stop(s_ui_state.blink_timer);
            esp_timer_delete(s_ui_state.blink_timer);
        }

        esp_err_t ret = esp_timer_create(&timer_args, &s_ui_state.blink_timer);
        if (ret == ESP_OK) {
            esp_timer_start_periodic(s_ui_state.blink_timer, 10000); // 10ms blink interval
        }
    } else {
        // Stop blinking
        if (s_ui_state.blink_timer) {
            esp_timer_stop(s_ui_state.blink_timer);
            esp_timer_delete(s_ui_state.blink_timer);
            s_ui_state.blink_timer = NULL;
        }

        // When stopping blink, clear all hidden flags to ensure all elements are visible
        ESP_LOGI(TAG, "Stopping blinking, clearing all hidden flags");
        clear_all_hidden_flags();

        // Force refresh the UI to ensure all elements display correctly
        const dc_context_t *context = dc_state_machine_get_context();
        if (context) {
            ESP_LOGI(TAG, "Refreshing UI display after stopping blink");
            force_refresh_ui_display(context);
        }

        s_ui_state.current_screen = DC_UI_SCREEN_MAIN;
    }

    return ESP_OK;
}

esp_err_t dc_ui_integration_show_value_adjustment(dc_parameter_t param, int32_t value, bool is_blinking)
{
    // For value adjustment, we reuse the same blinking mechanism as menu navigation
    return dc_ui_integration_show_menu_navigation(param, is_blinking);
}

esp_err_t dc_ui_integration_update_device_info(const dc_device_info_t *device_info)
{
    if (!s_ui_state.is_initialized || !device_info) {
        return ESP_ERR_INVALID_ARG;
    }

    return update_device_status_display(device_info);
}

esp_err_t dc_ui_integration_deinit(void)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Stop and delete all timers
    if (s_ui_state.boot_timer) {
        esp_timer_stop(s_ui_state.boot_timer);
        esp_timer_delete(s_ui_state.boot_timer);
    }

    if (s_ui_state.message_timer) {
        esp_timer_stop(s_ui_state.message_timer);
        esp_timer_delete(s_ui_state.message_timer);
    }

    if (s_ui_state.blink_timer) {
        esp_timer_stop(s_ui_state.blink_timer);
        esp_timer_delete(s_ui_state.blink_timer);
    }

    // Clean up message label
    if (s_ui_state.message_label) {
        if (lv_obj_is_valid(s_ui_state.message_label)) {
            lv_obj_del_async(s_ui_state.message_label);
        }
        s_ui_state.message_label = NULL;
    }

    memset(&s_ui_state, 0, sizeof(s_ui_state));

    ESP_LOGI(TAG, "UI integration deinitialized");
    return ESP_OK;
}

dc_ui_screen_t dc_ui_integration_get_current_screen(void)
{
    return s_ui_state.current_screen;
}

static void force_refresh_ui_display(const dc_context_t *context)
{
    if (!context || !s_ui_state.ui) {
        return;
    }

    // Force refresh based on current state
    if (context->current_state == DC_STATE_VALUE_ADJUST) {
        // In value adjustment state, use editing values
        update_editing_value_display(context);
    } else {
        // In normal state, use device actual values
        const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
        if (device) {
            update_device_status_display(device);
        }
    }

    ESP_LOGD(TAG, "UI display force refreshed for state %d", context->current_state);
}
