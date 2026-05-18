#pragma once

#include "smarthome_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH_AC_POWER_OFF          0
#define SH_AC_POWER_ON           1
#define SH_AC_TEMP_MIN           16
#define SH_AC_TEMP_MAX           30
#define SH_AC_MODE_COOL          0
#define SH_AC_MODE_HEAT          1
#define SH_AC_MODE_FAN           2
#define SH_AC_MODE_DRY           3
#define SH_AC_MODE_AUTO          4
#define SH_AC_FAN_LOW            0
#define SH_AC_FAN_MEDIUM         1
#define SH_AC_FAN_HIGH           2

#define SH_LIGHT_POWER_OFF       0
#define SH_LIGHT_POWER_ON        1
#define SH_LIGHT_BRIGHTNESS_MIN  0
#define SH_LIGHT_BRIGHTNESS_MAX  100

#define SH_SWITCH_OFF            0
#define SH_SWITCH_ON             1

#define SH_TV_POWER_OFF          0
#define SH_TV_POWER_ON           1
#define SH_TV_VOLUME_MIN         0
#define SH_TV_VOLUME_MAX         100
#define SH_TV_CHANNEL_MIN        1
#define SH_TV_CHANNEL_MAX        999
#define SH_TV_INPUT_HDMI1        0
#define SH_TV_INPUT_HDMI2        1
#define SH_TV_INPUT_AV           2
#define SH_TV_INPUT_CAST         3

#define SH_CURTAIN_POWER_OFF     0
#define SH_CURTAIN_POWER_ON      1
#define SH_CURTAIN_POSITION_MIN  0
#define SH_CURTAIN_POSITION_MAX  100

typedef struct {
    uint16_t feature_id;
    const char *name;
    sh_feature_type_t type;
    sh_feature_role_t role;
    uint8_t flags;
    sh_feature_constraints_t constraints;
    int32_t default_value;
} sh_profile_feature_template_t;

const sh_device_profile_t *sh_profile_ac_get(void);
const sh_device_profile_t *sh_profile_light_get(void);
const sh_device_profile_t *sh_profile_switch_get(void);
const sh_device_profile_t *sh_profile_tv_get(void);
const sh_device_profile_t *sh_profile_curtain_get(void);
const sh_device_profile_t *sh_profiles_get_builtin(uint16_t profile_id);
uint8_t sh_profiles_get_builtin_count(void);
const sh_device_profile_t *sh_profiles_get_builtin_by_index(uint8_t index);
int sh_profiles_find_builtin_index(uint16_t profile_id);
esp_err_t sh_profiles_build_custom(sh_dynamic_profile_t *storage,
                                   uint16_t profile_id,
                                   const char *device_type,
                                   const char *display_name,
                                   const sh_profile_feature_template_t *features,
                                   uint8_t feature_count,
                                   const sh_device_profile_t **profile);
esp_err_t sh_profiles_register_builtin(void);

#ifdef __cplusplus
}
#endif
