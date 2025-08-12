#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <fcntl.h>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "ac_control.h"
#include "web_server.h"
// #include "mdns.h"
#include "esp_heap_caps.h"

static const char *TAG = "WEB_SERVER";

#define MDNS_HOST_NAME "esp-home"
#define MDNS_INSTANCE  "esp home web server"

#define WEB_MOUNT_POINT "/www"
#define WEB_PARTITION_LABEL "www"

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (4096)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

static httpd_handle_t s_httpd = NULL;
static rest_server_context_t *s_rest_ctx = NULL;

// 打印内存与配置以定位 httpd 启动失败原因
static void log_runtime_resources(const httpd_config_t *cfg, const char *phase)
{
    size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t min_free_8bit = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    size_t min_free_int = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "[%s] Heap free (8bit): %u, internal: %u; min free (8bit): %u, internal: %u",
             phase, (unsigned)free_8bit, (unsigned)free_internal,
             (unsigned)min_free_8bit, (unsigned)min_free_int);

    if (cfg) {
        ESP_LOGI(TAG, "[%s] httpd cfg: task_prio=%d, stack_size=%u, core_id=%d, max_open_sockets=%d, max_uri_handlers=%d, max_resp_headers=%d, lru=%d",
                 phase, cfg->task_priority, (unsigned)cfg->stack_size, cfg->core_id,
                 cfg->max_open_sockets, cfg->max_uri_handlers, cfg->max_resp_headers,
                 cfg->lru_purge_enable);
    }

#ifdef CONFIG_LWIP_MAX_SOCKETS
    ESP_LOGI(TAG, "[%s] LWIP_MAX_SOCKETS=%d (rule: httpd.max_open_sockets + 3 <= LWIP_MAX_SOCKETS)", phase, CONFIG_LWIP_MAX_SOCKETS);
#endif
#ifdef CONFIG_LWIP_MAX_ACTIVE_TCP
    ESP_LOGI(TAG, "[%s] LWIP_MAX_ACTIVE_TCP=%d, LWIP_MAX_LISTENING_TCP=%d, LWIP_MAX_UDP_PCBS=%d, LWIP_MAX_RAW_PCBS=%d",
             phase, CONFIG_LWIP_MAX_ACTIVE_TCP, CONFIG_LWIP_MAX_LISTENING_TCP,
#ifdef CONFIG_LWIP_MAX_UDP_PCBS
             CONFIG_LWIP_MAX_UDP_PCBS,
#else
             -1,
#endif
#ifdef CONFIG_LWIP_MAX_RAW_PCBS
             CONFIG_LWIP_MAX_RAW_PCBS
#else
             -1
#endif
             );
#endif
}

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

// static void initialise_mdns(void)
// {
//     mdns_init();
//     mdns_hostname_set(MDNS_HOST_NAME);
//     mdns_instance_name_set(MDNS_INSTANCE);
//     mdns_txt_item_t serviceTxtData[] = {
//         {"board", "esp32"},
//         {"path", "/"}
//     };
//     mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData,
//                      sizeof(serviceTxtData) / sizeof(serviceTxtData[0]));
// }

static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }

    int fd = open(filepath, O_RDONLY, 0);

    if (fd == -1) {
        const char *dot = strrchr(req->uri, '.');
        if (dot == NULL) {
            strlcpy(filepath, rest_context->base_path, sizeof(filepath));
            strlcat(filepath, "/index.html", sizeof(filepath));
            fd = open(filepath, O_RDONLY, 0);
        }
    }

    if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                httpd_resp_sendstr_chunk(req, NULL);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    close(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t system_info_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t temperature_data_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "raw", esp_random() % 20);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t json_send_devices(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    uint8_t count = ac_get_device_count();
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddNumberToObject(root, "count", count);
    cJSON_AddItemToObject(root, "devices", arr);

    if (count > 0) {
        ac_device_info_t *list = (ac_device_info_t *)heap_caps_calloc(count, sizeof(ac_device_info_t), MALLOC_CAP_SPIRAM);
        if (!list) list = (ac_device_info_t *)calloc(count, sizeof(ac_device_info_t));
        if (list) {
            uint8_t got = ac_get_device_list(list, count);
            for (uint8_t i = 0; i < got; i++) {
                cJSON *d = cJSON_CreateObject();
                cJSON_AddNumberToObject(d, "id", i);
                cJSON_AddNumberToObject(d, "addr", list[i].addr);
                cJSON_AddStringToObject(d, "name", list[i].device_name);
                cJSON_AddBoolToObject(d, "online", list[i].is_online);
                cJSON_AddNumberToObject(d, "power", list[i].power_state);
                cJSON_AddNumberToObject(d, "temperature", list[i].temperature);
                cJSON_AddNumberToObject(d, "mode", list[i].mode);
                cJSON_AddNumberToObject(d, "fan_speed", list[i].fan_speed);
                cJSON_AddItemToArray(arr, d);
            }
            free(list);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    if (!json) {
        cJSON_Delete(root);
        return httpd_resp_send_500(req);
    }
    esp_err_t ret = httpd_resp_sendstr(req, json);
    free(json);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t devices_list_get_handler(httpd_req_t *req)
{
    return json_send_devices(req);
}

static bool parse_index_from_uri(const char *uri, int *out_idx, const char **out_rest)
{
    // Expecting /api/v1/devices/<idx>[/...]
    const char *p = uri;
    // skip prefix
    const char *prefix = "/api/v1/devices/";
    size_t plen = strlen(prefix);
    if (strncmp(uri, prefix, plen) != 0) return false;
    p = uri + plen;
    char *endptr = NULL;
    long idx = strtol(p, &endptr, 10);
    if (endptr == p || idx < 0) return false;
    if (out_idx) *out_idx = (int)idx;
    if (out_rest) *out_rest = endptr;
    return true;
}

static esp_err_t device_detail_get_handler(httpd_req_t *req)
{
    int idx = -1; const char *rest = NULL;
    if (!parse_index_from_uri(req->uri, &idx, &rest)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid index");
    }
    uint8_t count = ac_get_device_count();
    if (idx < 0 || idx >= count) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "device not found");
    }
    uint16_t addr = ac_get_server_addr_by_index((uint8_t)idx);
    ac_device_info_t info = {0};
    // 获取最新信息（触发一次拉取可选）
    ac_client_get_power(addr);
    ac_client_get_temperature(addr);
    ac_client_get_mode(addr);
    ac_client_get_fan_speed(addr);

    // 通过列表接口聚合（简单起见）
    return json_send_devices(req);
}

static esp_err_t device_control_post_handler(httpd_req_t *req)
{
    int idx = -1; const char *rest = NULL;
    if (!parse_index_from_uri(req->uri, &idx, &rest)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid index");
    }
    if (!rest || strstr(rest, "/control") == NULL) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    uint8_t count = ac_get_device_count();
    if (idx < 0 || idx >= count) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "device not found");
    }
    uint16_t addr = ac_get_server_addr_by_index((uint8_t)idx);

    // 读取 body
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 1024) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body length");
    }
    char *buf = (char *)heap_caps_calloc(total_len + 1, 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (char *)calloc(total_len + 1, 1);
    if (!buf) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    }
    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv fail");
    }

    cJSON *root = cJSON_ParseWithLength(buf, received);
    if (!root) {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
    }
    esp_err_t op_err = ESP_OK;
    cJSON *v = NULL;
    if ((v = cJSON_GetObjectItem(root, "power")) && cJSON_IsNumber(v)) {
        op_err |= ac_client_set_power(addr, (uint8_t)(v->valuedouble != 0));
    }
    if ((v = cJSON_GetObjectItem(root, "temperature")) && cJSON_IsNumber(v)) {
        op_err |= ac_client_set_temperature(addr, (uint8_t)v->valuedouble);
    }
    if ((v = cJSON_GetObjectItem(root, "mode")) && cJSON_IsNumber(v)) {
        op_err |= ac_client_set_mode(addr, (uint8_t)v->valuedouble);
    }
    if ((v = cJSON_GetObjectItem(root, "fan_speed")) && cJSON_IsNumber(v)) {
        op_err |= ac_client_set_fan_speed(addr, (uint8_t)v->valuedouble);
    }
    cJSON_Delete(root);
    free(buf);

    if (op_err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "control failed");
    }
    // 返回最新设备列表，便于前端刷新
    return json_send_devices(req);
}

static esp_err_t init_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = WEB_MOUNT_POINT,
        .partition_label = WEB_PARTITION_LABEL,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(WEB_PARTITION_LABEL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition '%s' size: total: %d, used: %d", WEB_PARTITION_LABEL, total, used);
    }
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    // 提升 httpd/内部模块日志等级用于诊断
    esp_log_level_set("httpd", ESP_LOG_DEBUG);
    esp_log_level_set("httpd_parse", ESP_LOG_DEBUG);
    esp_log_level_set("httpd_uri", ESP_LOG_DEBUG);
    esp_log_level_set("httpd_sess", ESP_LOG_DEBUG);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    ESP_ERROR_CHECK(init_fs());

    // s_rest_ctx = calloc(1, sizeof(rest_server_context_t));
    s_rest_ctx = heap_caps_calloc(1, sizeof(rest_server_context_t), MALLOC_CAP_SPIRAM);
    if (!s_rest_ctx) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(s_rest_ctx->base_path, WEB_MOUNT_POINT, sizeof(s_rest_ctx->base_path));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    // config.lru_purge_enable = true;
    // if (config.max_open_sockets > 2) {
    //     config.max_open_sockets = 2;
    // }
    // if (config.stack_size < 4096) {
    //     config.stack_size = 4096; // 保持日志中的 4096，避免复杂页面时栈不足
    // }

    log_runtime_resources(&config, "before_httpd_start");

    ESP_LOGI(TAG, "Starting HTTP Server");
    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        log_runtime_resources(&config, "after_httpd_fail");
        if (err == ESP_ERR_HTTPD_TASK) {
            ESP_LOGE(TAG, "HTTPD task create failed. Consider: lower max_open_sockets, enable LRU, increase/adjust stack_size, or free memory.");
            ESP_LOGE(TAG, "Current config: max_open_sockets=%d, stack_size=%u, lru_enable=%d", 
                     config.max_open_sockets, (unsigned)config.stack_size, config.lru_purge_enable);
        } else if (err == ESP_ERR_INVALID_ARG) {
            ESP_LOGE(TAG, "Invalid config. Ensure max_open_sockets + 3 <= LWIP_MAX_SOCKETS.");
            ESP_LOGE(TAG, "Current: max_open_sockets=%d, required_sockets=%d, LWIP_MAX_SOCKETS=%d", 
                     config.max_open_sockets, config.max_open_sockets + 3, CONFIG_LWIP_MAX_SOCKETS);
        }
        free(s_rest_ctx);
        s_rest_ctx = NULL;
        return err;
    }

    httpd_uri_t system_info_get_uri = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = system_info_get_handler,
        .user_ctx = s_rest_ctx
    };
    httpd_register_uri_handler(s_httpd, &system_info_get_uri);

    // Register device control (more specific) before generic devices/* GET to avoid 405 due to wildcard precedence
    httpd_uri_t device_control_post_uri = {
        .uri = "/api/v1/devices/*",
        .method = HTTP_POST,
        .handler = device_control_post_handler,
        .user_ctx = s_rest_ctx
    };
    httpd_register_uri_handler(s_httpd, &device_control_post_uri);

    httpd_uri_t devices_list_get_uri = {
        .uri = "/api/v1/devices",
        .method = HTTP_GET,
        .handler = devices_list_get_handler,
        .user_ctx = s_rest_ctx
    };
    httpd_register_uri_handler(s_httpd, &devices_list_get_uri);

    httpd_uri_t device_detail_get_uri = {
        .uri = "/api/v1/devices/*",
        .method = HTTP_GET,
        .handler = device_detail_get_handler,
        .user_ctx = s_rest_ctx
    };
    httpd_register_uri_handler(s_httpd, &device_detail_get_uri);

    httpd_uri_t temperature_data_get_uri = {
        .uri = "/api/v1/temp/raw",
        .method = HTTP_GET,
        .handler = temperature_data_get_handler,
        .user_ctx = s_rest_ctx
    };
    httpd_register_uri_handler(s_httpd, &temperature_data_get_uri);

    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = s_rest_ctx
    };
    httpd_register_uri_handler(s_httpd, &common_get_uri);

    // httpd 启动成功后再初始化 mDNS，避免占用 socket 造成启动失败
    // initialise_mdns();

    log_runtime_resources(&config, "after_httpd_start");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_rest_ctx) {
        free(s_rest_ctx);
        s_rest_ctx = NULL;
    }
    esp_vfs_spiffs_unregister(WEB_PARTITION_LABEL);
    return ESP_OK;
}
