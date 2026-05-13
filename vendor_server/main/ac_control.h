/* ac_control.h - Compatibility facade over generic smart-home mesh node */

#ifndef _AC_CONTROL_H_
#define _AC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "smarthome_node.h"
#include "smarthome_profiles.h"

#define AC_POWER_OFF            SH_AC_POWER_OFF
#define AC_POWER_ON             SH_AC_POWER_ON
#define AC_TEMP_MIN             SH_AC_TEMP_MIN
#define AC_TEMP_MAX             SH_AC_TEMP_MAX
#define AC_MODE_COOL            SH_AC_MODE_COOL
#define AC_MODE_HEAT            SH_AC_MODE_HEAT
#define AC_MODE_FAN             SH_AC_MODE_FAN
#define AC_MODE_DRY             SH_AC_MODE_DRY
#define AC_MODE_AUTO            SH_AC_MODE_AUTO
#define AC_FAN_SPEED_LOW        SH_AC_FAN_LOW
#define AC_FAN_SPEED_MEDIUM     SH_AC_FAN_MEDIUM
#define AC_FAN_SPEED_HIGH       SH_AC_FAN_HIGH

typedef enum {
    AC_STATUS_POWER = 0,
    AC_STATUS_TEMPERATURE = 1,
    AC_STATUS_MODE = 2,
    AC_STATUS_FAN_SPEED = 3,
} ac_status_type_t;

struct esp_ble_mesh_key {
    uint16_t net_idx;
    uint16_t app_idx;
    uint8_t app_key[16];
};

typedef void (*ac_status_callback_t)(uint8_t value);

esp_err_t ac_server_init(void);
const sh_device_profile_t *ac_server_get_profile(void);
esp_err_t ac_server_set_feature(uint16_t feature_id, int32_t value);
esp_err_t ac_server_get_feature(uint16_t feature_id, sh_feature_state_t *state);
esp_err_t ac_server_set_power(uint8_t power_state);
esp_err_t ac_server_set_temperature(uint8_t temperature);
esp_err_t ac_server_set_mode(uint8_t mode);
esp_err_t ac_server_set_fan_speed(uint8_t fan_speed);
esp_err_t ac_server_set_all(uint8_t power, uint8_t temperature, uint8_t mode, uint8_t fan_speed);

uint8_t ac_server_get_current_power(void);
uint8_t ac_server_get_current_temperature(void);
uint8_t ac_server_get_current_mode(void);
uint8_t ac_server_get_current_fan_speed(void);

esp_err_t ac_server_start_heartbeat(uint16_t client_addr);
esp_err_t ac_server_stop_heartbeat(void);
esp_err_t ac_server_send_heartbeat(void);
void ac_server_handle_heartbeat_timeout(void);
void ac_server_handle_heartbeat_ack(void);
bool ac_server_is_connected(void);
uint16_t ac_server_get_client_addr(void);

esp_err_t ac_server_nvs_init(void);
esp_err_t ac_server_save_state_to_flash(void);
esp_err_t ac_server_load_state_from_flash(void);
esp_err_t ac_server_clear_state_from_flash(void);
uint16_t ac_server_get_device_addr(void);

#ifdef __cplusplus
}
#endif

#endif /* _AC_CONTROL_H_ */
