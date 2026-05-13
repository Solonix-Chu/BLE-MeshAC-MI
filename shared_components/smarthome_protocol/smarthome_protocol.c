#include "smarthome_protocol.h"
#include <string.h>

static uint8_t s_tid;

uint8_t sh_protocol_next_tid(void)
{
    s_tid++;
    if (s_tid == 0) {
        s_tid = 1;
    }
    return s_tid;
}

uint16_t sh_protocol_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    if (!data && len > 0) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

esp_err_t sh_protocol_write_header(uint8_t *buf, size_t buf_len, uint8_t tid, size_t *written)
{
    if (!buf || !written || buf_len < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    buf[0] = SH_PROTOCOL_VERSION;
    buf[1] = tid;
    *written = 2;
    return ESP_OK;
}

esp_err_t sh_protocol_read_header(const uint8_t *buf, size_t len, sh_msg_header_t *header, size_t *offset)
{
    if (!buf || !header || !offset || len < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    if (buf[0] != SH_PROTOCOL_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }

    header->version = buf[0];
    header->tid = buf[1];
    *offset = 2;
    return ESP_OK;
}

esp_err_t sh_tlv_encode(uint8_t *buf, size_t buf_len, const sh_tlv_t *tlv, size_t *written)
{
    if (!buf || !tlv || !written || (tlv->value_len > 0 && !tlv->value)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (tlv->value_len > SH_MAX_TLV_VALUE_LEN || buf_len < (size_t)(4 + tlv->value_len)) {
        return ESP_ERR_NO_MEM;
    }

    buf[0] = (uint8_t)(tlv->feature_id & 0xFF);
    buf[1] = (uint8_t)((tlv->feature_id >> 8) & 0xFF);
    buf[2] = (uint8_t)tlv->type;
    buf[3] = tlv->value_len;
    if (tlv->value_len > 0) {
        memcpy(&buf[4], tlv->value, tlv->value_len);
    }

    *written = 4 + tlv->value_len;
    return ESP_OK;
}

esp_err_t sh_tlv_decode(const uint8_t *buf, size_t len, sh_tlv_t *tlv, size_t *consumed)
{
    if (!buf || !tlv || !consumed || len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t value_len = buf[3];
    if (value_len > SH_MAX_TLV_VALUE_LEN || len < (size_t)(4 + value_len)) {
        return ESP_ERR_INVALID_SIZE;
    }

    tlv->feature_id = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    tlv->type = (sh_feature_type_t)buf[2];
    tlv->value_len = value_len;
    tlv->value = value_len ? &buf[4] : NULL;
    *consumed = 4 + value_len;
    return ESP_OK;
}

esp_err_t sh_tlv_encode_u8(uint8_t *buf, size_t buf_len, uint16_t feature_id,
                           sh_feature_type_t type, uint8_t value, size_t *written)
{
    sh_tlv_t tlv = {
        .feature_id = feature_id,
        .type = type,
        .value_len = 1,
        .value = &value,
    };
    return sh_tlv_encode(buf, buf_len, &tlv, written);
}

esp_err_t sh_tlv_encode_i32(uint8_t *buf, size_t buf_len, uint16_t feature_id,
                            int32_t value, size_t *written)
{
    uint8_t raw[4] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF),
    };
    sh_tlv_t tlv = {
        .feature_id = feature_id,
        .type = SH_FEATURE_TYPE_INT,
        .value_len = sizeof(raw),
        .value = raw,
    };
    return sh_tlv_encode(buf, buf_len, &tlv, written);
}

esp_err_t sh_tlv_value_to_i32(const sh_tlv_t *tlv, int32_t *value)
{
    if (!tlv || !value || !tlv->value) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (tlv->type) {
    case SH_FEATURE_TYPE_BOOL:
    case SH_FEATURE_TYPE_ENUM:
        if (tlv->value_len != 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        *value = tlv->value[0];
        return ESP_OK;
    case SH_FEATURE_TYPE_INT:
        if (tlv->value_len == 1) {
            *value = tlv->value[0];
            return ESP_OK;
        }
        if (tlv->value_len != 4) {
            return ESP_ERR_INVALID_SIZE;
        }
        *value = (int32_t)((uint32_t)tlv->value[0] |
                           ((uint32_t)tlv->value[1] << 8) |
                           ((uint32_t)tlv->value[2] << 16) |
                           ((uint32_t)tlv->value[3] << 24));
        return ESP_OK;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}
