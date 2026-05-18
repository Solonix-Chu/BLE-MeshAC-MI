#include "smarthome_profiles.h"
#include <string.h>

static const char *const s_ac_modes[] = {"Cool", "Heat", "Fan", "Dry", "Auto"};
static const char *const s_ac_fans[] = {"Low", "Medium", "High"};
static const char *const s_tv_inputs[] = {"HDMI1", "HDMI2", "AV", "Cast"};

static const sh_feature_def_t s_ac_features[] = {
    {
        .feature_id = SH_FEATURE_ID_POWER,
        .name = "Power",
        .type = SH_FEATURE_TYPE_BOOL,
        .role = SH_FEATURE_ROLE_SWITCH,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = SH_AC_POWER_OFF,
    },
    {
        .feature_id = SH_FEATURE_ID_TEMPERATURE,
        .name = "Temperature",
        .type = SH_FEATURE_TYPE_INT,
        .role = SH_FEATURE_ROLE_TEMPERATURE,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = SH_AC_TEMP_MIN, .max = SH_AC_TEMP_MAX, .step = 1},
        .default_value = 25,
    },
    {
        .feature_id = SH_FEATURE_ID_MODE,
        .name = "Mode",
        .type = SH_FEATURE_TYPE_ENUM,
        .role = SH_FEATURE_ROLE_MODE,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.enum_labels = s_ac_modes, .enum_count = 5},
        .default_value = SH_AC_MODE_COOL,
    },
    {
        .feature_id = SH_FEATURE_ID_FAN_SPEED,
        .name = "Fan Speed",
        .type = SH_FEATURE_TYPE_ENUM,
        .role = SH_FEATURE_ROLE_FAN_SPEED,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.enum_labels = s_ac_fans, .enum_count = 3},
        .default_value = SH_AC_FAN_LOW,
    },
};

static const sh_device_profile_t s_ac_profile = {
    .profile_id = SH_PROFILE_ID_AC,
    .version = 1,
    .device_type = "air_conditioner",
    .display_name = "Air Conditioner",
    .feature_count = sizeof(s_ac_features) / sizeof(s_ac_features[0]),
    .features = s_ac_features,
};

static const sh_feature_def_t s_light_features[] = {
    {
        .feature_id = SH_FEATURE_ID_POWER,
        .name = "Power",
        .type = SH_FEATURE_TYPE_BOOL,
        .role = SH_FEATURE_ROLE_SWITCH,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = SH_LIGHT_POWER_OFF,
    },
    {
        .feature_id = SH_FEATURE_ID_BRIGHTNESS,
        .name = "Brightness",
        .type = SH_FEATURE_TYPE_INT,
        .role = SH_FEATURE_ROLE_BRIGHTNESS,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = SH_LIGHT_BRIGHTNESS_MIN, .max = SH_LIGHT_BRIGHTNESS_MAX, .step = 5},
        .default_value = 50,
    },
};

static const sh_device_profile_t s_light_profile = {
    .profile_id = SH_PROFILE_ID_LIGHT,
    .version = 1,
    .device_type = "light",
    .display_name = "Light",
    .feature_count = sizeof(s_light_features) / sizeof(s_light_features[0]),
    .features = s_light_features,
};

static const sh_feature_def_t s_switch_features[] = {
    {
        .feature_id = SH_FEATURE_ID_POWER,
        .name = "Power",
        .type = SH_FEATURE_TYPE_BOOL,
        .role = SH_FEATURE_ROLE_SWITCH,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = SH_SWITCH_OFF,
    },
};

static const sh_device_profile_t s_switch_profile = {
    .profile_id = SH_PROFILE_ID_SWITCH,
    .version = 1,
    .device_type = "switch",
    .display_name = "Switch",
    .feature_count = sizeof(s_switch_features) / sizeof(s_switch_features[0]),
    .features = s_switch_features,
};

static const sh_feature_def_t s_tv_features[] = {
    {
        .feature_id = SH_FEATURE_ID_POWER,
        .name = "Power",
        .type = SH_FEATURE_TYPE_BOOL,
        .role = SH_FEATURE_ROLE_SWITCH,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = SH_TV_POWER_OFF,
    },
    {
        .feature_id = SH_FEATURE_ID_VOLUME,
        .name = "Volume",
        .type = SH_FEATURE_TYPE_INT,
        .role = SH_FEATURE_ROLE_VOLUME,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = SH_TV_VOLUME_MIN, .max = SH_TV_VOLUME_MAX, .step = 5},
        .default_value = 20,
    },
    {
        .feature_id = SH_FEATURE_ID_CHANNEL,
        .name = "Channel",
        .type = SH_FEATURE_TYPE_INT,
        .role = SH_FEATURE_ROLE_CHANNEL,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS,
        .constraints = {.min = SH_TV_CHANNEL_MIN, .max = SH_TV_CHANNEL_MAX, .step = 1},
        .default_value = 1,
    },
    {
        .feature_id = SH_FEATURE_ID_MUTE,
        .name = "Mute",
        .type = SH_FEATURE_TYPE_BOOL,
        .role = SH_FEATURE_ROLE_MUTE,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = 0,
    },
    {
        .feature_id = SH_FEATURE_ID_INPUT_SOURCE,
        .name = "Input",
        .type = SH_FEATURE_TYPE_ENUM,
        .role = SH_FEATURE_ROLE_INPUT_SOURCE,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS,
        .constraints = {.enum_labels = s_tv_inputs, .enum_count = 4},
        .default_value = SH_TV_INPUT_HDMI1,
    },
};

static const sh_device_profile_t s_tv_profile = {
    .profile_id = SH_PROFILE_ID_TV,
    .version = 1,
    .device_type = "television",
    .display_name = "TV",
    .feature_count = sizeof(s_tv_features) / sizeof(s_tv_features[0]),
    .features = s_tv_features,
};

static const sh_feature_def_t s_curtain_features[] = {
    {
        .feature_id = SH_FEATURE_ID_POWER,
        .name = "Power",
        .type = SH_FEATURE_TYPE_BOOL,
        .role = SH_FEATURE_ROLE_SWITCH,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = SH_CURTAIN_POWER_ON,
    },
    {
        .feature_id = SH_FEATURE_ID_POSITION,
        .name = "Position",
        .type = SH_FEATURE_TYPE_INT,
        .role = SH_FEATURE_ROLE_POSITION,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = SH_CURTAIN_POSITION_MIN, .max = SH_CURTAIN_POSITION_MAX, .step = 10},
        .default_value = 0,
    },
};

static const sh_device_profile_t s_curtain_profile = {
    .profile_id = SH_PROFILE_ID_CURTAIN,
    .version = 1,
    .device_type = "curtain",
    .display_name = "Curtain",
    .feature_count = sizeof(s_curtain_features) / sizeof(s_curtain_features[0]),
    .features = s_curtain_features,
};

static const sh_device_profile_t *const s_builtin_profiles[] = {
    &s_ac_profile,
    &s_light_profile,
    &s_switch_profile,
    &s_tv_profile,
    &s_curtain_profile,
};

const sh_device_profile_t *sh_profile_ac_get(void)
{
    return &s_ac_profile;
}

const sh_device_profile_t *sh_profile_light_get(void)
{
    return &s_light_profile;
}

const sh_device_profile_t *sh_profile_switch_get(void)
{
    return &s_switch_profile;
}

const sh_device_profile_t *sh_profile_tv_get(void)
{
    return &s_tv_profile;
}

const sh_device_profile_t *sh_profile_curtain_get(void)
{
    return &s_curtain_profile;
}

const sh_device_profile_t *sh_profiles_get_builtin(uint16_t profile_id)
{
    switch (profile_id) {
    case SH_PROFILE_ID_AC:
        return &s_ac_profile;
    case SH_PROFILE_ID_LIGHT:
        return &s_light_profile;
    case SH_PROFILE_ID_SWITCH:
        return &s_switch_profile;
    case SH_PROFILE_ID_TV:
        return &s_tv_profile;
    case SH_PROFILE_ID_CURTAIN:
        return &s_curtain_profile;
    default:
        return NULL;
    }
}

uint8_t sh_profiles_get_builtin_count(void)
{
    return sizeof(s_builtin_profiles) / sizeof(s_builtin_profiles[0]);
}

const sh_device_profile_t *sh_profiles_get_builtin_by_index(uint8_t index)
{
    return index < sh_profiles_get_builtin_count() ? s_builtin_profiles[index] : NULL;
}

int sh_profiles_find_builtin_index(uint16_t profile_id)
{
    for (uint8_t i = 0; i < sh_profiles_get_builtin_count(); i++) {
        if (s_builtin_profiles[i]->profile_id == profile_id) {
            return i;
        }
    }
    return -1;
}

static char *copy_dynamic_string(sh_dynamic_profile_t *storage, const char *value)
{
    size_t len = value ? strlen(value) : 0;
    if (storage->strings_used + len + 1 > sizeof(storage->strings)) {
        return NULL;
    }

    char *dst = &storage->strings[storage->strings_used];
    if (len > 0) {
        memcpy(dst, value, len);
    }
    dst[len] = '\0';
    storage->strings_used += len + 1;
    return dst;
}

esp_err_t sh_profiles_build_custom(sh_dynamic_profile_t *storage,
                                   uint16_t profile_id,
                                   const char *device_type,
                                   const char *display_name,
                                   const sh_profile_feature_template_t *features,
                                   uint8_t feature_count,
                                   const sh_device_profile_t **profile)
{
    if (!storage || !device_type || !display_name || !features || !profile ||
        feature_count == 0 || feature_count > SH_MODEL_MAX_FEATURES) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(storage, 0, sizeof(*storage));
    storage->profile.profile_id = profile_id;
    storage->profile.version = 1;
    storage->profile.device_type = copy_dynamic_string(storage, device_type);
    storage->profile.display_name = copy_dynamic_string(storage, display_name);
    storage->profile.feature_count = feature_count;
    storage->profile.features = storage->features;
    if (!storage->profile.device_type || !storage->profile.display_name) {
        return ESP_ERR_NO_MEM;
    }

    for (uint8_t i = 0; i < feature_count; i++) {
        storage->features[i].feature_id = features[i].feature_id;
        storage->features[i].name = copy_dynamic_string(storage, features[i].name);
        storage->features[i].type = features[i].type;
        storage->features[i].role = features[i].role;
        storage->features[i].flags = features[i].flags;
        storage->features[i].constraints = features[i].constraints;
        storage->features[i].default_value = features[i].default_value;
        if (!storage->features[i].name) {
            return ESP_ERR_NO_MEM;
        }
        if (features[i].constraints.enum_count > SH_MODEL_MAX_ENUM_OPTIONS) {
            return ESP_ERR_INVALID_ARG;
        }
        for (uint8_t e = 0; e < features[i].constraints.enum_count; e++) {
            storage->enum_labels[i][e] =
                copy_dynamic_string(storage, features[i].constraints.enum_labels[e]);
            if (!storage->enum_labels[i][e]) {
                return ESP_ERR_NO_MEM;
            }
        }
        if (features[i].constraints.enum_count > 0) {
            storage->features[i].constraints.enum_labels =
                (const char *const *)storage->enum_labels[i];
        }
    }

    *profile = &storage->profile;
    return sh_model_register_profile(*profile);
}

esp_err_t sh_profiles_register_builtin(void)
{
    for (uint8_t i = 0; i < sh_profiles_get_builtin_count(); i++) {
        esp_err_t err = sh_model_register_profile(s_builtin_profiles[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
