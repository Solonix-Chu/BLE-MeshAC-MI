# Device Controller 组件使用说明

## 概述

Device Controller是一个ESP-IDF组件，提供基于按键的设备控制界面。支持上/下/左/右导航按键和中心按键（单击/双击），实现状态机驱动的菜单导航和参数调节功能。

## 功能特性

- **三种状态模式**：
  - `S_IDLE`：主显示模式，显示设备状态
  - `S_MENU_NAVIGATE`：菜单导航模式，参数选择
  - `S_VALUE_ADJUST`：数值调节模式
- **按键操作**：支持5个按键（上/下/左/右/中心）
- **中心按键**：单击和双击检测
- **多设备支持**：可管理多个设备（可配置）
- **超时机制**：自动返回主界面
- **回调系统**：状态变化、显示更新、参数变化通知

## 文件结构

```
components/device_controller/
├── CMakeLists.txt                    # 组件构建配置
├── Kconfig.projbuild                 # 组件配置选项
├── device_controller.h               # 主API接口
├── device_controller.c               # 主实现
├── device_controller_types.h         # 数据类型定义
├── device_controller_config.h        # 配置宏定义
├── device_controller_state_machine.h # 状态机接口
├── device_controller_state_machine.c # 状态机实现
├── device_controller_buttons.h       # 按键管理接口
├── device_controller_buttons.c       # 按键管理实现
├── device_controller_display.h       # 显示接口
├── device_controller_display.c       # 显示实现
└── README.md                         # 本说明文档
```

## 快速开始

### 1. 添加组件依赖

在你的项目的 `main/CMakeLists.txt` 中添加组件依赖：

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES device_controller)
```

### 2. 配置GPIO和参数

运行 `idf.py menuconfig`，在 "Device Controller Configuration" 菜单中配置：

- **按键GPIO引脚**：UP/DOWN/LEFT/RIGHT/CENTER按键的GPIO编号
- **按键参数**：按键有效电平、长按时间、双击时间等
- **超时时间**：菜单和调节模式的超时时间
- **多设备支持**：是否启用多设备管理

### 3. 基本使用示例

```c
#include "device_controller.h"
#include "device_controller_types.h"

// 状态变化回调
void on_state_change(dc_state_t old_state, dc_state_t new_state, void *user_data) {
    printf("State changed from %d to %d\n", old_state, new_state);
}

// 显示更新回调
void on_display_update(dc_context_t *context, void *user_data) {
    printf("Display update - State: %d, Device: %d\n", 
           context->current_state, context->current_device_idx);
}

// 参数变化回调
esp_err_t on_param_change(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data) {
    printf("Device %d - Parameter %d changed to %ld\n", device_id, param, value);
    // 在这里实现实际的设备控制逻辑
    return ESP_OK;
}

void app_main() {
    // 初始化组件
    esp_err_t ret = device_controller_init();
    if (ret != ESP_OK) {
        printf("Device controller init failed: %s\n", esp_err_to_name(ret));
        return;
    }

    // 启动组件
    ret = device_controller_start();
    if (ret != ESP_OK) {
        printf("Device controller start failed: %s\n", esp_err_to_name(ret));
        return;
    }

    printf("Device controller started successfully\n");
}
```

## API 参考

### 主要函数

#### `device_controller_init()`
初始化设备控制器组件。

**返回值：**
- `ESP_OK`：成功
- 其他：错误码

#### `device_controller_start()`
启动设备控制器服务。

**返回值：**
- `ESP_OK`：成功
- 其他：错误码

#### `device_controller_stop()`
停止设备控制器服务。

**返回值：**
- `ESP_OK`：成功
- 其他：错误码

#### `device_controller_deinit()`
反初始化设备控制器组件。

**返回值：**
- `ESP_OK`：成功
- 其他：错误码

## 状态机说明

### 状态转换图

```
       [中心键双击]
S_IDLE ════════════> S_MENU_NAVIGATE
  ↑                        ↓ [中心键单击]
  ↑                        ↓
  ↑ [中心键单击/双击/超时]   ↓
  ↑                        ↓
  ↑                  S_VALUE_ADJUST
  ↑                        ↓
  ↑ [中心键单击/双击/超时]   ↓
  ↑                        ↓
  ↑════════════════════════↓
```

### 状态说明

1. **S_IDLE（主显示状态）**
   - 显示当前设备状态信息
   - UP/DOWN：切换设备（多设备模式）
   - CENTER双击：进入菜单导航
   - LEFT/RIGHT：无操作

2. **S_MENU_NAVIGATE（菜单导航状态）**
   - 参数选择界面，闪烁显示当前选择
   - UP/DOWN：切换参数（Power→Temperature→Fan Speed→Mode）
   - CENTER单击：进入数值调节
   - CENTER双击/超时：返回主界面
   - LEFT/RIGHT：无操作

3. **S_VALUE_ADJUST（数值调节状态）**
   - 调节选定参数的数值
   - UP/DOWN：增加/减少数值
   - CENTER单击：保存并返回主界面
   - CENTER双击/超时：放弃修改，返回主界面
   - LEFT/RIGHT：无操作

## 回调函数

### 状态变化回调
```c
typedef void (*dc_state_change_cb_t)(dc_state_t old_state, dc_state_t new_state, void *user_data);
```
在状态机状态发生变化时被调用。

### 显示更新回调
```c
typedef void (*dc_display_update_cb_t)(dc_context_t *context, void *user_data);
```
当显示内容需要更新时被调用，提供当前上下文信息。

### 参数变化回调
```c
typedef esp_err_t (*dc_param_change_cb_t)(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data);
```
当参数值被修改并确认时被调用，用于执行实际的设备控制。

## 配置选项

| 配置项 | 说明 | 默认值 | 范围 |
|--------|------|--------|------|
| `DEVICE_CONTROLLER_BUTTON_UP_GPIO` | UP按键GPIO | 0 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_DOWN_GPIO` | DOWN按键GPIO | 1 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_LEFT_GPIO` | LEFT按键GPIO | 2 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_RIGHT_GPIO` | RIGHT按键GPIO | 3 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_CENTER_GPIO` | CENTER按键GPIO | 4 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_ACTIVE_LEVEL` | 按键有效电平 | 0 | 0-1 |
| `DEVICE_CONTROLLER_LONG_PRESS_TIME_MS` | 长按时间 | 1000ms | 500-5000ms |
| `DEVICE_CONTROLLER_SHORT_PRESS_TIME_MS` | 短按时间 | 180ms | 50-500ms |
| `DEVICE_CONTROLLER_DOUBLE_CLICK_TIME_MS` | 双击时间 | 500ms | 100-1000ms |
| `DEVICE_CONTROLLER_TIMEOUT_MS` | 菜单超时 | 30000ms | 5000-60000ms |
| `DEVICE_CONTROLLER_ENABLE_MULTI_DEVICE` | 多设备支持 | 是 | 是/否 |
| `DEVICE_CONTROLLER_MAX_DEVICES` | 最大设备数 | 3 | 1-10 |

## 参数类型

### 支持的参数
- **Power（电源）**：布尔值，开/关
- **Temperature（温度）**：整数值，可配置范围
- **Fan Speed（风速）**：枚举值，多档位
- **Mode（模式）**：枚举值，制冷/制热/除湿等

### 数据类型
- `DC_VALUE_TYPE_BOOL`：布尔值
- `DC_VALUE_TYPE_INT`：整数值（带范围限制）
- `DC_VALUE_TYPE_ENUM`：枚举值（预定义选项）

## 注意事项

1. **GPIO配置**：确保配置的GPIO引脚未被其他组件使用
2. **按键硬件**：按键应配置为上拉或下拉，与`BUTTON_ACTIVE_LEVEL`配置匹配
3. **内存使用**：组件会创建定时器和任务，注意内存分配
4. **线程安全**：回调函数在不同任务中执行，注意线程安全
5. **错误处理**：在参数变化回调中返回错误码以指示操作失败
6. **Mesh协议集成**：组件已集成`mesh_common.h`中的空调控制宏定义，确保参数值与Bluetooth Mesh协议兼容

### Mesh协议参数映射

组件使用`mesh_common.h`中定义的以下宏：

**电源状态**：
- `AC_POWER_OFF (0)` - 关闭
- `AC_POWER_ON (1)` - 开启

**温度范围**：
- `AC_TEMP_MIN (16)` - 最低温度16°C
- `AC_TEMP_MAX (30)` - 最高温度30°C

**风速设置**：
- `AC_FAN_SPEED_LOW (0)` - 低速
- `AC_FAN_SPEED_MEDIUM (1)` - 中速
- `AC_FAN_SPEED_HIGH (2)` - 高速

**工作模式**：
- `AC_MODE_COOL (0)` - 制冷
- `