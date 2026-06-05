#include "smarthome_controller.h"

#include <string.h>
#include "esp_log.h"
#include "smarthome_remote.h"

#define TAG "SH_CONTROLLER"

static sh_controller_role_t s_role = SH_CONTROLLER_ROLE_PROVISIONER_CONTROLLER;
static sh_controller_callbacks_t s_callbacks;

static void client_feature_status_cb(uint16_t device_addr,
                                     uint16_t feature_id,
                                     sh_feature_type_t type,
                                     int32_t value)
{
    if (s_callbacks.feature_status_cb) {
        s_callbacks.feature_status_cb(device_addr, feature_id, type, value);
    }
}

static void client_online_cb(uint16_t device_addr, bool is_online)
{
    if (s_callbacks.online_cb) {
        s_callbacks.online_cb(device_addr, is_online);
    }
}

static void client_provisioned_cb(uint16_t device_addr)
{
    if (s_callbacks.provisioned_cb) {
        s_callbacks.provisioned_cb(device_addr);
    }
}

static void client_profile_cb(uint16_t device_addr, const sh_device_profile_t *profile)
{
    if (s_callbacks.profile_cb) {
        s_callbacks.profile_cb(device_addr, profile);
    }
}

static void register_client_callbacks(void)
{
    sh_client_callbacks_t callbacks = {
        .feature_status_cb = client_feature_status_cb,
        .online_cb = client_online_cb,
        .provisioned_cb = client_provisioned_cb,
        .profile_cb = client_profile_cb,
    };
    if (s_role == SH_CONTROLLER_ROLE_PROVISIONER_CONTROLLER) {
        sh_client_register_callbacks(&callbacks);
    } else {
        sh_remote_register_callbacks(&callbacks);
    }
}

esp_err_t sh_controller_init(sh_controller_role_t role, const sh_controller_callbacks_t *callbacks)
{
    s_role = role;
    sh_controller_register_callbacks(callbacks);

    esp_err_t err = (role == SH_CONTROLLER_ROLE_PROVISIONER_CONTROLLER) ?
        sh_client_init() : sh_remote_init();
    if (err == ESP_OK) {
        register_client_callbacks();
        ESP_LOGI(TAG, "Controller initialized role=%d", role);
    }
    return err;
}

void sh_controller_register_callbacks(const sh_controller_callbacks_t *callbacks)
{
    if (callbacks) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }
    register_client_callbacks();
}

sh_controller_role_t sh_controller_get_role(void)
{
    return s_role;
}

static bool use_client_backend(void)
{
    return s_role == SH_CONTROLLER_ROLE_PROVISIONER_CONTROLLER;
}

uint8_t sh_controller_get_device_count(void) { return use_client_backend() ? sh_client_get_device_count() : sh_remote_get_device_count(); }
uint8_t sh_controller_get_online_device_count(void) { return use_client_backend() ? sh_client_get_online_device_count() : sh_remote_get_online_device_count(); }
uint8_t sh_controller_get_device_list(sh_controller_device_t *devices, uint8_t max_devices) { return use_client_backend() ? sh_client_get_device_list(devices, max_devices) : sh_remote_get_device_list(devices, max_devices); }
esp_err_t sh_controller_get_device_by_index(uint8_t index, sh_controller_device_t *device) { return use_client_backend() ? sh_client_get_device_by_index(index, device) : sh_remote_get_device_by_index(index, device); }
esp_err_t sh_controller_get_device_by_addr(uint16_t addr, sh_controller_device_t *device) { return use_client_backend() ? sh_client_get_device_by_addr(addr, device) : sh_remote_get_device_by_addr(addr, device); }
uint16_t sh_controller_get_device_addr_by_index(uint8_t index) { return use_client_backend() ? sh_client_get_device_addr_by_index(index) : sh_remote_get_device_addr_by_index(index); }
bool sh_controller_is_device_online(uint16_t addr) { return use_client_backend() ? sh_client_is_device_online(addr) : sh_remote_is_device_online(addr); }
bool sh_controller_is_device_set_cmd_responsive(uint16_t addr) { return use_client_backend() ? sh_client_is_device_set_cmd_responsive(addr) : sh_remote_is_device_set_cmd_responsive(addr); }
esp_err_t sh_controller_get_profile(uint16_t addr) { return use_client_backend() ? sh_client_get_profile(addr) : sh_remote_get_profile(addr); }
esp_err_t sh_controller_set_profile(uint16_t addr, const sh_device_profile_t *profile) { return use_client_backend() ? sh_client_set_profile(addr, profile) : sh_remote_set_profile(addr, profile); }
esp_err_t sh_controller_get_feature(uint16_t addr, uint16_t feature_id) { return use_client_backend() ? sh_client_get_feature(addr, feature_id) : sh_remote_get_feature(addr, feature_id); }
esp_err_t sh_controller_set_feature(uint16_t addr, uint16_t feature_id, int32_t value) { return use_client_backend() ? sh_client_set_feature(addr, feature_id, value) : sh_remote_set_feature(addr, feature_id, value); }
esp_err_t sh_controller_group_set_feature(uint16_t feature_id, int32_t value) { return use_client_backend() ? sh_client_group_set_feature(feature_id, value) : sh_remote_group_set_feature(feature_id, value); }
esp_err_t sh_controller_refresh_device(uint16_t addr) { return use_client_backend() ? sh_client_refresh_device(addr) : sh_remote_refresh_device(addr); }
esp_err_t sh_controller_refresh_all(void) { return use_client_backend() ? sh_client_refresh_all() : sh_remote_refresh_all(); }
esp_err_t sh_controller_add_device_to_group(uint16_t addr) { return use_client_backend() ? sh_client_add_device_to_group(addr) : ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_controller_remove_device_from_group(uint16_t addr) { return use_client_backend() ? sh_client_remove_device_from_group(addr) : ESP_ERR_NOT_SUPPORTED; }
uint16_t sh_controller_get_group_address(void) { return use_client_backend() ? sh_client_get_group_address() : sh_remote_get_group_address(); }
bool sh_controller_is_device_in_group(uint16_t addr) { return use_client_backend() ? sh_client_is_device_in_group(addr) : sh_remote_is_device_in_group(addr); }
esp_err_t sh_controller_disconnect_device(uint16_t addr) { return use_client_backend() ? sh_client_disconnect_device(addr) : ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_controller_reconnect_device(uint16_t addr) { return use_client_backend() ? sh_client_reconnect_device(addr) : ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_controller_add_device_to_filter(uint16_t addr) { return use_client_backend() ? sh_client_add_device_to_filter(addr) : ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_controller_remove_device_from_filter(uint16_t addr) { return use_client_backend() ? sh_client_remove_device_from_filter(addr) : ESP_ERR_NOT_SUPPORTED; }
bool sh_controller_is_device_filtered(uint16_t addr) { return use_client_backend() ? sh_client_is_device_filtered(addr) : false; }
bool sh_controller_is_device_blacklisted(uint16_t addr) { return use_client_backend() ? sh_client_is_device_blacklisted(addr) : false; }
esp_err_t sh_controller_toggle_device_connection(uint16_t addr) { return use_client_backend() ? sh_client_toggle_device_connection(addr) : ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_controller_send_disconnect_notify(uint16_t addr) { return use_client_backend() ? sh_client_send_disconnect_notify(addr) : ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_controller_remove_device_completely(uint16_t addr) { return use_client_backend() ? sh_client_remove_device_completely(addr) : ESP_ERR_NOT_SUPPORTED; }
esp_err_t sh_controller_perform_key_refresh(void) { return use_client_backend() ? sh_client_perform_key_refresh() : ESP_ERR_NOT_SUPPORTED; }
bool sh_controller_is_key_refresh_in_progress(void) { return use_client_backend() ? sh_client_is_key_refresh_in_progress() : false; }
uint8_t sh_controller_get_queue_size(void) { return use_client_backend() ? sh_client_get_queue_size() : 0; }
sh_send_state_t sh_controller_get_send_state(void) { return use_client_backend() ? sh_client_get_send_state() : SH_SEND_STATE_IDLE; }
void sh_controller_clear_queue(void) { if (use_client_backend()) { sh_client_clear_queue(); } }
