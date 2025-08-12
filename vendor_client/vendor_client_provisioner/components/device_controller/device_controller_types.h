#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device controller states
 */
typedef enum {
    DC_STATE_IDLE = 0,          /**< Main display mode - normal standby */
    DC_STATE_MENU_NAVIGATE,     /**< Menu navigation mode - settings selection */
    DC_STATE_VALUE_ADJUST,      /**< Parameter adjustment mode */
    DC_STATE_MAX
} dc_state_t;

/**
 * @brief Button identifiers
 */
typedef enum {
    DC_BUTTON_UP = 0,
    DC_BUTTON_DOWN,
    DC_BUTTON_LEFT,
    DC_BUTTON_RIGHT,
    DC_BUTTON_CENTER,
    DC_BUTTON_MAX
} dc_button_id_t;

/**
 * @brief Button events
 */
typedef enum {
    DC_EVENT_UP_PRESS = 0,
    DC_EVENT_DOWN_PRESS,
    DC_EVENT_LEFT_PRESS,
    DC_EVENT_RIGHT_PRESS,
    DC_EVENT_CENTER_SINGLE_CLICK,
    DC_EVENT_CENTER_DOUBLE_CLICK,
    DC_EVENT_CENTER_LONG_PRESS,
    DC_EVENT_TIMEOUT,
    DC_EVENT_MAX
} dc_event_t;

/**
 * @brief Device parameters
 */
typedef enum {
    DC_PARAM_POWER = 0,         /**< Power on/off */
    DC_PARAM_TEMPERATURE,       /**< Temperature setting */
    DC_PARAM_FAN_SPEED,         /**< Fan speed setting */
    DC_PARAM_MODE,              /**< Operating mode */
    DC_PARAM_MAX
} dc_parameter_t;

/**
 * @brief Parameter value types
 */
typedef enum {
    DC_VALUE_TYPE_BOOL = 0,     /**< Boolean value (on/off) */
    DC_VALUE_TYPE_INT,          /**< Integer value */
    DC_VALUE_TYPE_ENUM,         /**< Enumerated value */
    DC_VALUE_TYPE_MAX
} dc_value_type_t;

/**
 * @brief Parameter configuration
 */
typedef struct {
    dc_parameter_t param_id;
    const char *name;
    dc_value_type_t value_type;
    union {
        struct {
            int32_t min;
            int32_t max;
            int32_t step;
        } int_range;
        struct {
            const char **options;
            uint8_t count;
        } enum_options;
    } config;
} dc_param_config_t;

/**
 * @brief Device information
 */
typedef struct {
    uint8_t device_id;
    const char *device_name;
    bool is_online;
    struct {
        bool power;
        int32_t temperature;
        uint8_t fan_speed;
        uint8_t mode;
    } status;
} dc_device_info_t;

/**
 * @brief Controller context
 */
typedef struct {
    dc_state_t current_state;
    uint8_t current_device_idx;
    dc_parameter_t selected_parameter;
    int32_t editing_value;
    uint8_t current_selection;
    bool timeout_active;
} dc_context_t;

/**
 * @brief State change callback function
 */
typedef void (*dc_state_change_cb_t)(dc_state_t old_state, dc_state_t new_state, void *user_data);

/**
 * @brief Display update callback function
 */
typedef void (*dc_display_update_cb_t)(dc_context_t *context, void *user_data);

/**
 * @brief Parameter change callback function
 */
typedef esp_err_t (*dc_param_change_cb_t)(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data);

/**
 * @brief Device controller callbacks
 */
typedef struct {
    dc_state_change_cb_t state_change_cb;
    dc_display_update_cb_t display_update_cb;
    dc_param_change_cb_t param_change_cb;
    void *user_data;
} dc_callbacks_t;

#ifdef __cplusplus
}
#endif 