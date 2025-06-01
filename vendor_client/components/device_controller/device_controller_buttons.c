#include "device_controller_buttons.h"
#include "device_controller_config.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "esp_log.h"
#include "string.h"

static const char *TAG = "DC_BUTTONS";

// Button handles
static button_handle_t s_button_handles[DC_BUTTON_MAX] = {NULL};
static void (*s_event_callback)(dc_event_t event, void *user_data) = NULL;
static void *s_user_data = NULL;
static bool s_initialized = false;

// Button GPIO configurations
static const int s_button_gpios[DC_BUTTON_MAX] = {
    [DC_BUTTON_UP] = DEVICE_CONTROLLER_BUTTON_UP_GPIO,
    [DC_BUTTON_DOWN] = DEVICE_CONTROLLER_BUTTON_DOWN_GPIO,
    [DC_BUTTON_LEFT] = DEVICE_CONTROLLER_BUTTON_LEFT_GPIO,
    [DC_BUTTON_RIGHT] = DEVICE_CONTROLLER_BUTTON_RIGHT_GPIO,
    [DC_BUTTON_CENTER] = DEVICE_CONTROLLER_BUTTON_CENTER_GPIO,
};

// Button event mapping
static const dc_event_t s_button_events[DC_BUTTON_MAX] = {
    [DC_BUTTON_UP] = DC_EVENT_UP_PRESS,
    [DC_BUTTON_DOWN] = DC_EVENT_DOWN_PRESS,
    [DC_BUTTON_LEFT] = DC_EVENT_LEFT_PRESS,
    [DC_BUTTON_RIGHT] = DC_EVENT_RIGHT_PRESS,
    [DC_BUTTON_CENTER] = DC_EVENT_CENTER_SINGLE_CLICK, // Default to single click
};

// Forward declarations
static void button_single_click_cb(void *button_handle, void *usr_data);
static void button_double_click_cb(void *button_handle, void *usr_data);

// Button callback for single click
static void button_single_click_cb(void *button_handle, void *usr_data)
{
    dc_button_id_t *button_id = (dc_button_id_t *)usr_data;
    
    if (!s_event_callback || !button_id) {
        return;
    }
    
    dc_event_t event;
    if (*button_id == DC_BUTTON_CENTER) {
        event = DC_EVENT_CENTER_SINGLE_CLICK;
    } else {
        event = s_button_events[*button_id];
    }
    
    ESP_LOGD(TAG, "Button %d single click", *button_id);
    s_event_callback(event, s_user_data);
}

// Button callback for double click (only for center button)
static void button_double_click_cb(void *button_handle, void *usr_data)
{
    dc_button_id_t *button_id = (dc_button_id_t *)usr_data;
    
    if (!s_event_callback || !button_id || *button_id != DC_BUTTON_CENTER) {
        return;
    }
    
    ESP_LOGD(TAG, "Button %d double click", *button_id);
    s_event_callback(DC_EVENT_CENTER_DOUBLE_CLICK, s_user_data);
}

esp_err_t dc_buttons_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Button manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Button configuration
    button_config_t btn_cfg = {
        .long_press_time = DEVICE_CONTROLLER_LONG_PRESS_TIME_MS,
        .short_press_time = DEVICE_CONTROLLER_SHORT_PRESS_TIME_MS,
    };
    
    // GPIO configuration
    button_gpio_config_t gpio_cfg = {
        .active_level = DEVICE_CONTROLLER_BUTTON_ACTIVE_LEVEL,
        .enable_power_save = false,
        .disable_pull = false,
    };
    
    // Create button instances
    for (int i = 0; i < DC_BUTTON_MAX; i++) {
        gpio_cfg.gpio_num = s_button_gpios[i];
        
        ret = iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &s_button_handles[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create button %d: %s", i, esp_err_to_name(ret));
            goto cleanup;
        }
        
        // Allocate memory for button ID to pass to callback
        dc_button_id_t *button_id = malloc(sizeof(dc_button_id_t));
        if (!button_id) {
            ESP_LOGE(TAG, "Failed to allocate memory for button ID %d", i);
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }
        *button_id = (dc_button_id_t)i;
        
        // Register single click callback for all buttons
        ret = iot_button_register_cb(s_button_handles[i], BUTTON_SINGLE_CLICK, NULL, button_single_click_cb, button_id);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register single click callback for button %d: %s", i, esp_err_to_name(ret));
            free(button_id);
            goto cleanup;
        }
        
        // Register double click callback only for center button
        if (i == DC_BUTTON_CENTER) {
            ret = iot_button_register_cb(s_button_handles[i], BUTTON_DOUBLE_CLICK, NULL, button_double_click_cb, button_id);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register double click callback for center button: %s", esp_err_to_name(ret));
                free(button_id);
                goto cleanup;
            }
        }
        
        ESP_LOGD(TAG, "Button %d initialized on GPIO %d", i, s_button_gpios[i]);
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Button manager initialized successfully");
    
    return ESP_OK;
    
cleanup:
    // Clean up any created buttons
    for (int i = 0; i < DC_BUTTON_MAX; i++) {
        if (s_button_handles[i] != NULL) {
            iot_button_delete(s_button_handles[i]);
            s_button_handles[i] = NULL;
        }
    }
    
    return ret;
}

esp_err_t dc_buttons_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Delete all button instances
    for (int i = 0; i < DC_BUTTON_MAX; i++) {
        if (s_button_handles[i] != NULL) {
            // Note: The button driver should free the user data (button_id) automatically
            iot_button_delete(s_button_handles[i]);
            s_button_handles[i] = NULL;
        }
    }
    
    s_event_callback = NULL;
    s_user_data = NULL;
    s_initialized = false;
    
    ESP_LOGI(TAG, "Button manager deinitialized");
    
    return ESP_OK;
}

esp_err_t dc_buttons_register_callback(void (*callback)(dc_event_t event, void *user_data), void *user_data)
{
    if (!callback) {
        ESP_LOGE(TAG, "Callback cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    s_event_callback = callback;
    s_user_data = user_data;
    
    ESP_LOGI(TAG, "Button event callback registered");
    
    return ESP_OK;
} 