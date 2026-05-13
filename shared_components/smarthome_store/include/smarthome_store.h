#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "smarthome_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH_STORE_NAMESPACE_DEFAULT "sh_store"

esp_err_t sh_store_init(const char *nvs_namespace);
esp_err_t sh_store_save_feature(uint16_t profile_id, uint16_t feature_id, int32_t value);
esp_err_t sh_store_load_feature(uint16_t profile_id, uint16_t feature_id, int32_t *value);
esp_err_t sh_store_save_profile_blob(uint16_t profile_id, const uint8_t *blob, size_t len);
esp_err_t sh_store_load_profile_blob(uint16_t profile_id, uint8_t *blob, size_t max_len, size_t *len);
esp_err_t sh_store_clear_profile(uint16_t profile_id);

#ifdef __cplusplus
}
#endif
