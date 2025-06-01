#include "device_controller_state_machine.h"
#include "device_controller_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

static const char *TAG = "DC_STATE_MACHINE";

// State machine context
static dc_context_t s_context = {0};
static dc_callbacks_t s_callbacks = {0};
static dc_device_info_t s_devices[DEVICE_CONTROLLER_MAX_DEVICES] = {0};
static uint8_t s_device_count = 0;
static esp_timer_handle_t s_timeout_timer = NULL;
static bool s_initialized = false;

// Parameter configurations
static const char* s_mode_options[] = {"Cool", "Heat", "Fan", "Dry", "Auto"};

static const dc_param_config_t s_param_configs[DC_PARAM_MAX] = {
    [DC_PARAM_POWER] = {
        .param_id = DC_PARAM_POWER,
        .name = "Power",
        .value_type = DC_VALUE_TYPE_BOOL,
    },
    [DC_PARAM_TEMPERATURE] = {
        .param_id = DC_PARAM_TEMPERATURE,
        .name = "Temperature",
        .value_type = DC_VALUE_TYPE_INT,
        .config.int_range = {.min = DEVICE_CONTROLLER_TEMP_MIN, .max = DEVICE_CONTROLLER_TEMP_MAX, .step = 1}
    },
    [DC_PARAM_FAN_SPEED] = {
        .param_id = DC_PARAM_FAN_SPEED,
        .name = "Fan Speed",
        .value_type = DC_VALUE_TYPE_ENUM,
        .config.enum_options = {.options = (const char*[]){"Low", "Medium", "High"}, .count = 3}
    },
    [DC_PARAM_MODE] = {
        .param_id = DC_PARAM_MODE,
        .name = "Mode",
        .value_type = DC_VALUE_TYPE_ENUM,
        .config.enum_options = {.options = s_mode_options, .count = 5}
    }
};

// Forward declarations
static void timeout_timer_callback(void* arg);
static void start_timeout_timer(void);
static void stop_timeout_timer(void);
static void change_state(dc_state_t new_state);
static void update_display(void);
static int32_t get_parameter_value(uint8_t device_idx, dc_parameter_t param);
static void set_parameter_value(uint8_t device_idx, dc_parameter_t param, int32_t value);
static void adjust_value(dc_parameter_t param, int32_t *value, bool increment);

// Timer callback for timeout events
static void timeout_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "Timeout occurred");
    dc_state_machine_process_event(DC_EVENT_TIMEOUT);
}

static void start_timeout_timer(void)
{
    if (s_timeout_timer) {
        esp_timer_stop(s_timeout_timer);
        esp_timer_start_once(s_timeout_timer, DEVICE_CONTROLLER_TIMEOUT_MS * 1000);
        s_context.timeout_active = true;
        ESP_LOGD(TAG, "Timeout timer started (%d ms)", DEVICE_CONTROLLER_TIMEOUT_MS);
    }
}

static void stop_timeout_timer(void)
{
    if (s_timeout_timer) {
        esp_timer_stop(s_timeout_timer);
        s_context.timeout_active = false;
        ESP_LOGD(TAG, "Timeout timer stopped");
    }
}

static void change_state(dc_state_t new_state)
{
    dc_state_t old_state = s_context.current_state;
    s_context.current_state = new_state;
    
    const char *old_state_str = old_state == DC_STATE_IDLE ? "IDLE" :
                               old_state == DC_STATE_MENU_NAVIGATE ? "MENU_NAVIGATE" :
                               old_state == DC_STATE_VALUE_ADJUST ? "VALUE_ADJUST" : "UNKNOWN";
    const char *new_state_str = new_state == DC_STATE_IDLE ? "IDLE" :
                               new_state == DC_STATE_MENU_NAVIGATE ? "MENU_NAVIGATE" :
                               new_state == DC_STATE_VALUE_ADJUST ? "VALUE_ADJUST" : "UNKNOWN";
    ESP_LOGI(TAG, "State change: %s -> %s", old_state_str, new_state_str);
    
    // Call state change callback
    if (s_callbacks.state_change_cb) {
        s_callbacks.state_change_cb(old_state, new_state, s_callbacks.user_data);
    }
    
    // Handle timeout timer
    if (new_state == DC_STATE_IDLE) {
        stop_timeout_timer();
    } else {
        start_timeout_timer();
    }
    
    update_display();
}

static void update_display(void)
{
    if (s_callbacks.display_update_cb) {
        s_callbacks.display_update_cb(&s_context, s_callbacks.user_data);
    }
}

static int32_t get_parameter_value(uint8_t device_idx, dc_parameter_t param)
{
    if (device_idx >= s_device_count) {
        return 0;
    }
    
    switch (param) {
        case DC_PARAM_POWER:
            return s_devices[device_idx].status.power ? 1 : 0;
        case DC_PARAM_TEMPERATURE:
            return s_devices[device_idx].status.temperature;
        case DC_PARAM_FAN_SPEED:
            return s_devices[device_idx].status.fan_speed;
        case DC_PARAM_MODE:
            return s_devices[device_idx].status.mode;
        default:
            return 0;
    }
}

static void set_parameter_value(uint8_t device_idx, dc_parameter_t param, int32_t value)
{
    if (device_idx >= s_device_count) {
        ESP_LOGW(TAG, "Invalid device index: %d (max: %d)", device_idx, s_device_count);
        return;
    }
    
    ESP_LOGI(TAG, "Setting parameter %d to value %ld for device %d", param, value, device_idx);
    
    switch (param) {
        case DC_PARAM_POWER:
            s_devices[device_idx].status.power = (value != 0);
            ESP_LOGI(TAG, "Power set to: %s", s_devices[device_idx].status.power ? "ON" : "OFF");
            break;
        case DC_PARAM_TEMPERATURE:
            s_devices[device_idx].status.temperature = value;
            ESP_LOGI(TAG, "Temperature set to: %ld°C", s_devices[device_idx].status.temperature);
            break;
        case DC_PARAM_FAN_SPEED:
            s_devices[device_idx].status.fan_speed = value;
            ESP_LOGI(TAG, "Fan speed set to: %d", s_devices[device_idx].status.fan_speed);
            break;
        case DC_PARAM_MODE:
            s_devices[device_idx].status.mode = value;
            ESP_LOGI(TAG, "Mode set to: %d", s_devices[device_idx].status.mode);
            break;
        default:
            ESP_LOGW(TAG, "Unknown parameter: %d", param);
            break;
    }
}

static void adjust_value(dc_parameter_t param, int32_t *value, bool increment)
{
    const dc_param_config_t *config = &s_param_configs[param];
    int32_t old_value = *value;
    
    ESP_LOGI(TAG, "Adjusting parameter %d: %ld -> ", param, old_value);
    
    switch (config->value_type) {
        case DC_VALUE_TYPE_BOOL:
            *value = *value ? 0 : 1;
            ESP_LOGI(TAG, "Bool toggle: %ld -> %ld", old_value, *value);
            break;
            
        case DC_VALUE_TYPE_INT:
            if (increment) {
                *value += config->config.int_range.step;
                if (*value > config->config.int_range.max) {
                    *value = config->config.int_range.max;
                }
            } else {
                *value -= config->config.int_range.step;
                if (*value < config->config.int_range.min) {
                    *value = config->config.int_range.min;
                }
            }
            ESP_LOGI(TAG, "Int adjust (%s): %ld -> %ld (range: %ld-%ld)", 
                     increment ? "+" : "-", old_value, *value, 
                     config->config.int_range.min, config->config.int_range.max);
            break;
            
        case DC_VALUE_TYPE_ENUM:
            if (increment) {
                *value = (*value + 1) % config->config.enum_options.count;
            } else {
                *value = (*value - 1 + config->config.enum_options.count) % config->config.enum_options.count;
            }
            ESP_LOGI(TAG, "Enum adjust (%s): %ld -> %ld (count: %d)", 
                     increment ? "next" : "prev", old_value, *value, 
                     config->config.enum_options.count);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown value type: %d", config->value_type);
            break;
    }
}

esp_err_t dc_state_machine_init(const dc_callbacks_t *callbacks)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "State machine already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!callbacks) {
        ESP_LOGE(TAG, "Callbacks cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy callbacks
    memcpy(&s_callbacks, callbacks, sizeof(dc_callbacks_t));
    
    // Initialize context
    memset(&s_context, 0, sizeof(dc_context_t));
    s_context.current_state = DC_STATE_IDLE;
    
    // Create timeout timer
    esp_timer_create_args_t timer_args = {
        .callback = timeout_timer_callback,
        .arg = NULL,
        .name = "dc_timeout"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &s_timeout_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timeout timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "State machine initialized");
    
    return ESP_OK;
}

esp_err_t dc_state_machine_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop and delete timer
    if (s_timeout_timer) {
        esp_timer_stop(s_timeout_timer);
        esp_timer_delete(s_timeout_timer);
        s_timeout_timer = NULL;
    }
    
    // Clear context
    memset(&s_context, 0, sizeof(dc_context_t));
    memset(&s_callbacks, 0, sizeof(dc_callbacks_t));
    
    s_initialized = false;
    ESP_LOGI(TAG, "State machine deinitialized");
    
    return ESP_OK;
}

esp_err_t dc_state_machine_process_event(dc_event_t event)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "State machine not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Processing event %d in state %d", event, s_context.current_state);
    
    switch (s_context.current_state) {
        case DC_STATE_IDLE:
            switch (event) {
                case DC_EVENT_CENTER_DOUBLE_CLICK:
                    // Enter menu navigation
                    s_context.current_selection = 0; // Start with first parameter (Power)
                    change_state(DC_STATE_MENU_NAVIGATE);
                    break;
                    
                case DC_EVENT_UP_PRESS:
                case DC_EVENT_LEFT_PRESS:
#if DEVICE_CONTROLLER_ENABLE_MULTI_DEVICE
                    // Switch to previous device
                    if (s_device_count > 1) {
                        s_context.current_device_idx = (s_context.current_device_idx - 1 + s_device_count) % s_device_count;
                        update_display();
                    }
#endif
                    break;
                    
                case DC_EVENT_DOWN_PRESS:
                case DC_EVENT_RIGHT_PRESS:
#if DEVICE_CONTROLLER_ENABLE_MULTI_DEVICE
                    // Switch to next device
                    if (s_device_count > 1) {
                        s_context.current_device_idx = (s_context.current_device_idx + 1) % s_device_count;
                        update_display();
                    }
#endif
                    break;
                    
                default:
                    // Ignore other events in idle state
                    break;
            }
            break;
            
        case DC_STATE_MENU_NAVIGATE:
            switch (event) {
                case DC_EVENT_UP_PRESS:
                case DC_EVENT_LEFT_PRESS:
                    // Cycle to previous parameter
                    s_context.current_selection = (s_context.current_selection - 1 + DC_PARAM_MAX) % DC_PARAM_MAX;
                    ESP_LOGI(TAG, "Menu selection changed to parameter %d", s_context.current_selection);
                    update_display();
                    start_timeout_timer(); // Reset timeout
                    break;
                    
                case DC_EVENT_DOWN_PRESS:
                case DC_EVENT_RIGHT_PRESS:
                    // Cycle to next parameter
                    s_context.current_selection = (s_context.current_selection + 1) % DC_PARAM_MAX;
                    ESP_LOGI(TAG, "Menu selection changed to parameter %d", s_context.current_selection);
                    update_display();
                    start_timeout_timer(); // Reset timeout
                    break;
                    
                case DC_EVENT_CENTER_SINGLE_CLICK:
                    // Enter value adjustment
                    s_context.selected_parameter = (dc_parameter_t)s_context.current_selection;
                    s_context.editing_value = get_parameter_value(s_context.current_device_idx, s_context.selected_parameter);
                    ESP_LOGI(TAG, "Entering value adjustment for parameter %d, current value: %ld", 
                             s_context.selected_parameter, s_context.editing_value);
                    change_state(DC_STATE_VALUE_ADJUST);
                    break;
                    
                case DC_EVENT_CENTER_DOUBLE_CLICK:
                case DC_EVENT_TIMEOUT:
                    // Return to idle
                    change_state(DC_STATE_IDLE);
                    break;
                    
                default:
                    break;
            }
            break;
            
        case DC_STATE_VALUE_ADJUST:
            switch (event) {
                case DC_EVENT_UP_PRESS:
                case DC_EVENT_LEFT_PRESS:
                    // Adjust value (increment)
                    adjust_value(s_context.selected_parameter, &s_context.editing_value, true);
                    update_display();
                    start_timeout_timer(); // Reset timeout
                    break;
                    
                case DC_EVENT_DOWN_PRESS:
                case DC_EVENT_RIGHT_PRESS:
                    // Adjust value (decrement)
                    adjust_value(s_context.selected_parameter, &s_context.editing_value, false);
                    update_display();
                    start_timeout_timer(); // Reset timeout
                    break;
                    
                case DC_EVENT_CENTER_SINGLE_CLICK:
                    // Apply value and return to idle
                    set_parameter_value(s_context.current_device_idx, s_context.selected_parameter, s_context.editing_value);
                    
                    // Call parameter change callback
                    if (s_callbacks.param_change_cb) {
                        esp_err_t ret = s_callbacks.param_change_cb(
                            s_devices[s_context.current_device_idx].device_id,
                            s_context.selected_parameter,
                            s_context.editing_value,
                            s_callbacks.user_data
                        );
                        if (ret != ESP_OK) {
                            ESP_LOGW(TAG, "Parameter change callback failed: %s", esp_err_to_name(ret));
                        }
                    }
                    
                    change_state(DC_STATE_IDLE);
                    break;
                    
                case DC_EVENT_CENTER_DOUBLE_CLICK:
                case DC_EVENT_TIMEOUT:
                    // Discard changes and return to idle
                    change_state(DC_STATE_IDLE);
                    break;
                    
                default:
                    break;
            }
            break;
            
        default:
            ESP_LOGE(TAG, "Invalid state: %d", s_context.current_state);
            return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

dc_state_t dc_state_machine_get_current_state(void)
{
    return s_context.current_state;
}

const dc_context_t* dc_state_machine_get_context(void)
{
    return &s_context;
}

esp_err_t dc_state_machine_force_idle(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    change_state(DC_STATE_IDLE);
    return ESP_OK;
}

esp_err_t dc_state_machine_set_device_info(uint8_t device_idx, const dc_device_info_t *device_info)
{
    if (device_idx >= DEVICE_CONTROLLER_MAX_DEVICES || !device_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_devices[device_idx], device_info, sizeof(dc_device_info_t));
    
    if (device_idx >= s_device_count) {
        s_device_count = device_idx + 1;
    }
    
    ESP_LOGI(TAG, "Device %d info updated: %s", device_idx, device_info->device_name);
    
    return ESP_OK;
}

const dc_device_info_t* dc_state_machine_get_device_info(uint8_t device_idx)
{
    if (device_idx >= s_device_count) {
        return NULL;
    }
    
    return &s_devices[device_idx];
}

uint8_t dc_state_machine_get_device_count(void)
{
    return s_device_count;
} 