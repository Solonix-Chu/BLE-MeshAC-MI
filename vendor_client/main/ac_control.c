/* ac_control.c - AC profile compatibility facade over generic smart-home mesh client */

#include "ac_control.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "AC_FACADE";

static ac_device_status_callback_t s_status_cb;
static ac_device_online_callback_t s_online_cb;
static ac_device_provisioned_callback_t s_provisioned_cb;
static bool s_initialized;
static sh_client_device_t s_sh_device_list[MAX_AC_SERVERS];
static sh_client_device_t s_sh_device_tmp;

uint16_t ac_status_to_feature_id(ac_status_type_t status_type)
{
    switch (status_type) {
    case AC_STATUS_POWER:
        return SH_FEATURE_ID_POWER;
    case AC_STATUS_TEMPERATURE:
        return SH_FEATURE_ID_TEMPERATURE;
    case AC_STATUS_MODE:
        return SH_FEATURE_ID_MODE;
    case AC_STATUS_FAN_SPEED:
        return SH_FEATURE_ID_FAN_SPEED;
    default:
        return 0;
    }
}

bool ac_feature_to_status(uint16_t feature_id, ac_status_type_t *status_type)
{
    if (!status_type) {
        return false;
    }
    switch (feature_id) {
    case SH_FEATURE_ID_POWER:
        *status_type = AC_STATUS_POWER;
        return true;
    case SH_FEATURE_ID_TEMPERATURE:
        *status_type = AC_STATUS_TEMPERATURE;
        return true;
    case SH_FEATURE_ID_MODE:
        *status_type = AC_STATUS_MODE;
        return true;
    case SH_FEATURE_ID_FAN_SPEED:
        *status_type = AC_STATUS_FAN_SPEED;
        return true;
    default:
        return false;
    }
}

static int32_t get_feature_value(const sh_client_device_t *device, uint16_t feature_id, int32_t fallback)
{
    const sh_feature_state_t *state = sh_model_find_const_state(device->states,
                                                                device->state_count,
                                                                feature_id);
    return state ? state->value : fallback;
}

static void map_device_info(const sh_client_device_t *src, ac_device_info_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->addr = src->addr;
    dst->is_online = src->is_online;
    dst->is_configured = src->is_configured;
    dst->is_filtered = src->is_filtered;
    dst->is_manually_disconnected = src->is_manually_disconnected;
    dst->is_blacklisted = src->is_blacklisted;
    dst->is_in_group = src->is_in_group;
    dst->is_set_cmd_unresponsive = src->is_set_cmd_unresponsive;
    dst->power_state = (uint8_t)get_feature_value(src, SH_FEATURE_ID_POWER, AC_POWER_OFF);
    dst->temperature = (uint8_t)get_feature_value(src, SH_FEATURE_ID_TEMPERATURE, 25);
    dst->mode = (uint8_t)get_feature_value(src, SH_FEATURE_ID_MODE, AC_MODE_AUTO);
    dst->fan_speed = (uint8_t)get_feature_value(src, SH_FEATURE_ID_FAN_SPEED, AC_FAN_SPEED_LOW);
    strncpy(dst->device_name, src->device_name, sizeof(dst->device_name) - 1);
    dst->last_update_time = src->last_update_time;
    dst->profile = src->profile;
    dst->feature_state_count = (uint8_t)src->state_count;
    memcpy(dst->feature_states, src->states, sizeof(dst->feature_states));
}

static void sh_feature_status_cb(uint16_t device_addr,
                                 uint16_t feature_id,
                                 sh_feature_type_t type,
                                 int32_t value)
{
    (void)type;
    ac_status_type_t status_type;
    if (s_status_cb && ac_feature_to_status(feature_id, &status_type)) {
        s_status_cb(device_addr, status_type, (uint8_t)value);
    } else if (s_provisioned_cb) {
        s_provisioned_cb(device_addr);
    }
}

static void sh_online_cb(uint16_t device_addr, bool is_online)
{
    if (s_online_cb) {
        s_online_cb(device_addr, is_online);
    }
}

static void sh_provisioned_cb(uint16_t device_addr)
{
    if (s_provisioned_cb) {
        s_provisioned_cb(device_addr);
    }
}

static void sh_profile_cb(uint16_t device_addr, const sh_device_profile_t *profile)
{
    (void)profile;
    if (s_provisioned_cb) {
        s_provisioned_cb(device_addr);
    }
}

esp_err_t ac_client_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = sh_client_init();
    if (err == ESP_OK) {
        s_initialized = true;
    }
    return err;
}

void ac_client_register_callbacks(ac_device_status_callback_t status_cb,
                                  ac_device_online_callback_t online_cb,
                                  ac_device_provisioned_callback_t provisioned_cb)
{
    s_status_cb = status_cb;
    s_online_cb = online_cb;
    s_provisioned_cb = provisioned_cb;

    sh_client_callbacks_t callbacks = {
        .feature_status_cb = sh_feature_status_cb,
        .online_cb = sh_online_cb,
        .provisioned_cb = sh_provisioned_cb,
        .profile_cb = sh_profile_cb,
    };
    sh_client_register_callbacks(&callbacks);
}

uint8_t ac_get_device_count(void)
{
    return sh_client_get_device_count();
}

uint8_t ac_get_online_device_count(void)
{
    return sh_client_get_online_device_count();
}

uint8_t ac_get_device_list(ac_device_info_t *device_list, uint8_t max_devices)
{
    if (!device_list || max_devices == 0) {
        return 0;
    }

    uint8_t list_max = max_devices > MAX_AC_SERVERS ? MAX_AC_SERVERS : max_devices;
    uint8_t count = sh_client_get_device_list(s_sh_device_list, list_max);
    for (uint8_t i = 0; i < count; i++) {
        map_device_info(&s_sh_device_list[i], &device_list[i]);
    }
    return count;
}

esp_err_t ac_get_device_info_by_index(uint8_t index, ac_device_info_t *device_info)
{
    if (!device_info) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sh_client_get_device_by_index(index, &s_sh_device_tmp);
    if (err == ESP_OK) {
        map_device_info(&s_sh_device_tmp, device_info);
    }
    return err;
}

esp_err_t ac_get_device_info_by_addr(uint16_t device_addr, ac_device_info_t *device_info)
{
    if (!device_info) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = sh_client_get_device_by_addr(device_addr, &s_sh_device_tmp);
    if (err == ESP_OK) {
        map_device_info(&s_sh_device_tmp, device_info);
    }
    return err;
}

esp_err_t ac_send_command_by_index(uint8_t device_index, ac_status_type_t command_type, uint8_t value)
{
    uint16_t addr = ac_get_server_addr_by_index(device_index);
    if (addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return ESP_ERR_INVALID_ARG;
    }
    return ac_send_command_by_addr(addr, command_type, value);
}

esp_err_t ac_send_command_by_addr(uint16_t device_addr, ac_status_type_t command_type, uint8_t value)
{
    uint16_t feature_id = ac_status_to_feature_id(command_type);
    if (!feature_id) {
        return ESP_ERR_INVALID_ARG;
    }
    return sh_client_set_feature(device_addr, feature_id, value);
}

esp_err_t ac_get_status_by_index(uint8_t device_index, ac_status_type_t status_type)
{
    uint16_t addr = ac_get_server_addr_by_index(device_index);
    if (addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        return ESP_ERR_INVALID_ARG;
    }
    return ac_get_status_by_addr(addr, status_type);
}

esp_err_t ac_get_status_by_addr(uint16_t device_addr, ac_status_type_t status_type)
{
    uint16_t feature_id = ac_status_to_feature_id(status_type);
    if (!feature_id) {
        return ESP_ERR_INVALID_ARG;
    }
    return sh_client_get_feature(device_addr, feature_id);
}

esp_err_t ac_send_command_to_all_online(ac_status_type_t command_type, uint8_t value)
{
    uint16_t feature_id = ac_status_to_feature_id(command_type);
    if (!feature_id) {
        return ESP_ERR_INVALID_ARG;
    }
    return sh_client_group_set_feature(feature_id, value);
}

esp_err_t ac_refresh_all_device_status(void)
{
    return sh_client_refresh_all();
}

esp_err_t ac_set_device_name(uint16_t device_addr, const char *name)
{
    (void)device_addr;
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ac_client_set_power(uint16_t server_addr, uint8_t power_state)
{
    return sh_client_set_feature(server_addr, SH_FEATURE_ID_POWER, power_state);
}

esp_err_t ac_client_get_power(uint16_t server_addr)
{
    return sh_client_get_feature(server_addr, SH_FEATURE_ID_POWER);
}

esp_err_t ac_client_set_temperature(uint16_t server_addr, uint8_t temperature)
{
    return sh_client_set_feature(server_addr, SH_FEATURE_ID_TEMPERATURE, temperature);
}

esp_err_t ac_client_get_temperature(uint16_t server_addr)
{
    return sh_client_get_feature(server_addr, SH_FEATURE_ID_TEMPERATURE);
}

esp_err_t ac_client_set_mode(uint16_t server_addr, uint8_t mode)
{
    return sh_client_set_feature(server_addr, SH_FEATURE_ID_MODE, mode);
}

esp_err_t ac_client_get_mode(uint16_t server_addr)
{
    return sh_client_get_feature(server_addr, SH_FEATURE_ID_MODE);
}

esp_err_t ac_client_set_fan_speed(uint16_t server_addr, uint8_t fan_speed)
{
    return sh_client_set_feature(server_addr, SH_FEATURE_ID_FAN_SPEED, fan_speed);
}

esp_err_t ac_client_get_fan_speed(uint16_t server_addr)
{
    return sh_client_get_feature(server_addr, SH_FEATURE_ID_FAN_SPEED);
}

void ac_ble_mesh_store_info(void)
{
}

void ac_ble_mesh_restore_info(void)
{
}

void ac_add_server_addr(uint16_t addr)
{
    (void)addr;
    ESP_LOGW(TAG, "ac_add_server_addr is deprecated; devices are managed by sh_client");
}

uint8_t ac_get_num_servers(void)
{
    return sh_client_get_device_count();
}

uint16_t ac_get_server_addr_by_index(uint8_t index)
{
    return sh_client_get_device_addr_by_index(index);
}

bool ac_is_server_online(uint16_t server_addr)
{
    return sh_client_is_device_online(server_addr);
}

bool ac_is_device_set_cmd_responsive(uint16_t device_addr)
{
    return sh_client_is_device_set_cmd_responsive(device_addr);
}

esp_err_t ac_disconnect_device(uint16_t device_addr)
{
    return sh_client_disconnect_device(device_addr);
}

esp_err_t ac_reconnect_device(uint16_t device_addr)
{
    return sh_client_reconnect_device(device_addr);
}

esp_err_t ac_add_device_to_filter(uint16_t device_addr)
{
    return sh_client_add_device_to_filter(device_addr);
}

esp_err_t ac_remove_device_from_filter(uint16_t device_addr)
{
    return sh_client_remove_device_from_filter(device_addr);
}

bool ac_is_device_filtered(uint16_t device_addr)
{
    return sh_client_is_device_filtered(device_addr);
}

bool ac_is_device_blacklisted(uint16_t device_addr)
{
    return sh_client_is_device_blacklisted(device_addr);
}

esp_err_t ac_toggle_device_connection(uint16_t device_addr)
{
    return sh_client_toggle_device_connection(device_addr);
}

esp_err_t ac_add_device_to_group(uint16_t device_addr)
{
    return sh_client_add_device_to_group(device_addr);
}

esp_err_t ac_remove_device_from_group(uint16_t device_addr)
{
    return sh_client_remove_device_from_group(device_addr);
}

esp_err_t ac_send_group_command(ac_status_type_t command_type, uint8_t value)
{
    uint16_t feature_id = ac_status_to_feature_id(command_type);
    if (!feature_id) {
        return ESP_ERR_INVALID_ARG;
    }
    return sh_client_group_set_feature(feature_id, value);
}

uint16_t ac_get_group_address(void)
{
    return sh_client_get_group_address();
}

bool ac_is_device_in_group(uint16_t device_addr)
{
    return sh_client_is_device_in_group(device_addr);
}

esp_err_t ac_send_disconnect_notify(uint16_t device_addr)
{
    return sh_client_send_disconnect_notify(device_addr);
}

esp_err_t ac_remove_device_completely(uint16_t device_addr)
{
    return sh_client_remove_device_completely(device_addr);
}

esp_err_t ac_perform_key_refresh(void)
{
    return sh_client_perform_key_refresh();
}

bool ac_is_key_refresh_in_progress(void)
{
    return sh_client_is_key_refresh_in_progress();
}

ac_send_state_t ac_get_send_state(void)
{
    switch (sh_client_get_send_state()) {
    case SH_SEND_STATE_SENDING:
        return AC_SEND_STATE_SENDING;
    case SH_SEND_STATE_IDLE:
    default:
        return AC_SEND_STATE_IDLE;
    }
}

uint8_t ac_get_queue_size(void)
{
    return sh_client_get_queue_size();
}

void ac_clear_msg_queue(void)
{
    sh_client_clear_queue();
}
