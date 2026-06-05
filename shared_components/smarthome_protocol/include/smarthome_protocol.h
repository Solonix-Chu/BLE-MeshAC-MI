#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_ble_mesh_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH_COMPANY_ID              0x02E5
#define SH_MODEL_ID_CLIENT         0x1001
#define SH_MODEL_ID_NODE           0x1002
#define SH_GROUP_ADDR_DEFAULT      0xC000
#define SH_PROTOCOL_VERSION        0x01
#define SH_PROFILE_CHUNK_PAYLOAD   240
#define SH_MAX_TLV_VALUE_LEN       48

#define SH_OP_PROFILE_GET          ESP_BLE_MESH_MODEL_OP_3(0x20, SH_COMPANY_ID)
#define SH_OP_PROFILE_STATUS       ESP_BLE_MESH_MODEL_OP_3(0x21, SH_COMPANY_ID)
#define SH_OP_PROFILE_SET          ESP_BLE_MESH_MODEL_OP_3(0x28, SH_COMPANY_ID)
#define SH_OP_FEATURE_GET          ESP_BLE_MESH_MODEL_OP_3(0x22, SH_COMPANY_ID)
#define SH_OP_FEATURE_SET          ESP_BLE_MESH_MODEL_OP_3(0x23, SH_COMPANY_ID)
#define SH_OP_FEATURE_STATUS       ESP_BLE_MESH_MODEL_OP_3(0x24, SH_COMPANY_ID)
#define SH_OP_NODE_EVENT           ESP_BLE_MESH_MODEL_OP_3(0x25, SH_COMPANY_ID)
#define SH_OP_DISCONNECT_NOTIFY    ESP_BLE_MESH_MODEL_OP_3(0x26, SH_COMPANY_ID)
#define SH_OP_DISCONNECT_ACK       ESP_BLE_MESH_MODEL_OP_3(0x27, SH_COMPANY_ID)
#define SH_OP_DEVICE_DIRECTORY     ESP_BLE_MESH_MODEL_OP_3(0x29, SH_COMPANY_ID)
#define SH_OP_DEVICE_DIRECTORY_GET ESP_BLE_MESH_MODEL_OP_3(0x2A, SH_COMPANY_ID)

typedef enum {
    SH_FEATURE_TYPE_BOOL = 1,
    SH_FEATURE_TYPE_INT = 2,
    SH_FEATURE_TYPE_ENUM = 3,
    SH_FEATURE_TYPE_RAW = 4,
} sh_feature_type_t;

typedef enum {
    SH_NODE_EVENT_ONLINE = 1,
    SH_NODE_EVENT_PROFILE_CHANGED = 2,
    SH_NODE_EVENT_ERROR = 3,
} sh_node_event_type_t;

typedef struct {
    uint8_t version;
    uint8_t tid;
} sh_msg_header_t;

typedef struct {
    uint16_t feature_id;
    sh_feature_type_t type;
    uint8_t value_len;
    const uint8_t *value;
} sh_tlv_t;

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t tid;
    uint16_t profile_id;
    uint16_t profile_version;
    uint16_t total_len;
    uint16_t crc16;
    uint8_t chunk_index;
    uint8_t chunk_count;
    uint8_t chunk_len;
} sh_profile_chunk_header_t;

uint8_t sh_protocol_next_tid(void);
uint16_t sh_protocol_crc16(const uint8_t *data, size_t len);

esp_err_t sh_protocol_write_header(uint8_t *buf, size_t buf_len, uint8_t tid, size_t *written);
esp_err_t sh_protocol_read_header(const uint8_t *buf, size_t len, sh_msg_header_t *header, size_t *offset);

esp_err_t sh_tlv_encode(uint8_t *buf, size_t buf_len, const sh_tlv_t *tlv, size_t *written);
esp_err_t sh_tlv_decode(const uint8_t *buf, size_t len, sh_tlv_t *tlv, size_t *consumed);

esp_err_t sh_tlv_encode_u8(uint8_t *buf, size_t buf_len, uint16_t feature_id,
                           sh_feature_type_t type, uint8_t value, size_t *written);
esp_err_t sh_tlv_encode_i32(uint8_t *buf, size_t buf_len, uint16_t feature_id,
                            int32_t value, size_t *written);
esp_err_t sh_tlv_value_to_i32(const sh_tlv_t *tlv, int32_t *value);

#ifdef __cplusplus
}
#endif
