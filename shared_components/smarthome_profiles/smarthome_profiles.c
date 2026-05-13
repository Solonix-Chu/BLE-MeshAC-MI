#include "smarthome_profiles.h"

static const char *const s_ac_modes[] = {"Cool", "Heat", "Fan", "Dry", "Auto"};
static const char *const s_ac_fans[] = {"Low", "Medium", "High"};

static const sh_feature_def_t s_ac_features[] = {
    {
        .feature_id = SH_FEATURE_ID_POWER,
        .name = "Power",
        .type = SH_FEATURE_TYPE_BOOL,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = SH_AC_POWER_OFF,
    },
    {
        .feature_id = SH_FEATURE_ID_TEMPERATURE,
        .name = "Temperature",
        .type = SH_FEATURE_TYPE_INT,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = SH_AC_TEMP_MIN, .max = SH_AC_TEMP_MAX, .step = 1},
        .default_value = 25,
    },
    {
        .feature_id = SH_FEATURE_ID_MODE,
        .name = "Mode",
        .type = SH_FEATURE_TYPE_ENUM,
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.enum_labels = s_ac_modes, .enum_count = 5},
        .default_value = SH_AC_MODE_COOL,
    },
    {
        .feature_id = SH_FEATURE_ID_FAN_SPEED,
        .name = "Fan Speed",
        .type = SH_FEATURE_TYPE_ENUM,
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
        .flags = SH_FEATURE_FLAG_READABLE | SH_FEATURE_FLAG_WRITABLE |
                 SH_FEATURE_FLAG_REPORTS | SH_FEATURE_FLAG_GROUPABLE,
        .constraints = {.min = 0, .max = 1, .step = 1},
        .default_value = SH_LIGHT_POWER_OFF,
    },
    {
        .feature_id = SH_FEATURE_ID_BRIGHTNESS,
        .name = "Brightness",
        .type = SH_FEATURE_TYPE_INT,
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

esp_err_t sh_profiles_register_builtin(void)
{
    esp_err_t err = sh_model_register_profile(&s_ac_profile);
    if (err != ESP_OK) {
        return err;
    }
    err = sh_model_register_profile(&s_light_profile);
    if (err != ESP_OK) {
        return err;
    }
    return sh_model_register_profile(&s_switch_profile);
}
