#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "smarthome_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH_BLE_BRIDGE_DEVICE_NAME_MAX 24

typedef struct {
    const char *device_name;
    bool enable_notifications;
} sh_ble_bridge_config_t;

typedef enum {
    SH_BLE_BRIDGE_CMD_SET_FEATURE = 0x01,
    SH_BLE_BRIDGE_CMD_GROUP_SET_FEATURE = 0x02,
    SH_BLE_BRIDGE_CMD_REFRESH_ALL = 0x03,
} sh_ble_bridge_cmd_t;

esp_err_t sh_ble_bridge_init(const sh_ble_bridge_config_t *config);
esp_err_t sh_ble_bridge_notify_device_status(uint16_t addr,
                                             uint16_t feature_id,
                                             sh_feature_type_t type,
                                             int32_t value);

#ifdef __cplusplus
}
#endif
