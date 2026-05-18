#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "smarthome_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sh_node_feature_changed_cb_t)(uint16_t feature_id,
                                             sh_feature_type_t type,
                                             int32_t value,
                                             void *user_data);
typedef void (*sh_node_connection_cb_t)(bool is_connected, uint16_t client_addr, void *user_data);
typedef void (*sh_node_reset_requested_cb_t)(void *user_data);
typedef void (*sh_node_profile_changed_cb_t)(const sh_device_profile_t *profile, void *user_data);

typedef struct {
    sh_node_feature_changed_cb_t feature_changed_cb;
    sh_node_connection_cb_t connection_cb;
    sh_node_reset_requested_cb_t reset_requested_cb;
    sh_node_profile_changed_cb_t profile_changed_cb;
    void *user_data;
} sh_node_callbacks_t;

esp_err_t sh_node_init(const sh_device_profile_t *profile, const sh_node_callbacks_t *callbacks);
esp_err_t sh_node_set_feature(uint16_t feature_id, int32_t value);
esp_err_t sh_node_get_feature(uint16_t feature_id, sh_feature_state_t *state);
esp_err_t sh_node_set_all(const sh_feature_state_t *states, size_t state_count);
uint16_t sh_node_get_device_addr(void);
uint16_t sh_node_get_client_addr(void);
bool sh_node_is_connected(void);
const sh_device_profile_t *sh_node_get_profile(void);
esp_err_t sh_node_save_state(void);
esp_err_t sh_node_load_state(void);
esp_err_t sh_node_clear_state(void);

#ifdef __cplusplus
}
#endif
