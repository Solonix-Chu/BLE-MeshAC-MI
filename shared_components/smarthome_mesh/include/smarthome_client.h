#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "smarthome_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH_CLIENT_MAX_DEVICES        10
#define SH_CLIENT_DEVICE_NAME_MAX    24

typedef enum {
    SH_SEND_STATE_IDLE = 0,
    SH_SEND_STATE_SENDING,
} sh_send_state_t;

typedef struct {
    uint16_t addr;
    bool is_online;
    bool is_configured;
    bool is_filtered;
    bool is_manually_disconnected;
    bool is_blacklisted;
    bool is_in_group;
    bool profile_loaded;
    bool is_set_cmd_unresponsive;
    char device_name[SH_CLIENT_DEVICE_NAME_MAX];
    const sh_device_profile_t *profile;
    sh_feature_state_t states[SH_MODEL_MAX_FEATURES];
    size_t state_count;
    uint32_t last_update_time;
} sh_client_device_t;

typedef void (*sh_client_feature_status_cb_t)(uint16_t device_addr,
                                              uint16_t feature_id,
                                              sh_feature_type_t type,
                                              int32_t value);
typedef void (*sh_client_online_cb_t)(uint16_t device_addr, bool is_online);
typedef void (*sh_client_provisioned_cb_t)(uint16_t device_addr);
typedef void (*sh_client_profile_cb_t)(uint16_t device_addr, const sh_device_profile_t *profile);

typedef struct {
    sh_client_feature_status_cb_t feature_status_cb;
    sh_client_online_cb_t online_cb;
    sh_client_provisioned_cb_t provisioned_cb;
    sh_client_profile_cb_t profile_cb;
} sh_client_callbacks_t;

esp_err_t sh_client_init(void);
void sh_client_register_callbacks(const sh_client_callbacks_t *callbacks);

uint8_t sh_client_get_device_count(void);
uint8_t sh_client_get_online_device_count(void);
uint8_t sh_client_get_device_list(sh_client_device_t *devices, uint8_t max_devices);
esp_err_t sh_client_get_device_by_index(uint8_t index, sh_client_device_t *device);
esp_err_t sh_client_get_device_by_addr(uint16_t addr, sh_client_device_t *device);
uint16_t sh_client_get_device_addr_by_index(uint8_t index);
bool sh_client_is_device_online(uint16_t addr);
bool sh_client_is_device_set_cmd_responsive(uint16_t addr);

esp_err_t sh_client_get_profile(uint16_t addr);
esp_err_t sh_client_set_profile(uint16_t addr, const sh_device_profile_t *profile);
esp_err_t sh_client_get_feature(uint16_t addr, uint16_t feature_id);
esp_err_t sh_client_set_feature(uint16_t addr, uint16_t feature_id, int32_t value);
esp_err_t sh_client_group_set_feature(uint16_t feature_id, int32_t value);
esp_err_t sh_client_refresh_device(uint16_t addr);
esp_err_t sh_client_refresh_all(void);

esp_err_t sh_client_add_device_to_group(uint16_t addr);
esp_err_t sh_client_remove_device_from_group(uint16_t addr);
uint16_t sh_client_get_group_address(void);
bool sh_client_is_device_in_group(uint16_t addr);

esp_err_t sh_client_disconnect_device(uint16_t addr);
esp_err_t sh_client_reconnect_device(uint16_t addr);
esp_err_t sh_client_add_device_to_filter(uint16_t addr);
esp_err_t sh_client_remove_device_from_filter(uint16_t addr);
bool sh_client_is_device_filtered(uint16_t addr);
bool sh_client_is_device_blacklisted(uint16_t addr);
esp_err_t sh_client_toggle_device_connection(uint16_t addr);
esp_err_t sh_client_send_disconnect_notify(uint16_t addr);
esp_err_t sh_client_remove_device_completely(uint16_t addr);
esp_err_t sh_client_perform_key_refresh(void);
bool sh_client_is_key_refresh_in_progress(void);

uint8_t sh_client_get_queue_size(void);
sh_send_state_t sh_client_get_send_state(void);
void sh_client_clear_queue(void);

#ifdef __cplusplus
}
#endif
