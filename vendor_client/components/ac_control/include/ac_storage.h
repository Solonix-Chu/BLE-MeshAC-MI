/* ac_storage.h - AC Control Storage Module */

#ifndef _AC_STORAGE_H_
#define _AC_STORAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ==================== 存储模块配置 ==================== */
#define AC_STORAGE_DEFAULT_NAMESPACE "ac_storage"
#define AC_STORAGE_DEVICE_INFO_KEY   "device_info"

/* ==================== 数据结构定义 ==================== */

/* AC服务器信息结构体 */
typedef struct {
    uint16_t addr;                          /* Server unicast address */
    bool is_online;                         /* Online status */
    bool is_configured;                     /* Configuration completed status */
    uint8_t consecutive_timeouts;           /* Count of consecutive send timeouts */
    /* 设备状态信息 */
    uint8_t power_state;                    /* 电源状态 */
    uint8_t temperature;                    /* 设定温度 */
    uint8_t mode;                           /* 运行模式 */
    uint8_t fan_speed;                      /* 风速 */
    char device_name[16];                   /* 设备名称 */
    uint32_t last_update_time;              /* 最后更新时间戳 */
} ac_storage_server_info_t;

/* AC设备信息存储结构体 */
typedef struct {
    ac_storage_server_info_t servers[8];    /* 服务器信息数组 (最大8个设备) */
    uint8_t num_servers;                    /* 当前服务器数量 */
    uint16_t vnd_tid;                       /* Vendor message TID */
} ac_storage_device_info_t;

/* ==================== 通用存储API ==================== */

/**
 * @brief 初始化存储模块
 * 
 * @param nvs_namespace NVS命名空间，如果为NULL则使用默认值
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_storage_init(const char *nvs_namespace);

/**
 * @brief 保存数据到NVS
 * 
 * @param key 存储键名
 * @param data 要保存的数据
 * @param size 数据大小
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_storage_save(const char *key, const void *data, size_t size);

/**
 * @brief 从NVS加载数据
 * 
 * @param key 存储键名
 * @param data 数据缓冲区
 * @param size 缓冲区大小
 * @param exists 指示数据是否存在（可选参数，可为NULL）
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_storage_load(const char *key, void *data, size_t size, bool *exists);

/**
 * @brief 清理存储模块资源
 * 
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_storage_deinit(void);

/* ==================== 设备信息专用API ==================== */

/**
 * @brief 保存AC设备信息到存储
 * 
 * @param device_info 设备信息结构体指针
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_storage_save_device_info(const ac_storage_device_info_t *device_info);

/**
 * @brief 从存储加载AC设备信息
 * 
 * @param device_info 设备信息结构体指针
 * @param exists 指示数据是否存在（可选参数，可为NULL）
 * @return esp_err_t ESP_OK成功，其他值失败
 */
esp_err_t ac_storage_load_device_info(ac_storage_device_info_t *device_info, bool *exists);

/* ==================== 便捷API ==================== */

/**
 * @brief 获取存储模块是否已初始化
 * 
 * @return true 已初始化，false 未初始化
 */
bool ac_storage_is_initialized(void);

/**
 * @brief 获取当前使用的命名空间
 * 
 * @return const char* 命名空间字符串，如果未初始化返回NULL
 */
const char* ac_storage_get_namespace(void);

/**
 * @brief 打印存储模块状态信息（调试用）
 */
void ac_storage_print_status(void);

/* ==================== 使用示例 ==================== */
/*
 * 通用存储使用示例：
 *
 * 1. 初始化存储模块：
 *    esp_err_t err = ac_storage_init("my_app");
 *    if (err != ESP_OK) {
 *        ESP_LOGE(TAG, "Failed to init storage: %s", esp_err_to_name(err));
 *    }
 *
 * 2. 保存任意数据：
 *    my_config_t config = {...};
 *    err = ac_storage_save("config", &config, sizeof(config));
 *
 * 3. 加载任意数据：
 *    my_config_t config;
 *    bool exists;
 *    err = ac_storage_load("config", &config, sizeof(config), &exists);
 *    if (err == ESP_OK && exists) {
 *        // 使用加载的配置
 *    }
 *
 * 设备信息专用API使用示例：
 *
 * 1. 保存设备信息：
 *    ac_storage_device_info_t device_info = {...};
 *    err = ac_storage_save_device_info(&device_info);
 *
 * 2. 加载设备信息：
 *    ac_storage_device_info_t device_info;
 *    bool exists;
 *    err = ac_storage_load_device_info(&device_info, &exists);
 *    if (err == ESP_OK && exists) {
 *        printf("Found %d servers\n", device_info.num_servers);
 *    }
 *
 * 3. 清理资源：
 *    ac_storage_deinit();
 */

#ifdef __cplusplus
}
#endif

#endif /* _AC_STORAGE_H_ */ 