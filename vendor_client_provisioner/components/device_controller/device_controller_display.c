#include "device_controller_display.h"
#include "device_controller_state_machine.h"
#include "device_controller_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "stdio.h"
#include "device_controller_ui_integration.h"

static const char *TAG = "DC_DISPLAY";

// Display state
static bool s_initialized = false;
static bool s_blink_state = false;
static esp_timer_handle_t s_blink_timer = NULL;
static esp_timer_handle_t s_message_timer = NULL;
static char s_temp_message[128] = {0};
static bool s_showing_temp_message = false;

// Parameter names for display
static const char* s_param_names[DC_PARAM_MAX] = {
    [DC_PARAM_POWER] = "Power",
    [DC_PARAM_TEMPERATURE] = "Temp",
    [DC_PARAM_FAN_SPEED] = "Fan",
    [DC_PARAM_MODE] = "Mode"
};

// Mode names corresponding to mesh_common.h definitions
static const char* s_mode_names[] = {"Cool", "Heat", "Fan", "Dry", "Auto"};

// Fan speed names corresponding to mesh_common.h definitions  
static const char* s_fan_names[] = {"Auto", "Low", "Med", "High"};

// Forward declarations
static void blink_timer_callback(void* arg);
static void message_timer_callback(void* arg);
static void display_idle_state(const dc_context_t *context);
static void display_menu_navigate_state(const dc_context_t *context);
static void display_value_adjust_state(const dc_context_t *context);
static const char* get_parameter_value_string(const dc_device_info_t *device, dc_parameter_t param, int32_t value);

// Blink timer callback for menu highlight
static void blink_timer_callback(void* arg)
{
    s_blink_state = !s_blink_state;
    
    // Trigger display update when blinking to show the change
    const dc_context_t *context = dc_state_machine_get_context();
    if (context && context->current_state == DC_STATE_MENU_NAVIGATE) {
        display_menu_navigate_state(context);
    }
}

// Message timer callback to clear temporary messages
static void message_timer_callback(void* arg)
{
    s_showing_temp_message = false;
    memset(s_temp_message, 0, sizeof(s_temp_message));
    
    // Update display to show normal content
    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        dc_display_update(context);
    }
}

static const char* get_parameter_value_string(const dc_device_info_t *device, dc_parameter_t param, int32_t value)
{
    static char value_str[32];
    
    switch (param) {
        case DC_PARAM_POWER:
            return value ? "ON" : "OFF";
            
        case DC_PARAM_TEMPERATURE:
            snprintf(value_str, sizeof(value_str), "%ld°C", value);
            return value_str;
            
        case DC_PARAM_FAN_SPEED:
            if (value >= 0 && value < 4) {
                return s_fan_names[value];
            }
            return "Unknown";
            
        case DC_PARAM_MODE:
            if (value >= 0 && value < 5) {
                return s_mode_names[value];
            }
            return "Unknown";
            
        default:
            return "N/A";
    }
}

static void display_idle_state(const dc_context_t *context)
{
    const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
    if (!device) {
        ESP_LOGI(TAG, "Display: No device information available");
        return;
    }
    
    ESP_LOGI(TAG, "=== DEVICE CONTROLLER ===");
    
#if DEVICE_CONTROLLER_ENABLE_MULTI_DEVICE
    uint8_t device_count = dc_state_machine_get_device_count();
    if (device_count > 1) {
        ESP_LOGI(TAG, "Device: %s (%d/%d)", device->device_name, 
                context->current_device_idx + 1, device_count);
    } else {
        ESP_LOGI(TAG, "Device: %s", device->device_name);
    }
#else
    ESP_LOGI(TAG, "Device: %s", device->device_name);
#endif
    
    ESP_LOGI(TAG, "Status: %s", device->is_online ? "Online" : "Offline");
    ESP_LOGI(TAG, "Power: %s", device->status.power ? "ON" : "OFF");
    
    if (device->status.power) {
        ESP_LOGI(TAG, "Temperature: %ld°C", device->status.temperature);
        ESP_LOGI(TAG, "Fan Speed: %s", (device->status.fan_speed >= 0 && device->status.fan_speed < 4) ? 
                 s_fan_names[device->status.fan_speed] : "Unknown");
        ESP_LOGI(TAG, "Mode: %s", (device->status.mode >= 0 && device->status.mode < 5) ? 
                 s_mode_names[device->status.mode] : "Unknown");
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Double-click CENTER to enter settings");
    ESP_LOGI(TAG, "========================");
}

static void display_menu_navigate_state(const dc_context_t *context)
{
    ESP_LOGI(TAG, "=== SETTINGS MENU ===");
    
    for (int i = 0; i < DC_PARAM_MAX; i++) {
        bool is_selected = (i == context->current_selection);
        const char *indicator = "";
        
        if (is_selected) {
            // Show blinking indicator for selected item
            indicator = s_blink_state ? ">>> " : "    ";
        } else {
            indicator = "    ";
        }
        
        ESP_LOGI(TAG, "%s%s", indicator, s_param_names[i]);
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "UP/DOWN: Navigate");
    ESP_LOGI(TAG, "CENTER: Select");
    ESP_LOGI(TAG, "DOUBLE-CLICK: Exit");
    ESP_LOGI(TAG, "====================");
}

static void display_value_adjust_state(const dc_context_t *context)
{
    const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
    if (!device) {
        return;
    }
    
    ESP_LOGI(TAG, "=== ADJUST VALUE ===");
    ESP_LOGI(TAG, "Parameter: %s", s_param_names[context->selected_parameter]);
    
    // Show current device value
    int32_t current_value = 0;
    switch (context->selected_parameter) {
        case DC_PARAM_POWER:
            current_value = device->status.power ? 1 : 0;
            break;
        case DC_PARAM_TEMPERATURE:
            current_value = device->status.temperature;
            break;
        case DC_PARAM_FAN_SPEED:
            current_value = device->status.fan_speed;
            break;
        case DC_PARAM_MODE:
            current_value = device->status.mode;
            break;
        default:
            break;
    }
    
    ESP_LOGI(TAG, "Current: %s", get_parameter_value_string(device, context->selected_parameter, current_value));
    ESP_LOGI(TAG, "New:     %s", get_parameter_value_string(device, context->selected_parameter, context->editing_value));
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "UP/DOWN: Change value");
    ESP_LOGI(TAG, "CENTER: Apply & Save");
    ESP_LOGI(TAG, "DOUBLE-CLICK: Cancel");
    ESP_LOGI(TAG, "===================");
}

esp_err_t dc_display_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Create blink timer for menu highlighting
    esp_timer_create_args_t blink_timer_args = {
        .callback = blink_timer_callback,
        .arg = NULL,
        .name = "dc_blink"
    };
    
    esp_err_t ret = esp_timer_create(&blink_timer_args, &s_blink_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create blink timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create message timer for temporary messages
    esp_timer_create_args_t message_timer_args = {
        .callback = message_timer_callback,
        .arg = NULL,
        .name = "dc_message"
    };
    
    ret = esp_timer_create(&message_timer_args, &s_message_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create message timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_blink_timer);
        return ret;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Display module initialized");
    
    return ESP_OK;
}

esp_err_t dc_display_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop and delete timers
    if (s_blink_timer) {
        esp_timer_stop(s_blink_timer);
        esp_timer_delete(s_blink_timer);
        s_blink_timer = NULL;
    }
    
    if (s_message_timer) {
        esp_timer_stop(s_message_timer);
        esp_timer_delete(s_message_timer);
        s_message_timer = NULL;
    }
    
    s_initialized = false;
    ESP_LOGI(TAG, "Display module deinitialized");
    
    return ESP_OK;
}

esp_err_t dc_display_update(const dc_context_t *context)
{
    if (!s_initialized || !context) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Use UI integration to update display
    return dc_ui_integration_update_display(context);
}

esp_err_t dc_display_show_message(const char *message, uint32_t duration_ms)
{
    if (!s_initialized || !message) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Use UI integration to show message
    return dc_ui_integration_show_message(message, duration_ms);
}

esp_err_t dc_display_clear(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // For UI integration, clearing would mean showing main screen
    return dc_ui_integration_show_main_screen();
} 