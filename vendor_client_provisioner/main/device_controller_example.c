#include "device_controller.h"
#include "device_controller_types.h"
#include "mesh_common.h"
#include "esp_log.h"

static const char *TAG = "DC_EXAMPLE";

// 映射device_controller参数到mesh协议参数
static uint32_t map_dc_param_to_mesh_op(dc_parameter_t param, bool is_set)
{
    switch (param) {
        case DC_PARAM_POWER:
            return is_set ? AC_OP_SET_POWER : AC_OP_GET_POWER;
        case DC_PARAM_TEMPERATURE:
            return is_set ? AC_OP_SET_TEMPERATURE : AC_OP_GET_TEMPERATURE;
        case DC_PARAM_FAN_SPEED:
            return is_set ? AC_OP_SET_FAN_SPEED : AC_OP_GET_FAN_SPEED;
        case DC_PARAM_MODE:
            return is_set ? AC_OP_SET_MODE : AC_OP_GET_MODE;
        default:
            return 0;
    }
}

// 验证参数值是否符合mesh协议规范
static bool validate_ac_parameter(dc_parameter_t param, int32_t value)
{
    switch (param) {
        case DC_PARAM_POWER:
            return (value == AC_POWER_OFF || value == AC_POWER_ON);
            
        case DC_PARAM_TEMPERATURE:
            return (value >= AC_TEMP_MIN && value <= AC_TEMP_MAX);
            
        case DC_PARAM_FAN_SPEED:
            return (value >= AC_FAN_SPEED_LOW && value <= AC_FAN_SPEED_HIGH);
            
        case DC_PARAM_MODE:
            return (value >= AC_MODE_COOL && value <= AC_MODE_AUTO);
            
        default:
            return false;
    }
}

// 状态变化回调
void example_state_change_cb(dc_state_t old_state, dc_state_t new_state, void *user_data)
{
    ESP_LOGI(TAG, "State changed: %d -> %d", old_state, new_state);
}

// 显示更新回调
void example_display_update_cb(dc_context_t *context, void *user_data)
{
    ESP_LOGI(TAG, "Display update - State: %d, Device: %d", 
             context->current_state, context->current_device_idx);
}

// 参数变化回调 - 集成mesh协议发送
esp_err_t example_param_change_cb(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data)
{
    ESP_LOGI(TAG, "Parameter change - Device: %d, Param: %d, Value: %ld", 
             device_id, param, value);
    
    // 验证参数值
    if (!validate_ac_parameter(param, value)) {
        ESP_LOGE(TAG, "Invalid parameter value for param %d: %ld", param, value);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 获取对应的mesh操作码
    uint32_t mesh_opcode = map_dc_param_to_mesh_op(param, true);
    if (mesh_opcode == 0) {
        ESP_LOGE(TAG, "Unknown parameter: %d", param);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    ESP_LOGI(TAG, "Sending mesh command - Opcode: 0x%06lX, Value: %ld", mesh_opcode, value);
    
    // 这里应该调用实际的mesh发送函数
    // 例如：ac_control_send_command(device_id, mesh_opcode, value);
    
    return ESP_OK;
}

// 初始化示例
esp_err_t device_controller_example_init(void)
{
    ESP_LOGI(TAG, "Initializing device controller with AC mesh integration");
    
    // 显示映射关系
    ESP_LOGI(TAG, "Parameter mappings:");
    ESP_LOGI(TAG, "  Power: %s/%s", (AC_POWER_OFF == 0) ? "OFF=0" : "OFF=1", 
             (AC_POWER_ON == 1) ? "ON=1" : "ON=0");
    ESP_LOGI(TAG, "  Temperature: %d°C - %d°C", AC_TEMP_MIN, AC_TEMP_MAX);
    ESP_LOGI(TAG, "  Fan Speed: Low=%d, Medium=%d, High=%d", 
             AC_FAN_SPEED_LOW, AC_FAN_SPEED_MEDIUM, AC_FAN_SPEED_HIGH);
    ESP_LOGI(TAG, "  Mode: Cool=%d, Heat=%d, Fan=%d, Dry=%d, Auto=%d",
             AC_MODE_COOL, AC_MODE_HEAT, AC_MODE_FAN, AC_MODE_DRY, AC_MODE_AUTO);
    
    return ESP_OK;
} 