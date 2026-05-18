/* ac_control.c - Compatibility facade over generic smart-home mesh node */

#include "ac_control.h"
#include "board.h"
#include "ui_update.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "esp_ble_mesh_networking_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AC_NODE_FACADE";

static esp_timer_handle_t s_restart_timer;

static const sh_device_profile_t *get_configured_profile(void)
{
#if defined(CONFIG_SH_NODE_PROFILE_LIGHT) && CONFIG_SH_NODE_PROFILE_LIGHT
    return sh_profile_light_get();
#elif defined(CONFIG_SH_NODE_PROFILE_SWITCH) && CONFIG_SH_NODE_PROFILE_SWITCH
    return sh_profile_switch_get();
#elif defined(CONFIG_SH_NODE_PROFILE_TV) && CONFIG_SH_NODE_PROFILE_TV
    return sh_profile_tv_get();
#elif defined(CONFIG_SH_NODE_PROFILE_CURTAIN) && CONFIG_SH_NODE_PROFILE_CURTAIN
    return sh_profile_curtain_get();
#else
    return sh_profile_ac_get();
#endif
}

static uint8_t get_feature_u8(uint16_t feature_id, uint8_t fallback)
{
    sh_feature_state_t state;
    if (sh_node_get_feature(feature_id, &state) == ESP_OK) {
        return (uint8_t)state.value;
    }
    return fallback;
}

static void restart_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Resetting BLE Mesh node and restarting after disconnect request");
    esp_ble_mesh_node_local_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void schedule_restart(void)
{
    if (s_restart_timer) {
        esp_timer_stop(s_restart_timer);
        esp_timer_delete(s_restart_timer);
        s_restart_timer = NULL;
    }

    const esp_timer_create_args_t args = {
        .callback = restart_timer_cb,
        .name = "sh_restart",
    };
    if (esp_timer_create(&args, &s_restart_timer) == ESP_OK) {
        esp_timer_start_once(s_restart_timer, 3000000);
    }
}

static void feature_changed_cb(uint16_t feature_id,
                               sh_feature_type_t type,
                               int32_t value,
                               void *user_data)
{
    (void)feature_id;
    (void)type;
    (void)value;
    (void)user_data;
    ui_update_node_status();
    board_led_temp_blink(128, 0, 128, 1, 400);
}

static void connection_cb(bool is_connected, uint16_t client_addr, void *user_data)
{
    (void)client_addr;
    (void)user_data;
    ui_update_connection_status(is_connected);
    board_led_operation(is_connected ? LED_STATE_SUCCESS : LED_STATE_PROV);
}

static void reset_requested_cb(void *user_data)
{
    (void)user_data;
    board_led_operation(LED_STATE_ERROR);
    ui_update_connection_status(false);
    schedule_restart();
}

static void profile_changed_cb(const sh_device_profile_t *profile, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "Node profile changed to 0x%04x (%s)",
             profile ? profile->profile_id : 0,
             profile && profile->display_name ? profile->display_name : "unknown");
    ui_update_node_status();
}

esp_err_t ac_server_init(void)
{
    const sh_device_profile_t *profile = get_configured_profile();
    ESP_LOGI(TAG, "Initializing smart-home node profile 0x%04x (%s)",
             profile->profile_id, profile->display_name);

    sh_node_callbacks_t callbacks = {
        .feature_changed_cb = feature_changed_cb,
        .connection_cb = connection_cb,
        .reset_requested_cb = reset_requested_cb,
        .profile_changed_cb = profile_changed_cb,
    };

    esp_err_t err = sh_node_init(profile, &callbacks);
    if (err != ESP_OK) {
        board_led_operation(LED_STATE_ERROR);
        return err;
    }

    board_led_operation(LED_STATE_PROV);
    return ESP_OK;
}

const sh_device_profile_t *ac_server_get_profile(void)
{
    return sh_node_get_profile();
}

esp_err_t ac_server_set_feature(uint16_t feature_id, int32_t value)
{
    return sh_node_set_feature(feature_id, value);
}

esp_err_t ac_server_get_feature(uint16_t feature_id, sh_feature_state_t *state)
{
    return sh_node_get_feature(feature_id, state);
}

esp_err_t ac_server_set_power(uint8_t power_state)
{
    return sh_node_set_feature(SH_FEATURE_ID_POWER, power_state);
}

esp_err_t ac_server_set_temperature(uint8_t temperature)
{
    return sh_node_set_feature(SH_FEATURE_ID_TEMPERATURE, temperature);
}

esp_err_t ac_server_set_mode(uint8_t mode)
{
    return sh_node_set_feature(SH_FEATURE_ID_MODE, mode);
}

esp_err_t ac_server_set_fan_speed(uint8_t fan_speed)
{
    return sh_node_set_feature(SH_FEATURE_ID_FAN_SPEED, fan_speed);
}

esp_err_t ac_server_set_all(uint8_t power, uint8_t temperature, uint8_t mode, uint8_t fan_speed)
{
    sh_feature_state_t states[] = {
        {.feature_id = SH_FEATURE_ID_POWER, .type = SH_FEATURE_TYPE_BOOL, .value = power},
        {.feature_id = SH_FEATURE_ID_TEMPERATURE, .type = SH_FEATURE_TYPE_INT, .value = temperature},
        {.feature_id = SH_FEATURE_ID_MODE, .type = SH_FEATURE_TYPE_ENUM, .value = mode},
        {.feature_id = SH_FEATURE_ID_FAN_SPEED, .type = SH_FEATURE_TYPE_ENUM, .value = fan_speed},
    };
    return sh_node_set_all(states, sizeof(states) / sizeof(states[0]));
}

uint8_t ac_server_get_current_power(void)
{
    return get_feature_u8(SH_FEATURE_ID_POWER, AC_POWER_OFF);
}

uint8_t ac_server_get_current_temperature(void)
{
    return get_feature_u8(SH_FEATURE_ID_TEMPERATURE, 25);
}

uint8_t ac_server_get_current_mode(void)
{
    return get_feature_u8(SH_FEATURE_ID_MODE, AC_MODE_COOL);
}

uint8_t ac_server_get_current_fan_speed(void)
{
    return get_feature_u8(SH_FEATURE_ID_FAN_SPEED, AC_FAN_SPEED_LOW);
}

esp_err_t ac_server_start_heartbeat(uint16_t client_addr)
{
    (void)client_addr;
    ui_update_connection_status(true);
    return ESP_OK;
}

esp_err_t ac_server_stop_heartbeat(void)
{
    ui_update_connection_status(false);
    return ESP_OK;
}

esp_err_t ac_server_send_heartbeat(void)
{
    return ESP_OK;
}

void ac_server_handle_heartbeat_timeout(void)
{
}

void ac_server_handle_heartbeat_ack(void)
{
}

bool ac_server_is_connected(void)
{
    return sh_node_is_connected();
}

uint16_t ac_server_get_client_addr(void)
{
    return sh_node_get_client_addr();
}

esp_err_t ac_server_nvs_init(void)
{
    return ESP_OK;
}

esp_err_t ac_server_save_state_to_flash(void)
{
    return sh_node_save_state();
}

esp_err_t ac_server_load_state_from_flash(void)
{
    return sh_node_load_state();
}

esp_err_t ac_server_clear_state_from_flash(void)
{
    return sh_node_clear_state();
}

uint16_t ac_server_get_device_addr(void)
{
    return sh_node_get_device_addr();
}
