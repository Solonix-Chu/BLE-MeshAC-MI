/* ac_control.h - Air Conditioner Bluetooth Mesh Client Control Interface */

#ifndef _MESH_COMMON_H_
#define _MESH_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_ble_mesh_defs.h"

/* 厂商ID */
#define MY_COMPANY_ID     0x02E5

/* 模型ID */
#define MY_MODEL_ID_AC_SERVER    0x0002
#define MY_MODEL_ID_AC_CLIENT    0x0001

/* AC Operation Codes - Power Control */
#define AC_OP_SET_POWER         ESP_BLE_MESH_MODEL_OP_3(0x00, MY_COMPANY_ID)
#define AC_OP_GET_POWER         ESP_BLE_MESH_MODEL_OP_3(0x01, MY_COMPANY_ID)
#define AC_OP_POWER_STATUS      ESP_BLE_MESH_MODEL_OP_3(0x02, MY_COMPANY_ID)

/* AC Operation Codes - Temperature Control */
#define AC_OP_SET_TEMPERATURE   ESP_BLE_MESH_MODEL_OP_3(0x03, MY_COMPANY_ID)
#define AC_OP_GET_TEMPERATURE   ESP_BLE_MESH_MODEL_OP_3(0x04, MY_COMPANY_ID)
#define AC_OP_TEMPERATURE_STATUS ESP_BLE_MESH_MODEL_OP_3(0x05, MY_COMPANY_ID)

/* AC Operation Codes - Mode Control */
#define AC_OP_SET_MODE          ESP_BLE_MESH_MODEL_OP_3(0x06, MY_COMPANY_ID)
#define AC_OP_GET_MODE          ESP_BLE_MESH_MODEL_OP_3(0x07, MY_COMPANY_ID)
#define AC_OP_MODE_STATUS       ESP_BLE_MESH_MODEL_OP_3(0x08, MY_COMPANY_ID)

/* AC Operation Codes - Fan Speed Control */
#define AC_OP_SET_FAN_SPEED     ESP_BLE_MESH_MODEL_OP_3(0x09, MY_COMPANY_ID)
#define AC_OP_GET_FAN_SPEED     ESP_BLE_MESH_MODEL_OP_3(0x0A, MY_COMPANY_ID)
#define AC_OP_FAN_SPEED_STATUS  ESP_BLE_MESH_MODEL_OP_3(0x0B, MY_COMPANY_ID)

/* AC Operation Codes - Heartbeat */
#define AC_OP_HEARTBEAT         ESP_BLE_MESH_MODEL_OP_3(0x0C, MY_COMPANY_ID)
#define AC_OP_HEARTBEAT_ACK     ESP_BLE_MESH_MODEL_OP_3(0x0D, MY_COMPANY_ID)

/* AC Mode Definitions */
#define AC_MODE_COOL            0x00
#define AC_MODE_HEAT            0x01
#define AC_MODE_FAN             0x02
#define AC_MODE_DRY             0x03
#define AC_MODE_AUTO            0x04

/* AC Fan Speed Definitions */
#define AC_FAN_SPEED_LOW        0x00
#define AC_FAN_SPEED_MEDIUM     0x01
#define AC_FAN_SPEED_HIGH       0x02

/* AC Power State */
#define AC_POWER_OFF            0x00
#define AC_POWER_ON             0x01

/* 温度范围 */
#define AC_TEMP_MIN             16
#define AC_TEMP_MAX             30

/* 组播地址定义 */
#define AC_GROUP_ADDR           0xC000     /* 空调设备组播地址 */

/* Maximum number of AC server devices that can be managed */
#define MAX_AC_SERVERS 10

#ifdef __cplusplus
}
#endif

#endif /* _MESH_COMMON_H_ */ 