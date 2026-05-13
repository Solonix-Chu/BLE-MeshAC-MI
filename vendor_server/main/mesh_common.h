/* mesh_common.h - compatibility aliases for the generic smart-home protocol */

#ifndef _MESH_COMMON_H_
#define _MESH_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "smarthome_protocol.h"
#include "smarthome_profiles.h"

#define AC_ENABLE_HEARTBEAT     0
#define MY_COMPANY_ID           SH_COMPANY_ID
#define MY_MODEL_ID_AC_SERVER   SH_MODEL_ID_NODE
#define MY_MODEL_ID_AC_CLIENT   SH_MODEL_ID_CLIENT

#define AC_OP_SET_POWER         SH_OP_FEATURE_SET
#define AC_OP_GET_POWER         SH_OP_FEATURE_GET
#define AC_OP_POWER_STATUS      SH_OP_FEATURE_STATUS
#define AC_OP_SET_TEMPERATURE   SH_OP_FEATURE_SET
#define AC_OP_GET_TEMPERATURE   SH_OP_FEATURE_GET
#define AC_OP_TEMPERATURE_STATUS SH_OP_FEATURE_STATUS
#define AC_OP_SET_MODE          SH_OP_FEATURE_SET
#define AC_OP_GET_MODE          SH_OP_FEATURE_GET
#define AC_OP_MODE_STATUS       SH_OP_FEATURE_STATUS
#define AC_OP_SET_FAN_SPEED     SH_OP_FEATURE_SET
#define AC_OP_GET_FAN_SPEED     SH_OP_FEATURE_GET
#define AC_OP_FAN_SPEED_STATUS  SH_OP_FEATURE_STATUS
#define AC_OP_HEARTBEAT         SH_OP_NODE_EVENT
#define AC_OP_HEARTBEAT_ACK     SH_OP_NODE_EVENT
#define AC_OP_DISCONNECT_NOTIFY SH_OP_DISCONNECT_NOTIFY
#define AC_OP_DISCONNECT_ACK    SH_OP_DISCONNECT_ACK

#define AC_MODE_COOL            SH_AC_MODE_COOL
#define AC_MODE_HEAT            SH_AC_MODE_HEAT
#define AC_MODE_FAN             SH_AC_MODE_FAN
#define AC_MODE_DRY             SH_AC_MODE_DRY
#define AC_MODE_AUTO            SH_AC_MODE_AUTO
#define AC_FAN_SPEED_LOW        SH_AC_FAN_LOW
#define AC_FAN_SPEED_MEDIUM     SH_AC_FAN_MEDIUM
#define AC_FAN_SPEED_HIGH       SH_AC_FAN_HIGH
#define AC_POWER_OFF            SH_AC_POWER_OFF
#define AC_POWER_ON             SH_AC_POWER_ON
#define AC_TEMP_MIN             SH_AC_TEMP_MIN
#define AC_TEMP_MAX             SH_AC_TEMP_MAX
#define AC_GROUP_ADDR           SH_GROUP_ADDR_DEFAULT
#define MAX_AC_SERVERS          10

#ifdef __cplusplus
}
#endif

#endif /* _MESH_COMMON_H_ */
