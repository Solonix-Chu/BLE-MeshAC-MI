#include "smarthome_store.h"
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

static char s_namespace[16] = SH_STORE_NAMESPACE_DEFAULT;

static void make_feature_key(char *buf, size_t len, uint16_t profile_id, uint16_t feature_id)
{
    snprintf(buf, len, "f%04x%04x", profile_id, feature_id);
}

static void make_profile_key(char *buf, size_t len, uint16_t profile_id)
{
    snprintf(buf, len, "p%04x", profile_id);
}

static const char *active_profile_key(void)
{
    return "active_prof";
}

esp_err_t sh_store_init(const char *nvs_namespace)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    if (nvs_namespace && nvs_namespace[0] != '\0') {
        strncpy(s_namespace, nvs_namespace, sizeof(s_namespace) - 1);
        s_namespace[sizeof(s_namespace) - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t sh_store_save_feature(uint16_t profile_id, uint16_t feature_id, int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(s_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    make_feature_key(key, sizeof(key), profile_id, feature_id);
    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t sh_store_load_feature(uint16_t profile_id, uint16_t feature_id, int32_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(s_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    make_feature_key(key, sizeof(key), profile_id, feature_id);
    err = nvs_get_i32(handle, key, value);
    nvs_close(handle);
    return err;
}

esp_err_t sh_store_save_profile_blob(uint16_t profile_id, const uint8_t *blob, size_t len)
{
    if (!blob || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(s_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    make_profile_key(key, sizeof(key), profile_id);
    err = nvs_set_blob(handle, key, blob, len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t sh_store_load_profile_blob(uint16_t profile_id, uint8_t *blob, size_t max_len, size_t *len)
{
    if (!blob || !len) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(s_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    make_profile_key(key, sizeof(key), profile_id);
    size_t required = 0;
    err = nvs_get_blob(handle, key, NULL, &required);
    if (err == ESP_OK) {
        if (required > max_len) {
            err = ESP_ERR_NO_MEM;
        } else {
            err = nvs_get_blob(handle, key, blob, &required);
            *len = required;
        }
    }
    nvs_close(handle);
    return err;
}

esp_err_t sh_store_save_active_profile(uint16_t profile_id)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(s_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u16(handle, active_profile_key(), profile_id);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t sh_store_load_active_profile(uint16_t *profile_id)
{
    if (!profile_id) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(s_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u16(handle, active_profile_key(), profile_id);
    nvs_close(handle);
    return err;
}

esp_err_t sh_store_clear_profile(uint16_t profile_id)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(s_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    char key[16];
    make_profile_key(key, sizeof(key), profile_id);
    err = nvs_erase_key(handle, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
