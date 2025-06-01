#include "device_controller.h"
#include "device_controller_config.h"
#include "device_controller_state_machine.h"
#include "device_controller_buttons.h"
#include "device_controller_display.h"
#include "device_controller_ui_integration.h"
#include "device_controller_types.h"
#include "gui_guider.h"
#include "esp_log.h"

static const char *TAG = "DEVICE_CONTROLLER";

static bool s_initialized = false;
static bool s_started = false;

// Forward declarations
static void button_event_handler(dc_event_t event, void *user_data);
static void state_change_handler(dc_state_t old_state, dc_state_t new_state, void *user_data);
static void display_update_handler(dc_context_t *context, void *user_data);
static esp_err_t parameter_change_handler(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data);
static esp_err_t initialize_multiple_devices(void);
static esp_err_t handle_device_switch(const dc_context_t *context);
static void show_device_switch_animation(uint8_t old_device_idx, uint8_t new_device_idx);

// UI integration callbacks
static void ui_screen_changed_handler(dc_ui_screen_t screen);
static void ui_boot_complete_handler(void);

// Button event handler
static void button_event_handler(dc_event_t event, void *user_data)
{
    // ESP_LOGD(TAG, "Button event: %d", event);
    
    esp_err_t ret = dc_state_machine_process_event(event);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to process event %d: %s", event, esp_err_to_name(ret));
    }
}

// State change handler
static void state_change_handler(dc_state_t old_state, dc_state_t new_state, void *user_data)
{
    ESP_LOGI(TAG, "State changed: %d -> %d", old_state, new_state);
    
    // Get current context for UI updates
    const dc_context_t *context = dc_state_machine_get_context();
    
    // Check for device switch first (can happen in any state)
    if (context) {
        handle_device_switch(context);
    }
    
    // Handle specific state transitions
    switch (new_state) {
        case DC_STATE_IDLE:
            ESP_LOGI(TAG, "Entered idle state");
            // Show main screen when returning to idle
            dc_ui_integration_show_main_screen();
            if (context) {
                dc_ui_integration_update_display((dc_context_t*)context);
                // Also show device status in console
                const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
                if (device) {
                    ESP_LOGI(TAG, "=== DEVICE STATUS ===");
                    ESP_LOGI(TAG, "Device: %s (#%d)", device->device_name, device->device_id + 1);
                    ESP_LOGI(TAG, "Status: %s", device->is_online ? "ONLINE" : "OFFLINE");
                    ESP_LOGI(TAG, "Power:  %s", device->status.power ? "ON" : "OFF");
                    ESP_LOGI(TAG, "Temp:   %ld°C", device->status.temperature);
                    ESP_LOGI(TAG, "Mode:   %s", (device->status.mode == 0) ? "COOL" : 
                            (device->status.mode == 1) ? "HEAT" : 
                            (device->status.mode == 2) ? "FAN" : 
                            (device->status.mode == 3) ? "DRY" : "AUTO");
                    ESP_LOGI(TAG, "Fan:    %s", (device->status.fan_speed == 1) ? "LOW" : 
                            (device->status.fan_speed == 2) ? "MED" : "HIGH");
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "LEFT/RIGHT: Switch Device");
                    ESP_LOGI(TAG, "DOUBLE-CLICK: Enter Menu");
                    ESP_LOGI(TAG, "===================");
                }
            }
            break;
            
        case DC_STATE_MENU_NAVIGATE:
            ESP_LOGI(TAG, "Entered menu navigation state");
            // Show menu navigation with blinking parameter
            if (context) {
                dc_ui_integration_show_menu_navigation((dc_parameter_t)context->current_selection, true);
                dc_ui_integration_update_display((dc_context_t*)context);
            }
            break;
            
        case DC_STATE_VALUE_ADJUST:
            ESP_LOGI(TAG, "Entered value adjustment state");
            // Show value adjustment mode
            if (context) {
                const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
                if (device) {
                    int32_t current_value = 0;
                    // Get current value based on selected parameter
                    switch (context->selected_parameter) {
                        case DC_PARAM_POWER:
                            current_value = device->status.power;
                            break;
                        case DC_PARAM_TEMPERATURE:
                            current_value = device->status.temperature;
                            break;
                        case DC_PARAM_MODE:
                            current_value = device->status.mode;
                            break;
                        case DC_PARAM_FAN_SPEED:
                            current_value = device->status.fan_speed;
                            break;
                        default:
                            break;
                    }
                    dc_ui_integration_show_value_adjustment(context->selected_parameter, current_value, true);
                    
                    // Show console value adjustment display
                    const char *param_name;
                    const char *param_value;
                    
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
                            param_value = (context->editing_value == 1) ? "LOW" : 
                                        (context->editing_value == 2) ? "MED" : "HIGH";
                            break;
                        case DC_PARAM_MODE:
                            param_name = "Mode";
                            param_value = (context->editing_value == 0) ? "COOL" : 
                                        (context->editing_value == 1) ? "HEAT" : 
                                        (context->editing_value == 2) ? "FAN" : 
                                        (context->editing_value == 3) ? "DRY" : "AUTO";
                            break;
                        default:
                            param_name = "Unknown";
                            param_value = "---";
                            break;
                    }
                    
                    ESP_LOGI(TAG, "=== VALUE ADJUSTMENT ===");
                    ESP_LOGI(TAG, "Device: %s", device->device_name);
                    ESP_LOGI(TAG, "Parameter: %s", param_name);
                    ESP_LOGI(TAG, "Value: >>> %s <<<", param_value);
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "UP/DOWN: Adjust");
                    ESP_LOGI(TAG, "CENTER: Confirm");
                    ESP_LOGI(TAG, "DOUBLE-CLICK: Cancel");
                    ESP_LOGI(TAG, "========================");
                }
                dc_ui_integration_update_display((dc_context_t*)context);
            }
            break;
            
        default:
            break;
    }
}

// Display update handler
static void display_update_handler(dc_context_t *context, void *user_data)
{
    esp_err_t ret = dc_ui_integration_update_display(context);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display: %s", esp_err_to_name(ret));
    }
}

// Parameter change handler
static esp_err_t parameter_change_handler(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data)
{
    const char *param_name;
    char msg[64];
    
    switch (param) {
        case DC_PARAM_POWER:
            param_name = "Power";
            snprintf(msg, sizeof(msg), "%s: %s", param_name, value ? "ON" : "OFF");
            break;
            
        case DC_PARAM_TEMPERATURE:
            param_name = "Temperature";
            snprintf(msg, sizeof(msg), "%s: %ld°C", param_name, value);
            break;
            
        case DC_PARAM_FAN_SPEED:
            param_name = "Fan Speed";
            const char *fan_names[] = {"Auto", "Low", "Medium", "High"};
            snprintf(msg, sizeof(msg), "%s: %s", param_name, 
                    (value >= 0 && value < 4) ? fan_names[value] : "Unknown");
            break;
            
        case DC_PARAM_MODE:
            param_name = "Mode";
            const char *mode_names[] = {"Cool", "Heat", "Fan", "Dry", "Auto"};
            snprintf(msg, sizeof(msg), "%s: %s", param_name, 
                    (value >= 0 && value < 5) ? mode_names[value] : "Unknown");
            break;
            
        default:
            snprintf(msg, sizeof(msg), "Parameter %d: %ld", param, value);
            break;
    }
    
    // Show "SET OK" message briefly
    dc_ui_integration_show_message("SET OK", 500);
    
    // Here you would typically send the command to the actual device
    // For demonstration, we just log it
    ESP_LOGI(TAG, "Sending command to device %d: %s", device_id, msg);
    
    return ESP_OK;
}

// Get parameter display string
static const char* get_param_display_string(const dc_device_info_t *device, dc_parameter_t param)
{
    static char str_buffer[32];
    
    switch (param) {
    case DC_PARAM_POWER:
        return device->status.power ? "ON" : "OFF";
        
    case DC_PARAM_TEMPERATURE:
        snprintf(str_buffer, sizeof(str_buffer), "%ld°C", device->status.temperature);
        return str_buffer;
        
    case DC_PARAM_FAN_SPEED:
        switch (device->status.fan_speed) {
            case DEVICE_CONTROLLER_FAN_SPEED_LOW: return "Low";
            case DEVICE_CONTROLLER_FAN_SPEED_MEDIUM: return "Medium";
            case DEVICE_CONTROLLER_FAN_SPEED_HIGH: return "High";
            default: return "Unknown";
        }
        
    case DC_PARAM_MODE:
        switch (device->status.mode) {
            case DEVICE_CONTROLLER_MODE_COOL: return "Cool";
            case DEVICE_CONTROLLER_MODE_HEAT: return "Heat";
            case DEVICE_CONTROLLER_MODE_FAN: return "Fan";
            case DEVICE_CONTROLLER_MODE_DRY: return "Dry";
            case DEVICE_CONTROLLER_MODE_AUTO: return "Auto";
            default: return "Unknown";
        }
        
    default:
        return "Unknown";
    }
}

// UI screen change handler
static void ui_screen_changed_handler(dc_ui_screen_t screen)
{
    ESP_LOGI(TAG, "UI Screen changed to: %d", screen);
    
    switch (screen) {
        case DC_UI_SCREEN_BOOT:
            ESP_LOGI(TAG, "Boot screen active");
            break;
            
        case DC_UI_SCREEN_MAIN:
            ESP_LOGI(TAG, "Main screen active - device controller ready");
            break;
            
        case DC_UI_SCREEN_MENU:
            ESP_LOGI(TAG, "Menu navigation active");
            break;
            
        case DC_UI_SCREEN_MESSAGE:
            ESP_LOGI(TAG, "Message display active");
            break;
            
        default:
            break;
    }
}

// UI boot complete handler
static void ui_boot_complete_handler(void)
{
    ESP_LOGI(TAG, "Boot sequence completed - device controller fully operational");
    
    // Start device controller operations
    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        // Update display with initial device state
        dc_ui_integration_update_display((dc_context_t*)context);
    }
}

// Initialize multiple devices with different configurations
static esp_err_t initialize_multiple_devices(void)
{
    esp_err_t ret = ESP_OK;
    
    // Device 1: Living Room AC
    dc_device_info_t device1 = {
        .device_id = 0,
        .device_name = "Living Room AC",
        .is_online = true,
        .status = {
            .power = true,
            .temperature = 22,
            .mode = DEVICE_CONTROLLER_MODE_COOL,
            .fan_speed = DEVICE_CONTROLLER_FAN_SPEED_MEDIUM
        }
    };
    
    ret = dc_state_machine_set_device_info(0, &device1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set device 1 info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Device 2: Bedroom AC
    dc_device_info_t device2 = {
        .device_id = 1,
        .device_name = "Bedroom AC",
        .is_online = true,
        .status = {
            .power = false,
            .temperature = 25,
            .mode = DEVICE_CONTROLLER_MODE_AUTO,
            .fan_speed = DEVICE_CONTROLLER_FAN_SPEED_LOW
        }
    };
    
    ret = dc_state_machine_set_device_info(1, &device2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set device 2 info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Device 3: Kitchen AC
    dc_device_info_t device3 = {
        .device_id = 2,
        .device_name = "Kitchen AC",
        .is_online = false,
        .status = {
            .power = false,
            .temperature = 26,
            .mode = DEVICE_CONTROLLER_MODE_FAN,
            .fan_speed = DEVICE_CONTROLLER_FAN_SPEED_HIGH
        }
    };
    
    ret = dc_state_machine_set_device_info(2, &device3);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set device 3 info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Initialized 3 devices successfully (Living Room, Bedroom, Kitchen)");
    return ESP_OK;
}

// Handle device switching with animation
static esp_err_t handle_device_switch(const dc_context_t *context)
{
    if (!context) {
        return ESP_ERR_INVALID_ARG;
    }
    
    static uint8_t previous_device_idx = 0;
    
    if (previous_device_idx != context->current_device_idx) {
        ESP_LOGI(TAG, "Device switched from %d to %d", previous_device_idx, context->current_device_idx);
        
        // Show switching animation
        show_device_switch_animation(previous_device_idx, context->current_device_idx);
        
        // Get new device info
        const dc_device_info_t *new_device = dc_state_machine_get_device_info(context->current_device_idx);
        if (new_device) {
            ESP_LOGI(TAG, "=== SWITCHED TO DEVICE %d ===", context->current_device_idx + 1);
            ESP_LOGI(TAG, "Device: %s", new_device->device_name);
            ESP_LOGI(TAG, "Status: %s", new_device->is_online ? "ONLINE" : "OFFLINE");
            ESP_LOGI(TAG, "Power:  %s", new_device->status.power ? "ON" : "OFF");
            ESP_LOGI(TAG, "Temp:   %ld°C", new_device->status.temperature);
            ESP_LOGI(TAG, "Mode:   %s", (new_device->status.mode == 0) ? "COOL" : 
                    (new_device->status.mode == 1) ? "HEAT" : 
                    (new_device->status.mode == 2) ? "FAN" : 
                    (new_device->status.mode == 3) ? "DRY" : "AUTO");
            ESP_LOGI(TAG, "Fan:    %s", (new_device->status.fan_speed == 1) ? "LOW" : 
                    (new_device->status.fan_speed == 2) ? "MED" : "HIGH");
            ESP_LOGI(TAG, "============================");
        }
        
        previous_device_idx = context->current_device_idx;
    }
    
    return ESP_OK;
}

// Show device switch animation
static void show_device_switch_animation(uint8_t old_device_idx, uint8_t new_device_idx)
{
    // Use the dedicated UI integration function for device switching (slide animation only)
    esp_err_t ret = dc_ui_integration_show_device_switch(old_device_idx, new_device_idx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to show device switch animation: %s", esp_err_to_name(ret));
        // Fallback: just refresh main screen
        dc_ui_integration_show_main_screen();
    }
    
    ESP_LOGI(TAG, "Device switch animation: %d → %d", 
             old_device_idx + 1, new_device_idx + 1);
}

esp_err_t device_controller_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Device controller already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // Initialize UI integration first
    dc_ui_callbacks_t ui_callbacks = {
        .on_screen_changed = ui_screen_changed_handler,
        .on_boot_complete = ui_boot_complete_handler
    };
    
    ret = dc_ui_integration_init(&guider_ui, &ui_callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI integration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize buttons
    ret = dc_buttons_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize buttons: %s", esp_err_to_name(ret));
        dc_ui_integration_deinit();
        return ret;
    }
    
    // Set up callbacks
    dc_callbacks_t callbacks = {
        .state_change_cb = state_change_handler,
        .display_update_cb = display_update_handler,
        .param_change_cb = parameter_change_handler,
        .user_data = NULL
    };
    
    // Initialize state machine
    ret = dc_state_machine_init(&callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize state machine: %s", esp_err_to_name(ret));
        dc_buttons_deinit();
        dc_ui_integration_deinit();
        return ret;
    }
    
    // Initialize multiple devices with different configurations
    ret = initialize_multiple_devices();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize multiple devices: %s", esp_err_to_name(ret));
        dc_state_machine_deinit();
        dc_buttons_deinit();
        dc_ui_integration_deinit();
        return ret;
    }
    
    // Register button callback
    ret = dc_buttons_register_callback(button_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register button callback: %s", esp_err_to_name(ret));
        dc_state_machine_deinit();
        dc_buttons_deinit();
        dc_ui_integration_deinit();
        return ret;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Device controller initialized successfully with %d devices", 
             dc_state_machine_get_device_count());
    
    return ESP_OK;
}

esp_err_t device_controller_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_started) {
        ESP_LOGW(TAG, "Device controller already started");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // Start UI integration (shows boot screen)
    ret = dc_ui_integration_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UI integration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_started = true;
    ESP_LOGI(TAG, "Device controller started successfully - boot screen displayed");
    
    return ESP_OK;
}

esp_err_t device_controller_stop(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Force return to idle state
    dc_state_machine_force_idle();
    
    s_started = false;
    ESP_LOGI(TAG, "Device controller stopped");
    
    return ESP_OK;
}

esp_err_t device_controller_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_started) {
        device_controller_stop();
    }
    
    // Deinitialize in reverse order
    dc_state_machine_deinit();
    dc_buttons_deinit();
    dc_ui_integration_deinit();
    
    s_initialized = false;
    ESP_LOGI(TAG, "Device controller deinitialized");
    
    return ESP_OK;
} 