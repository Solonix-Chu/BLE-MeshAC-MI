/* test_group_control.c - 组播控制功能测试程序 */

#include "ac_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mesh_common.h"

static const char *TAG = "GROUP_TEST";

void test_group_control_functionality(void)
{
    ESP_LOGI(TAG, "=== 组播控制功能测试 ===");
    
    // 获取组播地址
    uint16_t group_addr = ac_get_group_address();
    ESP_LOGI(TAG, "组播地址: 0x%04X", group_addr);
    
    // 获取设备数量
    uint8_t device_count = ac_get_device_count();
    ESP_LOGI(TAG, "当前设备数量: %d", device_count);
    
    if (device_count == 0) {
        ESP_LOGW(TAG, "没有设备连接，跳过组播测试");
        return;
    }
    
    // 测试将所有设备添加到组播组
    ESP_LOGI(TAG, "将所有设备添加到组播组...");
    for (uint8_t i = 0; i < device_count; i++) {
        uint16_t device_addr = ac_get_server_addr_by_index(i);
        if (device_addr != ESP_BLE_MESH_ADDR_UNASSIGNED) {
            esp_err_t err = ac_add_device_to_group(device_addr);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "设备 0x%04X 已添加到组播组", device_addr);
            } else {
                ESP_LOGE(TAG, "设备 0x%04X 添加到组播组失败: %s", device_addr, esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(500)); // 延迟避免网络拥塞
        }
    }
    
    // 等待设备配置完成
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 测试组播命令
    ESP_LOGI(TAG, "测试组播命令...");
    
    // 测试群控开机
    ESP_LOGI(TAG, "发送群控开机命令");
    esp_err_t err = ac_send_group_command(AC_STATUS_POWER, AC_POWER_ON);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "群控开机命令发送成功");
    } else {
        ESP_LOGE(TAG, "群控开机命令发送失败: %s", esp_err_to_name(err));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试群控温度设置
    ESP_LOGI(TAG, "发送群控温度设置命令 (26°C)");
    err = ac_send_group_command(AC_STATUS_TEMPERATURE, 26);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "群控温度设置命令发送成功");
    } else {
        ESP_LOGE(TAG, "群控温度设置命令发送失败: %s", esp_err_to_name(err));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试群控模式设置
    ESP_LOGI(TAG, "发送群控模式设置命令 (制冷模式)");
    err = ac_send_group_command(AC_STATUS_MODE, AC_MODE_COOL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "群控模式设置命令发送成功");
    } else {
        ESP_LOGE(TAG, "群控模式设置命令发送失败: %s", esp_err_to_name(err));
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 测试群控风速设置
    ESP_LOGI(TAG, "发送群控风速设置命令 (中速)");
    err = ac_send_group_command(AC_STATUS_FAN_SPEED, AC_FAN_SPEED_MEDIUM);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "群控风速设置命令发送成功");
    } else {
        ESP_LOGE(TAG, "群控风速设置命令发送失败: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "=== 组播控制功能测试完成 ===");
}

void test_device_group_status(void)
{
    ESP_LOGI(TAG, "=== 设备组播状态检查 ===");
    
    uint8_t device_count = ac_get_device_count();
    for (uint8_t i = 0; i < device_count; i++) {
        uint16_t device_addr = ac_get_server_addr_by_index(i);
        if (device_addr != ESP_BLE_MESH_ADDR_UNASSIGNED) {
            bool in_group = ac_is_device_in_group(device_addr);
            ESP_LOGI(TAG, "设备 0x%04X: %s组播组", device_addr, in_group ? "在" : "不在");
        }
    }
    
    ESP_LOGI(TAG, "=== 设备组播状态检查完成 ===");
} 