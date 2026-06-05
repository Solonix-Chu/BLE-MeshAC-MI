#pragma once

#include "smarthome_client.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sh_remote_init(void);
void sh_remote_register_callbacks(const sh_client_callbacks_t *callbacks);

uint8_t sh_remote_get_device_count(void);
uint8_t sh_remote_get_online_device_count(void);
uint8_t sh_remote_get_device_list(sh_client_device_t *devices, uint8_t max_devices);
esp_err_t sh_remote_get_device_by_index(uint8_t index, sh_client_device_t *device);
esp_err_t sh_remote_get_device_by_addr(uint16_t addr, sh_client_device_t *device);
uint16_t sh_remote_get_device_addr_by_index(uint8_t index);
bool sh_remote_is_device_online(uint16_t addr);
bool sh_remote_is_device_set_cmd_responsive(uint16_t addr);

esp_err_t sh_remote_get_profile(uint16_t addr);
esp_err_t sh_remote_set_profile(uint16_t addr, const sh_device_profile_t *profile);
esp_err_t sh_remote_get_feature(uint16_t addr, uint16_t feature_id);
esp_err_t sh_remote_set_feature(uint16_t addr, uint16_t feature_id, int32_t value);
esp_err_t sh_remote_group_set_feature(uint16_t feature_id, int32_t value);
esp_err_t sh_remote_refresh_device(uint16_t addr);
esp_err_t sh_remote_refresh_all(void);

uint16_t sh_remote_get_group_address(void);
bool sh_remote_is_device_in_group(uint16_t addr);

#ifdef __cplusplus
}
#endif
