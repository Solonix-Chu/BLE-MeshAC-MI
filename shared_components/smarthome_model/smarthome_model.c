#include "smarthome_model.h"
#include <string.h>

#ifndef ESP_RETURN_ON_ERROR
#define ESP_RETURN_ON_ERROR(x, tag, fmt, ...) do { \
        esp_err_t err_rc_ = (x);                    \
        if (err_rc_ != ESP_OK) {                    \
            return err_rc_;                         \
        }                                           \
    } while (0)
#endif

#define SH_PROFILE_MAGIC0 'S'
#define SH_PROFILE_MAGIC1 'H'
#define SH_PROFILE_MAGIC2 'P'
#define SH_PROFILE_MAGIC3 '1'

static const sh_device_profile_t *s_profiles[SH_MODEL_MAX_PROFILES];
static uint8_t s_profile_count;

static esp_err_t write_u16(uint8_t *buf, size_t cap, size_t *off, uint16_t value)
{
    if (*off + 2 > cap) {
        return ESP_ERR_NO_MEM;
    }
    buf[(*off)++] = (uint8_t)(value & 0xFF);
    buf[(*off)++] = (uint8_t)((value >> 8) & 0xFF);
    return ESP_OK;
}

static esp_err_t write_i32(uint8_t *buf, size_t cap, size_t *off, int32_t value)
{
    if (*off + 4 > cap) {
        return ESP_ERR_NO_MEM;
    }
    buf[(*off)++] = (uint8_t)(value & 0xFF);
    buf[(*off)++] = (uint8_t)((value >> 8) & 0xFF);
    buf[(*off)++] = (uint8_t)((value >> 16) & 0xFF);
    buf[(*off)++] = (uint8_t)((value >> 24) & 0xFF);
    return ESP_OK;
}

static esp_err_t write_string(uint8_t *buf, size_t cap, size_t *off, const char *value)
{
    size_t len = value ? strlen(value) : 0;
    if (len > 255 || *off + 1 + len > cap) {
        return ESP_ERR_NO_MEM;
    }
    buf[(*off)++] = (uint8_t)len;
    if (len > 0) {
        memcpy(&buf[*off], value, len);
        *off += len;
    }
    return ESP_OK;
}

static uint16_t read_u16(const uint8_t *buf, size_t *off)
{
    uint16_t value = (uint16_t)buf[*off] | ((uint16_t)buf[*off + 1] << 8);
    *off += 2;
    return value;
}

static int32_t read_i32(const uint8_t *buf, size_t *off)
{
    int32_t value = (int32_t)((uint32_t)buf[*off] |
                              ((uint32_t)buf[*off + 1] << 8) |
                              ((uint32_t)buf[*off + 2] << 16) |
                              ((uint32_t)buf[*off + 3] << 24));
    *off += 4;
    return value;
}

static char *storage_copy_string(sh_dynamic_profile_t *storage,
                                 const uint8_t *buf,
                                 size_t len,
                                 size_t *off)
{
    if (*off >= len) {
        return NULL;
    }
    uint8_t str_len = buf[(*off)++];
    if (*off + str_len > len ||
        storage->strings_used + str_len + 1 > sizeof(storage->strings)) {
        return NULL;
    }

    char *dst = &storage->strings[storage->strings_used];
    if (str_len > 0) {
        memcpy(dst, &buf[*off], str_len);
    }
    dst[str_len] = '\0';
    *off += str_len;
    storage->strings_used += str_len + 1;
    return dst;
}

esp_err_t sh_model_register_profile(const sh_device_profile_t *profile)
{
    if (!profile || !profile->features || profile->feature_count == 0 ||
        profile->feature_count > SH_MODEL_MAX_FEATURES) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < s_profile_count; i++) {
        if (s_profiles[i]->profile_id == profile->profile_id) {
            s_profiles[i] = profile;
            return ESP_OK;
        }
    }

    if (s_profile_count >= SH_MODEL_MAX_PROFILES) {
        return ESP_ERR_NO_MEM;
    }

    s_profiles[s_profile_count++] = profile;
    return ESP_OK;
}

const sh_device_profile_t *sh_model_find_profile(uint16_t profile_id)
{
    for (uint8_t i = 0; i < s_profile_count; i++) {
        if (s_profiles[i]->profile_id == profile_id) {
            return s_profiles[i];
        }
    }
    return NULL;
}

const sh_feature_def_t *sh_model_find_feature(const sh_device_profile_t *profile, uint16_t feature_id)
{
    if (!profile || !profile->features) {
        return NULL;
    }

    for (uint8_t i = 0; i < profile->feature_count; i++) {
        if (profile->features[i].feature_id == feature_id) {
            return &profile->features[i];
        }
    }
    return NULL;
}

esp_err_t sh_model_validate_value(const sh_feature_def_t *feature, int32_t value)
{
    if (!feature) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (feature->type) {
    case SH_FEATURE_TYPE_BOOL:
        return (value == 0 || value == 1) ? ESP_OK : ESP_ERR_INVALID_ARG;
    case SH_FEATURE_TYPE_ENUM:
        return (value >= 0 && value < feature->constraints.enum_count) ? ESP_OK : ESP_ERR_INVALID_ARG;
    case SH_FEATURE_TYPE_INT:
        if (value < feature->constraints.min || value > feature->constraints.max) {
            return ESP_ERR_INVALID_ARG;
        }
        if (feature->constraints.step > 1 &&
            ((value - feature->constraints.min) % feature->constraints.step) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        return ESP_OK;
    case SH_FEATURE_TYPE_RAW:
        return ESP_OK;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t sh_model_default_states(const sh_device_profile_t *profile,
                                  sh_feature_state_t *states,
                                  size_t max_states,
                                  size_t *state_count)
{
    if (!profile || !states || !state_count || max_states < profile->feature_count) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t i = 0; i < profile->feature_count; i++) {
        states[i].feature_id = profile->features[i].feature_id;
        states[i].type = profile->features[i].type;
        states[i].value = profile->features[i].default_value;
    }

    *state_count = profile->feature_count;
    return ESP_OK;
}

sh_feature_state_t *sh_model_find_state(sh_feature_state_t *states, size_t state_count, uint16_t feature_id)
{
    if (!states) {
        return NULL;
    }
    for (size_t i = 0; i < state_count; i++) {
        if (states[i].feature_id == feature_id) {
            return &states[i];
        }
    }
    return NULL;
}

const sh_feature_state_t *sh_model_find_const_state(const sh_feature_state_t *states, size_t state_count, uint16_t feature_id)
{
    if (!states) {
        return NULL;
    }
    for (size_t i = 0; i < state_count; i++) {
        if (states[i].feature_id == feature_id) {
            return &states[i];
        }
    }
    return NULL;
}

esp_err_t sh_model_serialize_profile(const sh_device_profile_t *profile,
                                     uint8_t *buf,
                                     size_t buf_len,
                                     size_t *written)
{
    if (!profile || !buf || !written || !profile->features) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t off = 0;
    if (buf_len < 6) {
        return ESP_ERR_NO_MEM;
    }
    buf[off++] = SH_PROFILE_MAGIC0;
    buf[off++] = SH_PROFILE_MAGIC1;
    buf[off++] = SH_PROFILE_MAGIC2;
    buf[off++] = SH_PROFILE_MAGIC3;
    ESP_RETURN_ON_ERROR(write_u16(buf, buf_len, &off, profile->profile_id), "sh_model", "profile_id");
    ESP_RETURN_ON_ERROR(write_u16(buf, buf_len, &off, profile->version), "sh_model", "version");
    ESP_RETURN_ON_ERROR(write_string(buf, buf_len, &off, profile->device_type), "sh_model", "device_type");
    ESP_RETURN_ON_ERROR(write_string(buf, buf_len, &off, profile->display_name), "sh_model", "display_name");
    if (off + 1 > buf_len || profile->feature_count > SH_MODEL_MAX_FEATURES) {
        return ESP_ERR_NO_MEM;
    }
    buf[off++] = profile->feature_count;

    for (uint8_t i = 0; i < profile->feature_count; i++) {
        const sh_feature_def_t *feature = &profile->features[i];
        ESP_RETURN_ON_ERROR(write_u16(buf, buf_len, &off, feature->feature_id), "sh_model", "feature_id");
        if (off + 3 > buf_len) {
            return ESP_ERR_NO_MEM;
        }
        buf[off++] = (uint8_t)feature->type;
        buf[off++] = feature->flags;
        buf[off++] = feature->constraints.enum_count;
        ESP_RETURN_ON_ERROR(write_i32(buf, buf_len, &off, feature->constraints.min), "sh_model", "min");
        ESP_RETURN_ON_ERROR(write_i32(buf, buf_len, &off, feature->constraints.max), "sh_model", "max");
        ESP_RETURN_ON_ERROR(write_i32(buf, buf_len, &off, feature->constraints.step), "sh_model", "step");
        ESP_RETURN_ON_ERROR(write_i32(buf, buf_len, &off, feature->default_value), "sh_model", "default");
        ESP_RETURN_ON_ERROR(write_string(buf, buf_len, &off, feature->name), "sh_model", "name");
        for (uint8_t e = 0; e < feature->constraints.enum_count; e++) {
            ESP_RETURN_ON_ERROR(write_string(buf, buf_len, &off, feature->constraints.enum_labels[e]), "sh_model", "enum");
        }
    }

    *written = off;
    return ESP_OK;
}

esp_err_t sh_model_deserialize_profile(const uint8_t *buf,
                                       size_t len,
                                       sh_dynamic_profile_t *storage,
                                       const sh_device_profile_t **profile)
{
    if (!buf || !storage || !profile || len < 9) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(storage, 0, sizeof(*storage));
    size_t off = 0;
    if (buf[off++] != SH_PROFILE_MAGIC0 || buf[off++] != SH_PROFILE_MAGIC1 ||
        buf[off++] != SH_PROFILE_MAGIC2 || buf[off++] != SH_PROFILE_MAGIC3) {
        return ESP_ERR_INVALID_ARG;
    }

    storage->profile.profile_id = read_u16(buf, &off);
    storage->profile.version = read_u16(buf, &off);
    storage->profile.device_type = storage_copy_string(storage, buf, len, &off);
    storage->profile.display_name = storage_copy_string(storage, buf, len, &off);
    if (!storage->profile.device_type || !storage->profile.display_name || off >= len) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t feature_count = buf[off++];
    if (feature_count == 0 || feature_count > SH_MODEL_MAX_FEATURES) {
        return ESP_ERR_INVALID_SIZE;
    }
    storage->profile.feature_count = feature_count;
    storage->profile.features = storage->features;

    for (uint8_t i = 0; i < feature_count; i++) {
        if (off + 20 > len) {
            return ESP_ERR_INVALID_SIZE;
        }
        sh_feature_def_t *feature = &storage->features[i];
        feature->feature_id = read_u16(buf, &off);
        feature->type = (sh_feature_type_t)buf[off++];
        feature->flags = buf[off++];
        feature->constraints.enum_count = buf[off++];
        feature->constraints.min = read_i32(buf, &off);
        feature->constraints.max = read_i32(buf, &off);
        feature->constraints.step = read_i32(buf, &off);
        feature->default_value = read_i32(buf, &off);
        feature->name = storage_copy_string(storage, buf, len, &off);
        if (!feature->name || feature->constraints.enum_count > SH_MODEL_MAX_ENUM_OPTIONS) {
            return ESP_ERR_INVALID_SIZE;
        }

        for (uint8_t e = 0; e < feature->constraints.enum_count; e++) {
            storage->enum_labels[i][e] = storage_copy_string(storage, buf, len, &off);
            if (!storage->enum_labels[i][e]) {
                return ESP_ERR_INVALID_SIZE;
            }
        }
        feature->constraints.enum_labels = (const char *const *)storage->enum_labels[i];
    }

    *profile = &storage->profile;
    return ESP_OK;
}
