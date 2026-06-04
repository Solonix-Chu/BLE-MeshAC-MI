#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "smarthome_client.h"
#include "smarthome_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SH_CONTROLLER_ROLE_PROVISIONER_CONTROLLER = 0,
    SH_CONTROLLER_ROLE_REMOTE_CONTROLLER,
    SH_CONTROLLER_ROLE_GATEWAY_RELAY,
} sh_controller_role_t;

typedef sh_client_device_t sh_controller_device_t;

typedef void (*sh_controller_feature_status_cb_t)(uint16_t device_addr,
                                                  uint16_t feature_id,
                                                  sh_feature_type_t type,
                                                  int32_t value);
typedef void (*sh_controller_online_cb_t)(uint16_t device_addr, bool is_online);
typedef void (*sh_controller_provisioned_cb_t)(uint16_t device_addr);
typedef void (*sh_controller_profile_cb_t)(uint16_t device_addr, const sh_device_profile_t *profile);

typedef struct {
    sh_controller_feature_status_cb_t feature_status_cb;
    sh_controller_online_cb_t online_cb;
    sh_controller_provisioned_cb_t provisioned_cb;
    sh_controller_profile_cb_t profile_cb;
} sh_controller_callbacks_t;

esp_err_t sh_controller_init(sh_controller_role_t role, const sh_controller_callbacks_t *callbacks);
void sh_controller_register_callbacks(const sh_controller_callbacks_t *callbacks);
sh_controller_role_t sh_controller_get_role(void);

uint8_t sh_controller_get_device_count(void);
uint8_t sh_controller_get_online_device_count(void);
uint8_t sh_controller_get_device_list(sh_controller_device_t *devices, uint8_t max_devices);
esp_err_t sh_controller_get_device_by_index(uint8_t index, sh_controller_device_t *device);
esp_err_t sh_controller_get_device_by_addr(uint16_t addr, sh_controller_device_t *device);
uint16_t sh_controller_get_device_addr_by_index(uint8_t index);
bool sh_controller_is_device_online(uint16_t addr);
bool sh_controller_is_device_set_cmd_responsive(uint16_t addr);

esp_err_t sh_controller_get_profile(uint16_t addr);
esp_err_t sh_controller_set_profile(uint16_t addr, const sh_device_profile_t *profile);
esp_err_t sh_controller_get_feature(uint16_t addr, uint16_t feature_id);
esp_err_t sh_controller_set_feature(uint16_t addr, uint16_t feature_id, int32_t value);
esp_err_t sh_controller_group_set_feature(uint16_t feature_id, int32_t value);
esp_err_t sh_controller_refresh_device(uint16_t addr);
esp_err_t sh_controller_refresh_all(void);

esp_err_t sh_controller_add_device_to_group(uint16_t addr);
esp_err_t sh_controller_remove_device_from_group(uint16_t addr);
uint16_t sh_controller_get_group_address(void);
bool sh_controller_is_device_in_group(uint16_t addr);

esp_err_t sh_controller_disconnect_device(uint16_t addr);
esp_err_t sh_controller_reconnect_device(uint16_t addr);
esp_err_t sh_controller_add_device_to_filter(uint16_t addr);
esp_err_t sh_controller_remove_device_from_filter(uint16_t addr);
bool sh_controller_is_device_filtered(uint16_t addr);
bool sh_controller_is_device_blacklisted(uint16_t addr);
esp_err_t sh_controller_toggle_device_connection(uint16_t addr);
esp_err_t sh_controller_send_disconnect_notify(uint16_t addr);
esp_err_t sh_controller_remove_device_completely(uint16_t addr);
esp_err_t sh_controller_perform_key_refresh(void);
bool sh_controller_is_key_refresh_in_progress(void);

uint8_t sh_controller_get_queue_size(void);
sh_send_state_t sh_controller_get_send_state(void);
void sh_controller_clear_queue(void);

#ifdef __cplusplus
}
#endif
