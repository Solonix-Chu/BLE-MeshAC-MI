/* ac_storage.c - AC Control Storage Module Implementation */

#include "ac_storage.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define TAG "AC_STORAGE"

/* ==================== 私有数据结构 ==================== */

/* 存储模块上下文 */
typedef struct {
    nvs_handle_t nvs_handle;        /* NVS句柄 */
    char namespace[32];             /* 命名空间 */
    bool initialized;               /* 初始化标志 */
} ac_storage_context_t;

/* ==================== 私有变量 ==================== */

static ac_storage_context_t s_storage_ctx = {
    .nvs_handle = 0,
    .namespace = {0},
    .initialized = false
};

/* ==================== 私有函数声明 ==================== */

static esp_err_t _ensure_initialized(void);

/* ==================== 通用存储API实现 ==================== */

esp_err_t ac_storage_init(const char *nvs_namespace)
{
    esp_err_t err = ESP_OK;
    
    // 如果已经初始化，先清理
    if (s_storage_ctx.initialized) {
        ESP_LOGW(TAG, "Storage already initialized, reinitializing...");
        ac_storage_deinit();
    }
    
    // 设置命名空间
    const char *ns = nvs_namespace ? nvs_namespace : AC_STORAGE_DEFAULT_NAMESPACE;
    strncpy(s_storage_ctx.namespace, ns, sizeof(s_storage_ctx.namespace) - 1);
    s_storage_ctx.namespace[sizeof(s_storage_ctx.namespace) - 1] = '\0';
    
    // 打开NVS句柄使用指定的命名空间
    err = nvs_open(s_storage_ctx.namespace, NVS_READWRITE, &s_storage_ctx.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", s_storage_ctx.namespace, esp_err_to_name(err));
        return err;
    }
    
    s_storage_ctx.initialized = true;
    ESP_LOGI(TAG, "Storage module initialized with namespace: %s", s_storage_ctx.namespace);
    
    return ESP_OK;
}

esp_err_t ac_storage_save(const char *key, const void *data, size_t size)
{
    esp_err_t err;
    
    if (key == NULL || data == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for storage save");
        return ESP_ERR_INVALID_ARG;
    }
    
    err = _ensure_initialized();
    if (err != ESP_OK) {
        return err;
    }
    
    ESP_LOGD(TAG, "Saving data to key '%s', size: %zu bytes", key, size);
    
    // 使用NVS API保存数据
    err = nvs_set_blob(s_storage_ctx.nvs_handle, key, data, size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save data to key '%s': %s", key, esp_err_to_name(err));
        return err;
    }
    
    // 提交更改到flash
    err = nvs_commit(s_storage_ctx.nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Successfully saved %zu bytes to key '%s'", size, key);
    return ESP_OK;
}

esp_err_t ac_storage_load(const char *key, void *data, size_t size, bool *exists)
{
    esp_err_t err;
    bool data_exists = false;
    
    if (key == NULL || data == NULL || size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for storage load");
        return ESP_ERR_INVALID_ARG;
    }
    
    err = _ensure_initialized();
    if (err != ESP_OK) {
        if (exists) *exists = false;
        return err;
    }
    
    ESP_LOGD(TAG, "Loading data from key '%s', buffer size: %zu bytes", key, size);
    
    // 使用NVS API加载数据
    size_t required_size = size;
    err = nvs_get_blob(s_storage_ctx.nvs_handle, key, data, &required_size);
    
    if (err == ESP_OK) {
        data_exists = true;
        if (required_size != size) {
            ESP_LOGW(TAG, "Data size mismatch for key '%s': expected %zu, got %zu", key, size, required_size);
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        data_exists = false;
        err = ESP_OK; // 不存在不是错误
    } else {
        data_exists = false;
        ESP_LOGE(TAG, "NVS read error for key '%s': %s", key, esp_err_to_name(err));
    }
    
    if (exists) {
        *exists = data_exists;
    }
    
    if (err == ESP_OK && data_exists) {
        ESP_LOGI(TAG, "Successfully loaded %zu bytes from key '%s'", size, key);
    } else if (err == ESP_OK && !data_exists) {
        ESP_LOGD(TAG, "Key '%s' not found in storage", key);
    } else {
        ESP_LOGW(TAG, "Failed to load data from key '%s': %s", key, esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t ac_storage_deinit(void)
{
    if (!s_storage_ctx.initialized) {
        ESP_LOGW(TAG, "Storage not initialized, nothing to deinitialize");
        return ESP_OK;
    }
    
    // 关闭NVS句柄
    if (s_storage_ctx.nvs_handle != 0) {
        nvs_close(s_storage_ctx.nvs_handle);
        s_storage_ctx.nvs_handle = 0;
    }
    memset(s_storage_ctx.namespace, 0, sizeof(s_storage_ctx.namespace));
    s_storage_ctx.initialized = false;
    
    ESP_LOGI(TAG, "Storage module deinitialized");
    return ESP_OK;
}

/* ==================== 设备信息专用API实现 ==================== */

esp_err_t ac_storage_save_device_info(const ac_storage_device_info_t *device_info)
{
    if (device_info == NULL) {
        ESP_LOGE(TAG, "Device info pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (device_info->num_servers > 8) {
        ESP_LOGE(TAG, "Invalid number of servers: %u (max: 8)", device_info->num_servers);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Saving device info: %u servers, vnd_tid: 0x%04x", 
             device_info->num_servers, device_info->vnd_tid);
    
    return ac_storage_save(AC_STORAGE_DEVICE_INFO_KEY, device_info, sizeof(ac_storage_device_info_t));
}

esp_err_t ac_storage_load_device_info(ac_storage_device_info_t *device_info, bool *exists)
{
    esp_err_t err;
    bool data_exists = false;
    
    if (device_info == NULL) {
        ESP_LOGE(TAG, "Device info pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化设备信息结构体
    memset(device_info, 0, sizeof(ac_storage_device_info_t));
    
    err = ac_storage_load(AC_STORAGE_DEVICE_INFO_KEY, device_info, 
                         sizeof(ac_storage_device_info_t), &data_exists);
    
    if (exists) {
        *exists = data_exists;
    }
    
    if (err == ESP_OK && data_exists) {
        ESP_LOGI(TAG, "Loaded device info: %u servers, vnd_tid: 0x%04x", 
                 device_info->num_servers, device_info->vnd_tid);
        
        // 验证加载的数据合理性
        if (device_info->num_servers > 8) {
            ESP_LOGW(TAG, "Loaded data has invalid server count: %u, resetting to 0", 
                     device_info->num_servers);
            device_info->num_servers = 0;
        }
    } else if (err == ESP_OK && !data_exists) {
        ESP_LOGD(TAG, "No device info found in storage, using defaults");
    }
    
    return err;
}

/* ==================== 便捷API实现 ==================== */

bool ac_storage_is_initialized(void)
{
    return s_storage_ctx.initialized;
}

const char* ac_storage_get_namespace(void)
{
    if (!s_storage_ctx.initialized) {
        return NULL;
    }
    return s_storage_ctx.namespace;
}

/* ==================== 私有函数实现 ==================== */

static esp_err_t _ensure_initialized(void)
{
    if (!s_storage_ctx.initialized) {
        ESP_LOGE(TAG, "Storage module not initialized. Call ac_storage_init() first.");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/* ==================== 调试和诊断函数 ==================== */

void ac_storage_print_status(void)
{
    ESP_LOGI(TAG, "=== Storage Module Status ===");
    ESP_LOGI(TAG, "Initialized: %s", s_storage_ctx.initialized ? "YES" : "NO");
    if (s_storage_ctx.initialized) {
        ESP_LOGI(TAG, "Namespace: %s", s_storage_ctx.namespace);
        ESP_LOGI(TAG, "NVS Handle: %lu", (unsigned long)s_storage_ctx.nvs_handle);
    }
    ESP_LOGI(TAG, "============================");
} 