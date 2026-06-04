/* ac_control.h - AC profile compatibility facade over generic smart-home mesh client */

#ifndef _AC_CONTROL_H_
#define _AC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_ble_mesh_defs.h"
#include "smarthome_controller.h"
#include "smarthome_profiles.h"

#define AC_ALL_DEVICE_ID        0xFF
#define AC_ALL_DEVICE_NAME      "All Device"

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
#define AC_GROUP_ADDR           SH_GROUP_ADDR_DEFAULT
#define MAX_AC_SERVERS          SH_CLIENT_MAX_DEVICES

typedef enum {
    AC_MSG_TYPE_SET_POWER,
    AC_MSG_TYPE_GET_POWER,
    AC_MSG_TYPE_SET_TEMPERATURE,
    AC_MSG_TYPE_GET_TEMPERATURE,
    AC_MSG_TYPE_SET_MODE,
    AC_MSG_TYPE_GET_MODE,
    AC_MSG_TYPE_SET_FAN_SPEED,
    AC_MSG_TYPE_GET_FAN_SPEED,
    AC_MSG_TYPE_HEARTBEAT_ACK,
    AC_MSG_TYPE_DISCONNECT_NOTIFY,
} ac_msg_type_t;

typedef struct {
    ac_msg_type_t msg_type;
    uint16_t server_addr;
    uint8_t value;
    uint32_t timestamp;
    uint8_t retry_count;
} ac_msg_queue_item_t;

typedef enum {
    AC_SEND_STATE_IDLE,
    AC_SEND_STATE_SENDING,
    AC_SEND_STATE_WAITING_ACK,
} ac_send_state_t;

typedef enum {
    AC_STATUS_POWER = 0,
    AC_STATUS_TEMPERATURE = 1,
    AC_STATUS_MODE = 2,
    AC_STATUS_FAN_SPEED = 3,
} ac_status_type_t;

typedef struct {
    uint16_t addr;
    bool is_online;
    bool is_configured;
    bool is_filtered;
    bool is_manually_disconnected;
    bool is_blacklisted;
    bool is_in_group;
    bool is_set_cmd_unresponsive;
    uint8_t power_state;
    uint8_t temperature;
    uint8_t mode;
    uint8_t fan_speed;
    char device_name[16];
    uint32_t last_update_time;
    const sh_device_profile_t *profile;
    sh_feature_state_t feature_states[SH_MODEL_MAX_FEATURES];
    uint8_t feature_state_count;
} ac_device_info_t;

typedef void (*ac_device_status_callback_t)(uint16_t device_addr, ac_status_type_t status_type, uint8_t value);
typedef void (*ac_device_online_callback_t)(uint16_t device_addr, bool is_online);
typedef void (*ac_device_provisioned_callback_t)(uint16_t device_addr);

ac_send_state_t ac_get_send_state(void);
uint8_t ac_get_queue_size(void);
void ac_clear_msg_queue(void);

esp_err_t ac_client_init(void);
void ac_client_register_callbacks(ac_device_status_callback_t status_cb,
                                  ac_device_online_callback_t online_cb,
                                  ac_device_provisioned_callback_t provisioned_cb);

uint8_t ac_get_device_count(void);
uint8_t ac_get_online_device_count(void);
uint8_t ac_get_device_list(ac_device_info_t *device_list, uint8_t max_devices);
esp_err_t ac_get_device_info_by_index(uint8_t index, ac_device_info_t *device_info);
esp_err_t ac_get_device_info_by_addr(uint16_t device_addr, ac_device_info_t *device_info);

esp_err_t ac_send_command_by_index(uint8_t device_index, ac_status_type_t command_type, uint8_t value);
esp_err_t ac_send_command_by_addr(uint16_t device_addr, ac_status_type_t command_type, uint8_t value);
esp_err_t ac_get_status_by_index(uint8_t device_index, ac_status_type_t status_type);
esp_err_t ac_get_status_by_addr(uint16_t device_addr, ac_status_type_t status_type);
esp_err_t ac_send_command_to_all_online(ac_status_type_t command_type, uint8_t value);
esp_err_t ac_refresh_all_device_status(void);
esp_err_t ac_set_device_name(uint16_t device_addr, const char *name);

esp_err_t ac_client_set_power(uint16_t server_addr, uint8_t power_state);
esp_err_t ac_client_get_power(uint16_t server_addr);
esp_err_t ac_client_set_temperature(uint16_t server_addr, uint8_t temperature);
esp_err_t ac_client_get_temperature(uint16_t server_addr);
esp_err_t ac_client_set_mode(uint16_t server_addr, uint8_t mode);
esp_err_t ac_client_get_mode(uint16_t server_addr);
esp_err_t ac_client_set_fan_speed(uint16_t server_addr, uint8_t fan_speed);
esp_err_t ac_client_get_fan_speed(uint16_t server_addr);

void ac_ble_mesh_store_info(void);
void ac_ble_mesh_restore_info(void);
void ac_add_server_addr(uint16_t addr);
uint8_t ac_get_num_servers(void);
uint16_t ac_get_server_addr_by_index(uint8_t index);
bool ac_is_server_online(uint16_t server_addr);
bool ac_is_device_set_cmd_responsive(uint16_t device_addr);

esp_err_t ac_disconnect_device(uint16_t device_addr);
esp_err_t ac_reconnect_device(uint16_t device_addr);
esp_err_t ac_add_device_to_filter(uint16_t device_addr);
esp_err_t ac_remove_device_from_filter(uint16_t device_addr);
bool ac_is_device_filtered(uint16_t device_addr);
bool ac_is_device_blacklisted(uint16_t device_addr);
esp_err_t ac_toggle_device_connection(uint16_t device_addr);

esp_err_t ac_add_device_to_group(uint16_t device_addr);
esp_err_t ac_remove_device_from_group(uint16_t device_addr);
esp_err_t ac_send_group_command(ac_status_type_t command_type, uint8_t value);
uint16_t ac_get_group_address(void);
bool ac_is_device_in_group(uint16_t device_addr);

esp_err_t ac_send_disconnect_notify(uint16_t device_addr);
esp_err_t ac_remove_device_completely(uint16_t device_addr);
esp_err_t ac_perform_key_refresh(void);
bool ac_is_key_refresh_in_progress(void);

uint16_t ac_status_to_feature_id(ac_status_type_t status_type);
bool ac_feature_to_status(uint16_t feature_id, ac_status_type_t *status_type);

#ifdef __cplusplus
}
#endif

#endif /* _AC_CONTROL_H_ */
