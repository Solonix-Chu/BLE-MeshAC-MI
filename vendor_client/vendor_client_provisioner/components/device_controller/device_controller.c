#include "device_controller.h"
#include "device_controller_config.h"
#include "device_controller_state_machine.h"
#include "device_controller_buttons.h"
#include "device_controller_display.h"
#include "device_controller_ui_integration.h"
#include "device_controller_types.h"
#include "gui_guider.h"
#include "esp_log.h"
#include "ac_control.h"
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DEVICE_CONTROLLER";

static bool s_initialized = false;
static bool s_started = false;

// Forward declarations
static void button_event_handler(dc_event_t event, void *user_data);
static void state_change_handler(dc_state_t old_state, dc_state_t new_state, void *user_data);
static void display_update_handler(dc_context_t *context, void *user_data);
static esp_err_t parameter_change_handler(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data);
static esp_err_t initialize_multiple_devices(void);
static esp_err_t handle_device_switch(const dc_context_t *context);
static void show_device_switch_animation(uint8_t old_device_idx, uint8_t new_device_idx);

// UI integration callbacks
static void ui_screen_changed_handler(dc_ui_screen_t screen);
static void ui_boot_complete_handler(void);

// AC control callbacks
static void ac_device_status_callback(uint16_t device_addr, ac_status_type_t status_type, uint8_t value);
static void ac_device_online_callback(uint16_t device_addr, bool is_online);
static void ac_device_provisioned_callback(uint16_t device_addr);
static void sync_device_info_from_ac_control(void);
static void sync_single_device_status(uint16_t device_addr);
static void sync_all_devices_status(void);
static uint16_t get_device_addr_by_index(uint8_t index);

// Button event handler
static void button_event_handler(dc_event_t event, void *user_data)
{
    // ESP_LOGD(TAG, "Button event: %d", event);
    
    esp_err_t ret = dc_state_machine_process_event(event);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to process event %d: %s", event, esp_err_to_name(ret));
    }
}

// State change handler
static void state_change_handler(dc_state_t old_state, dc_state_t new_state, void *user_data)
{
    ESP_LOGI(TAG, "State changed: %d -> %d", old_state, new_state);
    
    // Get current context for UI updates
    const dc_context_t *context = dc_state_machine_get_context();
    
    // Check for device switch first (can happen in any state)
    if (context) {
        handle_device_switch(context);
    }
    
    // Handle specific state transitions
    switch (new_state) {
        case DC_STATE_IDLE:
            ESP_LOGI(TAG, "Entered idle state");
            // Show main screen when returning to idle
            dc_ui_integration_show_main_screen();
            if (context) {
                dc_ui_integration_update_display((dc_context_t*)context);
                // Also show device status in console
                const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
                if (device) {
                    ESP_LOGI(TAG, "=== DEVICE STATUS ===");
                    ESP_LOGI(TAG, "Device: %s (#%d)", device->device_name, device->device_id + 1);
                    ESP_LOGI(TAG, "Status: %s", device->is_online ? "ONLINE" : "OFFLINE");
                    ESP_LOGI(TAG, "Power:  %s", device->status.power ? "ON" : "OFF");
                    ESP_LOGI(TAG, "Temp:   %ld°C", device->status.temperature);
                    ESP_LOGI(TAG, "Mode:   %s", (device->status.mode == 0) ? "COOL" : 
                            (device->status.mode == 1) ? "HEAT" : 
                            (device->status.mode == 2) ? "FAN" : 
                            (device->status.mode == 3) ? "DRY" : "AUTO");
                    ESP_LOGI(TAG, "Fan:    %s", (device->status.fan_speed == 1) ? "LOW" : 
                            (device->status.fan_speed == 2) ? "MED" : "HIGH");
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "LEFT/RIGHT: Switch Device");
                    ESP_LOGI(TAG, "DOUBLE-CLICK: Enter Menu");
                    ESP_LOGI(TAG, "===================");
                }
            }
            break;
            
        case DC_STATE_MENU_NAVIGATE:
            ESP_LOGI(TAG, "Entered menu navigation state");
            // Show menu navigation with blinking parameter
            if (context) {
                dc_ui_integration_show_menu_navigation((dc_parameter_t)context->current_selection, true);
                dc_ui_integration_update_display((dc_context_t*)context);
            }
            break;
            
        case DC_STATE_VALUE_ADJUST:
            ESP_LOGI(TAG, "Entered value adjustment state");
            // Show value adjustment mode
            if (context) {
                const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
                if (device) {
                    int32_t current_value = 0;
                    // Get current value based on selected parameter
                    switch (context->selected_parameter) {
                        case DC_PARAM_POWER:
                            current_value = device->status.power;
                            break;
                        case DC_PARAM_TEMPERATURE:
                            current_value = device->status.temperature;
                            break;
                        case DC_PARAM_MODE:
                            current_value = device->status.mode;
                            break;
                        case DC_PARAM_FAN_SPEED:
                            current_value = device->status.fan_speed;
                            break;
                        default:
                            break;
                    }
                    dc_ui_integration_show_value_adjustment(context->selected_parameter, current_value, true);
                    
                    // Show console value adjustment display
                    const char *param_name;
                    const char *param_value;
                    
                    switch (context->selected_parameter) {
                        case DC_PARAM_POWER:
                            param_name = "Power";
                            param_value = (context->editing_value != 0) ? "ON" : "OFF";
                            break;
                        case DC_PARAM_TEMPERATURE:
                            param_name = "Temperature";
                            static char temp_str[16];
                            snprintf(temp_str, sizeof(temp_str), "%ld°C", context->editing_value);
                            param_value = temp_str;
                            break;
                        case DC_PARAM_FAN_SPEED:
                            param_name = "Fan Speed";
                            param_value = (context->editing_value == 1) ? "LOW" : 
                                        (context->editing_value == 2) ? "MED" : "HIGH";
                            break;
                        case DC_PARAM_MODE:
                            param_name = "Mode";
                            param_value = (context->editing_value == 0) ? "COOL" : 
                                        (context->editing_value == 1) ? "HEAT" : 
                                        (context->editing_value == 2) ? "FAN" : 
                                        (context->editing_value == 3) ? "DRY" : "AUTO";
                            break;
                        default:
                            param_name = "Unknown";
                            param_value = "---";
                            break;
                    }
                    
                    ESP_LOGI(TAG, "=== VALUE ADJUSTMENT ===");
                    ESP_LOGI(TAG, "Device: %s", device->device_name);
                    ESP_LOGI(TAG, "Parameter: %s", param_name);
                    ESP_LOGI(TAG, "Value: >>> %s <<<", param_value);
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "UP/DOWN: Adjust");
                    ESP_LOGI(TAG, "CENTER: Confirm");
                    ESP_LOGI(TAG, "DOUBLE-CLICK: Cancel");
                    ESP_LOGI(TAG, "========================");
                }
                dc_ui_integration_update_display((dc_context_t*)context);
            }
            break;
            
        default:
            break;
    }
}

// Display update handler
static void display_update_handler(dc_context_t *context, void *user_data)
{
    esp_err_t ret = dc_ui_integration_update_display(context);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update display: %s", esp_err_to_name(ret));
    }
}

// Parameter change handler
static esp_err_t parameter_change_handler(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data)
{
    const char *param_name;
    char msg[64];
    esp_err_t ret = ESP_OK;
    
    // 检查是否为虚拟"All Device"群控
    uint8_t actual_device_count = ac_get_device_count();
    if (device_id == actual_device_count) {  // 虚拟设备的索引是实际设备数量
        ESP_LOGI(TAG, "Processing group control command for All Device");
        
        // 将设备控制器参数转换为AC控制参数
        ac_status_type_t ac_param;
        switch (param) {
            case DC_PARAM_POWER:
                ac_param = AC_STATUS_POWER;
                param_name = "Power";
                snprintf(msg, sizeof(msg), "All Devices %s: %s", param_name, value ? "ON" : "OFF");
                break;
            case DC_PARAM_TEMPERATURE:
                ac_param = AC_STATUS_TEMPERATURE;
                param_name = "Temperature";
                snprintf(msg, sizeof(msg), "All Devices %s: %ld°C", param_name, value);
                break;
            case DC_PARAM_FAN_SPEED:
                ac_param = AC_STATUS_FAN_SPEED;
                param_name = "Fan Speed";
                const char *fan_names[] = {"Auto", "Low", "Medium", "High"};
                snprintf(msg, sizeof(msg), "All Devices %s: %s", param_name, 
                        (value >= 0 && value < 4) ? fan_names[value] : "Unknown");
                break;
            case DC_PARAM_MODE:
                ac_param = AC_STATUS_MODE;
                param_name = "Mode";
                const char *mode_names[] = {"Cool", "Heat", "Fan", "Dry", "Auto"};
                snprintf(msg, sizeof(msg), "All Devices %s: %s", param_name, 
                        (value >= 0 && value < 5) ? mode_names[value] : "Unknown");
                break;
            default:
                ESP_LOGW(TAG, "Unknown parameter type for group control: %d", param);
                return ESP_ERR_INVALID_ARG;
        }
        
        // 发送群控命令
        ret = ac_send_group_command(ac_param, (uint8_t)value);
        
        if (ret == ESP_OK) {
            dc_ui_integration_show_message("GROUP SET OK", 1000);
            ESP_LOGI(TAG, "Successfully sent group command: %s", msg);
        } else {
            dc_ui_integration_show_message("GROUP SET ERROR", 1000);
            ESP_LOGE(TAG, "Failed to send group command: %s (error: %s)", msg, esp_err_to_name(ret));
        }
        
        return ret;
    }
    
    // 处理单个设备控制
    uint16_t device_addr = get_device_addr_by_index(device_id);
    if (device_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        ESP_LOGE(TAG, "Invalid device index %d", device_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否为设备删除或连接切换请求
    if (param == DC_PARAM_MAX && value == 0xFF) {
        ESP_LOGI(TAG, "Processing device connection toggle for device 0x%04X", device_addr);
        
        // 切换设备连接状态
        ret = ac_toggle_device_connection(device_addr);
        
        if (ret == ESP_OK) {
            // 检查切换后的状态并显示相应消息
            bool is_filtered = ac_is_device_filtered(device_addr);
            bool is_blacklisted = ac_is_device_blacklisted(device_addr);
            if (is_blacklisted) {
                dc_ui_integration_show_message("REMOVED", 2000);
                ESP_LOGI(TAG, "Device 0x%04X removed from network and blacklisted", device_addr);
            } else if (is_filtered) {
                dc_ui_integration_show_message("FILTERED", 2000);
                ESP_LOGI(TAG, "Device 0x%04X filtered from provisioning", device_addr);
            } else {
                dc_ui_integration_show_message("RECONNECTING", 2000);
                ESP_LOGI(TAG, "Device 0x%04X ready for reconnection", device_addr);
            }
        } else {
            dc_ui_integration_show_message("TOGGLE FAILED", 1000);
            ESP_LOGE(TAG, "Failed to toggle device 0x%04X connection: %s", device_addr, esp_err_to_name(ret));
        }
        
        return ret;
    }
    
    // 检查是否为完全删除设备请求（长按删除）
    if (param == DC_PARAM_MAX && value == 0xFE) {
        ESP_LOGI(TAG, "Processing complete device removal for device 0x%04X", device_addr);
        dc_ui_integration_show_message("REMOVING DEVICE...", 3000);
        
        // 使用完整删除流程：发送断开连接通知 + 从网络移除 + key refresh
        ret = ac_remove_device_completely(device_addr);
        
        if (ret == ESP_OK) {
            dc_ui_integration_show_message("DEVICE REMOVED", 2000);
            ESP_LOGI(TAG, "Device 0x%04X completely removed from network", device_addr);
            
            // 刷新设备列表，移除已删除的设备
            sync_device_info_from_ac_control();
        } else {
            dc_ui_integration_show_message("REMOVE FAILED", 1000);
            ESP_LOGE(TAG, "Failed to completely remove device 0x%04X: %s", device_addr, esp_err_to_name(ret));
        }
        
        return ret;
    }

    switch (param) {
        case DC_PARAM_POWER:
            param_name = "Power";
            snprintf(msg, sizeof(msg), "%s: %s", param_name, value ? "ON" : "OFF");
            // Send power command via BLE mesh
            ret = ac_client_set_power(device_addr, (uint8_t)value);
            break;
            
        case DC_PARAM_TEMPERATURE:
            param_name = "Temperature";
            snprintf(msg, sizeof(msg), "%s: %ld°C", param_name, value);
            // Send temperature command via BLE mesh
            ret = ac_client_set_temperature(device_addr, (uint8_t)value);
            break;
            
        case DC_PARAM_FAN_SPEED:
            param_name = "Fan Speed";
            const char *fan_names[] = {"Auto", "Low", "Medium", "High"};
            snprintf(msg, sizeof(msg), "%s: %s", param_name, 
                    (value >= 0 && value < 4) ? fan_names[value] : "Unknown");
            // Send fan speed command via BLE mesh
            ret = ac_client_set_fan_speed(device_addr, (uint8_t)value);
            break;
            
        case DC_PARAM_MODE:
            param_name = "Mode";
            const char *mode_names[] = {"Cool", "Heat", "Fan", "Dry", "Auto"};
            snprintf(msg, sizeof(msg), "%s: %s", param_name, 
                    (value >= 0 && value < 5) ? mode_names[value] : "Unknown");
            // Send mode command via BLE mesh
            ret = ac_client_set_mode(device_addr, (uint8_t)value);
            break;
            
        default:
            snprintf(msg, sizeof(msg), "Parameter %d: %ld", param, value);
            ESP_LOGW(TAG, "Unknown parameter type: %d", param);
            return ESP_ERR_INVALID_ARG;
    }
    
    if (ret == ESP_OK) {
    // Show "SET OK" message briefly
    dc_ui_integration_show_message("SET OK", 500);
        ESP_LOGI(TAG, "Successfully sent command to device 0x%04X: %s", device_addr, msg);
    } else {
        // Show error message
        dc_ui_integration_show_message("SET ERROR", 1000);
        ESP_LOGE(TAG, "Failed to send command to device 0x%04X: %s (error: %s)", 
                device_addr, msg, esp_err_to_name(ret));
    }
    
    return ret;
}

// Get parameter display string
__attribute__((unused))
static const char* get_param_display_string(const dc_device_info_t *device, dc_parameter_t param)
{
    static char str_buffer[32];
    
    switch (param) {
    case DC_PARAM_POWER:
        return device->status.power ? "ON" : "OFF";
        
    case DC_PARAM_TEMPERATURE:
        snprintf(str_buffer, sizeof(str_buffer), "%ld°C", device->status.temperature);
        return str_buffer;
        
    case DC_PARAM_FAN_SPEED:
        switch (device->status.fan_speed) {
            case DEVICE_CONTROLLER_FAN_SPEED_LOW: return "Low";
            case DEVICE_CONTROLLER_FAN_SPEED_MEDIUM: return "Medium";
            case DEVICE_CONTROLLER_FAN_SPEED_HIGH: return "High";
            default: return "Unknown";
        }
        
    case DC_PARAM_MODE:
        switch (device->status.mode) {
            case DEVICE_CONTROLLER_MODE_COOL: return "Cool";
            case DEVICE_CONTROLLER_MODE_HEAT: return "Heat";
            case DEVICE_CONTROLLER_MODE_FAN: return "Fan";
            case DEVICE_CONTROLLER_MODE_DRY: return "Dry";
            case DEVICE_CONTROLLER_MODE_AUTO: return "Auto";
            default: return "Unknown";
        }
        
    default:
        return "Unknown";
    }
}

// UI screen change handler
static void ui_screen_changed_handler(dc_ui_screen_t screen)
{
    ESP_LOGI(TAG, "UI Screen changed to: %d", screen);
    
    switch (screen) {
        case DC_UI_SCREEN_BOOT:
            ESP_LOGI(TAG, "Boot screen active");
            break;
            
        case DC_UI_SCREEN_MAIN:
            ESP_LOGI(TAG, "Main screen active - device controller ready");
            break;
            
        case DC_UI_SCREEN_MENU:
            ESP_LOGI(TAG, "Menu navigation active");
            break;
            
        case DC_UI_SCREEN_MESSAGE:
            ESP_LOGI(TAG, "Message display active");
            break;
            
        default:
            break;
    }
}

// UI boot complete handler
static void ui_boot_complete_handler(void)
{
    ESP_LOGI(TAG, "Boot sequence completed - device controller fully operational");
    
    // Sync device information from AC control after boot
    sync_device_info_from_ac_control();
    
    // Start device controller operations
    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        // Update display with initial device state
        dc_ui_integration_update_display((dc_context_t*)context);
    }
    
    // Sync status from all online devices after boot complete
    ESP_LOGI(TAG, "Boot complete, syncing status from all online devices...");
    // Add delay to ensure all systems are ready
    vTaskDelay(pdMS_TO_TICKS(1000));
    sync_all_devices_status();
}

// AC device status change callback
static void ac_device_status_callback(uint16_t device_addr, ac_status_type_t status_type, uint8_t value)
{
    ESP_LOGI(TAG, "AC device 0x%04X status updated: type %d = %d", device_addr, status_type, value);
    
    // Find device index by address
    uint8_t device_count = ac_get_device_count();
    for (uint8_t i = 0; i < device_count; i++) {
        if (get_device_addr_by_index(i) == device_addr) {
            // Update device status in state machine
            const dc_device_info_t *current_device = dc_state_machine_get_device_info(i);
            if (current_device) {
                dc_device_info_t updated_device = *current_device;
                
                switch (status_type) {
                    case AC_STATUS_POWER:
                        updated_device.status.power = value;
                        break;
                    case AC_STATUS_TEMPERATURE:
                        updated_device.status.temperature = value;
                        break;
                    case AC_STATUS_MODE:
                        updated_device.status.mode = value;
                        break;
                    case AC_STATUS_FAN_SPEED:
                        updated_device.status.fan_speed = value;
                        break;
                }
                
                dc_state_machine_set_device_info(i, &updated_device);
                
                // Update UI if this is the current device
                const dc_context_t *context = dc_state_machine_get_context();
                if (context && context->current_device_idx == i) {
                    dc_ui_integration_update_display((dc_context_t*)context);
                }
            }
            break;
        }
    }
}

// AC device online status change callback
static void ac_device_online_callback(uint16_t device_addr, bool is_online)
{
    ESP_LOGI(TAG, "AC device 0x%04X is now %s", device_addr, is_online ? "ONLINE" : "OFFLINE");
    
    // Find device index by address
    uint8_t device_count = ac_get_device_count();
    for (uint8_t i = 0; i < device_count; i++) {
        if (get_device_addr_by_index(i) == device_addr) {
            // Update device online status
            const dc_device_info_t *current_device = dc_state_machine_get_device_info(i);
            if (current_device) {
                dc_device_info_t updated_device = *current_device;
                updated_device.is_online = is_online;
                dc_state_machine_set_device_info(i, &updated_device);
                
                // Update UI if this is the current device
                const dc_context_t *context = dc_state_machine_get_context();
                if (context && context->current_device_idx == i) {
                    dc_ui_integration_update_display((dc_context_t*)context);
                }
                
                // Show status message
                char msg[32];
                snprintf(msg, sizeof(msg), "Device %s", is_online ? "ONLINE" : "OFFLINE");
                dc_ui_integration_show_message(msg, 1000);
                
                // If device came online, sync its status
                if (is_online) {
                    ESP_LOGI(TAG, "Device 0x%04X came online, syncing status...", device_addr);
                    // Add a small delay to ensure the device is ready
                    vTaskDelay(pdMS_TO_TICKS(500));
                    sync_single_device_status(device_addr);
                }
            }
            break;
        }
    }
}

// AC device provisioned callback
static void ac_device_provisioned_callback(uint16_t device_addr)
{
    ESP_LOGI(TAG, "New AC device 0x%04X provisioned!", device_addr);
    
    // Refresh device list and reinitialize
    sync_device_info_from_ac_control();
    
    // Show message to user
    dc_ui_integration_show_message("New Device", 2000);
    
    // Sync status of the newly provisioned device
    ESP_LOGI(TAG, "New device 0x%04X provisioned, syncing status...", device_addr);
    // Add delay to ensure device is fully ready after provisioning
    vTaskDelay(pdMS_TO_TICKS(1000));
    sync_single_device_status(device_addr);
}

// Sync device information from AC control module
static void sync_device_info_from_ac_control(void)
{
    ESP_LOGI(TAG, "Syncing device information from AC control");
    
    // Re-initialize devices with current AC control data
    esp_err_t ret = initialize_multiple_devices();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to sync device information: %s", esp_err_to_name(ret));
        return;
    }
    
    // Update UI
    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        dc_ui_integration_update_display((dc_context_t*)context);
    }
}

// Sync single device status from server
static void sync_single_device_status(uint16_t device_addr)
{
    ESP_LOGI(TAG, "Syncing status for device 0x%04X", device_addr);
    
    // Request all status information from the device
    esp_err_t ret;
    
    ret = ac_client_get_power(device_addr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to request power status from 0x%04X: %s", 
                device_addr, esp_err_to_name(ret));
    }
    
    ret = ac_client_get_temperature(device_addr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to request temperature status from 0x%04X: %s", 
                device_addr, esp_err_to_name(ret));
    }
    
    ret = ac_client_get_mode(device_addr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to request mode status from 0x%04X: %s", 
                device_addr, esp_err_to_name(ret));
    }
    
    ret = ac_client_get_fan_speed(device_addr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to request fan speed status from 0x%04X: %s", 
                device_addr, esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "Status sync requests sent for device 0x%04X", device_addr);
}

// Sync all online devices status
static void sync_all_devices_status(void)
{
    ESP_LOGI(TAG, "Syncing status for all online devices");
    
    uint8_t device_count = ac_get_device_count();
    for (uint8_t i = 0; i < device_count; i++) {
        uint16_t device_addr = get_device_addr_by_index(i);
        if (device_addr != ESP_BLE_MESH_ADDR_UNASSIGNED) {
            // Check if device is online before requesting status
            if (ac_is_server_online(device_addr)) {
                sync_single_device_status(device_addr);
                // Add small delay between requests to avoid overwhelming the mesh network
                vTaskDelay(pdMS_TO_TICKS(100));
            } else {
                ESP_LOGD(TAG, "Skipping offline device 0x%04X", device_addr);
            }
        }
    }
}

// Get device address by index
static uint16_t get_device_addr_by_index(uint8_t index)
{
    // 检查是否为虚拟"All Device"
    uint8_t actual_device_count = ac_get_device_count();
    if (index == actual_device_count) {  // 虚拟设备的索引是实际设备数量
        return ac_get_group_address();  // 返回组播地址
    }
    
    return ac_get_server_addr_by_index(index);
}

// Initialize multiple devices with different configurations
static esp_err_t initialize_multiple_devices(void)
{
    esp_err_t ret = ESP_OK;
    
    // Get actual device count from AC control
    uint8_t device_count = ac_get_device_count();
    
    if (device_count == 0) {
        ESP_LOGW(TAG, "No AC devices found, using minimal configuration");
        // Create a placeholder device for testing
        dc_device_info_t placeholder_device = {
            .device_id = 0,
            .device_name = "No Device",
            .is_online = false,
        .status = {
            .power = false,
                .temperature = 22,
            .mode = DEVICE_CONTROLLER_MODE_AUTO,
            .fan_speed = DEVICE_CONTROLLER_FAN_SPEED_LOW
        }
    };
    
        ret = dc_state_machine_set_device_info(0, &placeholder_device);
    if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set placeholder device info: %s", esp_err_to_name(ret));
        return ret;
        }
        
        ESP_LOGI(TAG, "Initialized with placeholder device (no real devices connected)");
        return ESP_OK;
    }
    
    // Get device list from AC control
    ac_device_info_t ac_devices[DEVICE_CONTROLLER_MAX_DEVICES];
    uint8_t actual_count = ac_get_device_list(ac_devices, DEVICE_CONTROLLER_MAX_DEVICES);
    
    ESP_LOGI(TAG, "AC control returned %d device records", actual_count);
    
    // Debug: Print all received device information
    for (uint8_t i = 0; i < actual_count; i++) {
        ESP_LOGI(TAG, "Device %d Debug Info:", i);
        ESP_LOGI(TAG, "  Address: 0x%04X", ac_devices[i].addr);
        ESP_LOGI(TAG, "  Online: %s", ac_devices[i].is_online ? "YES" : "NO");
        ESP_LOGI(TAG, "  Raw name length: %d", strlen(ac_devices[i].device_name));
        
        // Print device name character by character to debug encoding issues
        ESP_LOGI(TAG, "  Raw name bytes:");
        for (int j = 0; j < 16 && ac_devices[i].device_name[j] != '\0'; j++) {
            ESP_LOGI(TAG, "    [%d]: 0x%02X ('%c')", j, 
                    (unsigned char)ac_devices[i].device_name[j],
                    isprint((unsigned char)ac_devices[i].device_name[j]) ? ac_devices[i].device_name[j] : '?');
        }
        ESP_LOGI(TAG, "  Name string: \"%s\"", ac_devices[i].device_name);
    }
    
    // Convert AC device info to device controller format
    for (uint8_t i = 0; i < actual_count; i++) {
        // Use device name from AC control, or create default name
        const char *device_name;
        static char default_names[DEVICE_CONTROLLER_MAX_DEVICES][32];
        static char cleaned_names[DEVICE_CONTROLLER_MAX_DEVICES][32];
        
        // Check if device has a valid name
        bool has_valid_name = false;
        if (strlen(ac_devices[i].device_name) > 0) {
            // Check if name contains printable characters
            bool is_printable = true;
            for (int j = 0; j < strlen(ac_devices[i].device_name); j++) {
                if (!isprint((unsigned char)ac_devices[i].device_name[j])) {
                    is_printable = false;
                    break;
                }
            }
            
            if (is_printable) {
                // Copy and clean the name (remove any trailing spaces/nulls)
                strncpy(cleaned_names[i], ac_devices[i].device_name, sizeof(cleaned_names[i]) - 1);
                cleaned_names[i][sizeof(cleaned_names[i]) - 1] = '\0';
                
                // Trim trailing spaces
                int len = strlen(cleaned_names[i]);
                while (len > 0 && isspace((unsigned char)cleaned_names[i][len - 1])) {
                    cleaned_names[i][len - 1] = '\0';
                    len--;
                }
                
                if (strlen(cleaned_names[i]) > 0) {
                    device_name = cleaned_names[i];
                    has_valid_name = true;
                    ESP_LOGI(TAG, "Using cleaned device name: \"%s\"", device_name);
                }
            }
        }
        
        if (!has_valid_name) {
            snprintf(default_names[i], sizeof(default_names[i]), "AC Device %d", i + 1);
            device_name = default_names[i];
            ESP_LOGW(TAG, "Device %d has invalid/empty name, using default: \"%s\"", i, device_name);
        }
        
        dc_device_info_t dc_device = {
            .device_id = i,
            .device_name = device_name,
            .is_online = ac_devices[i].is_online,
            .status = {
                .power = ac_devices[i].power_state,
                .temperature = ac_devices[i].temperature,
                .mode = ac_devices[i].mode,
                .fan_speed = ac_devices[i].fan_speed
            }
        };
        
        ret = dc_state_machine_set_device_info(i, &dc_device);
    if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set device %d info: %s", i, esp_err_to_name(ret));
        return ret;
        }
        
        ESP_LOGI(TAG, "Initialized device %d: \"%s\" (0x%04X) - %s", 
                i, device_name, ac_devices[i].addr,
                dc_device.is_online ? "ONLINE" : "OFFLINE");
    }
    
    ESP_LOGI(TAG, "Successfully initialized %d devices from AC control", actual_count);
    ESP_LOGI(TAG, "=== End Device Initialization Debug ===");
    
    // 添加"All Device"虚拟设备用于群控
    if (actual_count > 0) {
        dc_device_info_t all_device = {
            .device_id = actual_count,  // 使用实际设备数量作为虚拟设备的索引
            .device_name = AC_ALL_DEVICE_NAME,
            .is_online = true,  // 虚拟设备始终在线
            .status = {
                .power = false,     // 默认状态
                .temperature = 25,  // 默认温度
                .mode = DEVICE_CONTROLLER_MODE_AUTO,
                .fan_speed = DEVICE_CONTROLLER_FAN_SPEED_LOW
            }
        };
        
        ret = dc_state_machine_set_device_info(actual_count, &all_device);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set All Device info: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Added virtual device: \"%s\" for group control at index %d", AC_ALL_DEVICE_NAME, actual_count);
    }
    
    return ESP_OK;
}

// Handle device switching with animation
static esp_err_t handle_device_switch(const dc_context_t *context)
{
    if (!context) {
        return ESP_ERR_INVALID_ARG;
    }
    
    static uint8_t previous_device_idx = 0;
    
    if (previous_device_idx != context->current_device_idx) {
        ESP_LOGI(TAG, "Device switched from %d to %d", previous_device_idx, context->current_device_idx);
        
        // Show switching animation
        show_device_switch_animation(previous_device_idx, context->current_device_idx);
        
        // Get new device info
        const dc_device_info_t *new_device = dc_state_machine_get_device_info(context->current_device_idx);
        if (new_device) {
            ESP_LOGI(TAG, "=== SWITCHED TO DEVICE %d ===", context->current_device_idx + 1);
            ESP_LOGI(TAG, "Device: %s", new_device->device_name);
            ESP_LOGI(TAG, "Status: %s", new_device->is_online ? "ONLINE" : "OFFLINE");
            ESP_LOGI(TAG, "Power:  %s", new_device->status.power ? "ON" : "OFF");
            ESP_LOGI(TAG, "Temp:   %ld°C", new_device->status.temperature);
            ESP_LOGI(TAG, "Mode:   %s", (new_device->status.mode == 0) ? "COOL" : 
                    (new_device->status.mode == 1) ? "HEAT" : 
                    (new_device->status.mode == 2) ? "FAN" : 
                    (new_device->status.mode == 3) ? "DRY" : "AUTO");
            ESP_LOGI(TAG, "Fan:    %s", (new_device->status.fan_speed == 1) ? "LOW" : 
                    (new_device->status.fan_speed == 2) ? "MED" : "HIGH");
            ESP_LOGI(TAG, "============================");
        }
        
        previous_device_idx = context->current_device_idx;
    }
    
    return ESP_OK;
}

// Show device switch animation
static void show_device_switch_animation(uint8_t old_device_idx, uint8_t new_device_idx)
{
    // Use the dedicated UI integration function for device switching (slide animation only)
    esp_err_t ret = dc_ui_integration_show_device_switch(old_device_idx, new_device_idx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to show device switch animation: %s", esp_err_to_name(ret));
        // Fallback: just refresh main screen
        dc_ui_integration_show_main_screen();
    }
    
    ESP_LOGI(TAG, "Device switch animation: %d → %d", 
             old_device_idx + 1, new_device_idx + 1);
}

esp_err_t device_controller_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Device controller already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // Initialize AC client first
    ret = ac_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize AC client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register AC control callbacks
    ac_client_register_callbacks(ac_device_status_callback, 
                                ac_device_online_callback,
                                ac_device_provisioned_callback);
    
    ESP_LOGI(TAG, "AC client initialized and callbacks registered");
    
    // Initialize UI integration
    dc_ui_callbacks_t ui_callbacks = {
        .on_screen_changed = ui_screen_changed_handler,
        .on_boot_complete = ui_boot_complete_handler
    };
    
    ret = dc_ui_integration_init(&guider_ui, &ui_callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI integration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize buttons
    ret = dc_buttons_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize buttons: %s", esp_err_to_name(ret));
        dc_ui_integration_deinit();
        return ret;
    }
    
    // Set up callbacks
    dc_callbacks_t callbacks = {
        .state_change_cb = state_change_handler,
        .display_update_cb = display_update_handler,
        .param_change_cb = parameter_change_handler,
        .user_data = NULL
    };
    
    // Initialize state machine
    ret = dc_state_machine_init(&callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize state machine: %s", esp_err_to_name(ret));
        dc_buttons_deinit();
        dc_ui_integration_deinit();
        return ret;
    }
    
    // Initialize devices from AC control (this will be called again after boot complete)
    ret = initialize_multiple_devices();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize multiple devices: %s", esp_err_to_name(ret));
        dc_state_machine_deinit();
        dc_buttons_deinit();
        dc_ui_integration_deinit();
        return ret;
    }
    
    // Register button callback
    ret = dc_buttons_register_callback(button_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register button callback: %s", esp_err_to_name(ret));
        dc_state_machine_deinit();
        dc_buttons_deinit();
        dc_ui_integration_deinit();
        return ret;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Device controller initialized successfully with %d devices", 
             dc_state_machine_get_device_count());
    
    return ESP_OK;
}

esp_err_t device_controller_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_started) {
        ESP_LOGW(TAG, "Device controller already started");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // Start UI integration (shows boot screen)
    ret = dc_ui_integration_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UI integration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_started = true;
    ESP_LOGI(TAG, "Device controller started successfully - boot screen displayed");
    
    return ESP_OK;
}

esp_err_t device_controller_stop(void)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Force return to idle state
    dc_state_machine_force_idle();
    
    s_started = false;
    ESP_LOGI(TAG, "Device controller stopped");
    
    return ESP_OK;
}

esp_err_t device_controller_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_started) {
        device_controller_stop();
    }
    
    // Deinitialize in reverse order
    dc_state_machine_deinit();
    dc_buttons_deinit();
    dc_ui_integration_deinit();
    
    s_initialized = false;
    ESP_LOGI(TAG, "Device controller deinitialized");
    
    return ESP_OK;
}

esp_err_t device_controller_refresh_device_status(uint8_t device_index)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint16_t device_addr = get_device_addr_by_index(device_index);
    if (device_addr == ESP_BLE_MESH_ADDR_UNASSIGNED) {
        ESP_LOGE(TAG, "Invalid device index %d", device_index);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!ac_is_server_online(device_addr)) {
        ESP_LOGW(TAG, "Device 0x%04X is offline, cannot refresh status", device_addr);
        return ESP_ERR_INVALID_STATE;
    }
    
    sync_single_device_status(device_addr);
    return ESP_OK;
}

esp_err_t device_controller_refresh_all_devices_status(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    sync_all_devices_status();
    return ESP_OK;
} 