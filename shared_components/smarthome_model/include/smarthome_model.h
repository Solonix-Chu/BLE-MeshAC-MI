#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "smarthome_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH_MODEL_MAX_FEATURES          12
#define SH_MODEL_MAX_ENUM_OPTIONS      8
#define SH_MODEL_MAX_PROFILES          12
#define SH_MODEL_NAME_MAX              24
#define SH_MODEL_DEVICE_TYPE_MAX       24
#define SH_MODEL_DYNAMIC_STRING_BYTES  512

#define SH_PROFILE_ID_AC               0x0100
#define SH_PROFILE_ID_LIGHT            0x0200
#define SH_PROFILE_ID_SWITCH           0x0300
#define SH_PROFILE_ID_TV               0x0400
#define SH_PROFILE_ID_CURTAIN          0x0500
#define SH_PROFILE_ID_CUSTOM_BASE      0x8000

#define SH_FEATURE_ID_POWER            0x0001
#define SH_FEATURE_ID_TEMPERATURE      0x0002
#define SH_FEATURE_ID_MODE             0x0003
#define SH_FEATURE_ID_FAN_SPEED        0x0004
#define SH_FEATURE_ID_BRIGHTNESS       0x0005
#define SH_FEATURE_ID_VOLUME           0x0006
#define SH_FEATURE_ID_CHANNEL          0x0007
#define SH_FEATURE_ID_MUTE             0x0008
#define SH_FEATURE_ID_INPUT_SOURCE     0x0009
#define SH_FEATURE_ID_POSITION         0x000A
#define SH_FEATURE_ID_OPEN_CLOSE       0x000B
#define SH_FEATURE_ID_ANALOG_VALUE     0x000C

typedef enum {
    SH_FEATURE_FLAG_READABLE  = 1 << 0,
    SH_FEATURE_FLAG_WRITABLE  = 1 << 1,
    SH_FEATURE_FLAG_REPORTS   = 1 << 2,
    SH_FEATURE_FLAG_GROUPABLE = 1 << 3,
} sh_feature_flags_t;

typedef enum {
    SH_FEATURE_ROLE_AUTO = 0,
    SH_FEATURE_ROLE_SWITCH,
    SH_FEATURE_ROLE_TEMPERATURE,
    SH_FEATURE_ROLE_MODE,
    SH_FEATURE_ROLE_FAN_SPEED,
    SH_FEATURE_ROLE_BRIGHTNESS,
    SH_FEATURE_ROLE_POSITION,
    SH_FEATURE_ROLE_VOLUME,
    SH_FEATURE_ROLE_CHANNEL,
    SH_FEATURE_ROLE_MUTE,
    SH_FEATURE_ROLE_INPUT_SOURCE,
    SH_FEATURE_ROLE_ANALOG,
} sh_feature_role_t;

typedef struct {
    int32_t min;
    int32_t max;
    int32_t step;
    const char *const *enum_labels;
    uint8_t enum_count;
} sh_feature_constraints_t;

typedef struct {
    uint16_t feature_id;
    const char *name;
    sh_feature_type_t type;
    sh_feature_role_t role;
    uint8_t flags;
    sh_feature_constraints_t constraints;
    int32_t default_value;
} sh_feature_def_t;

typedef struct {
    uint16_t profile_id;
    uint16_t version;
    const char *device_type;
    const char *display_name;
    uint8_t feature_count;
    const sh_feature_def_t *features;
} sh_device_profile_t;

typedef struct {
    uint16_t feature_id;
    sh_feature_type_t type;
    int32_t value;
} sh_feature_state_t;

typedef struct {
    sh_device_profile_t profile;
    sh_feature_def_t features[SH_MODEL_MAX_FEATURES];
    char *enum_labels[SH_MODEL_MAX_FEATURES][SH_MODEL_MAX_ENUM_OPTIONS];
    char strings[SH_MODEL_DYNAMIC_STRING_BYTES];
    size_t strings_used;
} sh_dynamic_profile_t;

esp_err_t sh_model_register_profile(const sh_device_profile_t *profile);
const sh_device_profile_t *sh_model_find_profile(uint16_t profile_id);
const sh_feature_def_t *sh_model_find_feature(const sh_device_profile_t *profile, uint16_t feature_id);
esp_err_t sh_model_validate_value(const sh_feature_def_t *feature, int32_t value);
sh_feature_role_t sh_model_infer_feature_role(const sh_feature_def_t *feature);
const char *sh_model_feature_role_name(sh_feature_role_t role);
const char *sh_model_feature_unit(const sh_feature_def_t *feature);

esp_err_t sh_model_default_states(const sh_device_profile_t *profile,
                                  sh_feature_state_t *states,
                                  size_t max_states,
                                  size_t *state_count);
sh_feature_state_t *sh_model_find_state(sh_feature_state_t *states, size_t state_count, uint16_t feature_id);
const sh_feature_state_t *sh_model_find_const_state(const sh_feature_state_t *states, size_t state_count, uint16_t feature_id);

esp_err_t sh_model_serialize_profile(const sh_device_profile_t *profile,
                                     uint8_t *buf,
                                     size_t buf_len,
                                     size_t *written);
esp_err_t sh_model_deserialize_profile(const uint8_t *buf,
                                       size_t len,
                                       sh_dynamic_profile_t *storage,
                                       const sh_device_profile_t **profile);

#ifdef __cplusplus
}
#endif
