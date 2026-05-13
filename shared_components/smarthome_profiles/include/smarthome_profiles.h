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

const sh_device_profile_t *sh_profile_ac_get(void);
const sh_device_profile_t *sh_profile_light_get(void);
const sh_device_profile_t *sh_profile_switch_get(void);
esp_err_t sh_profiles_register_builtin(void);

#ifdef __cplusplus
}
#endif
