# Device Controller UI Integration

这个模块实现了设备控制器与LVGL UI界面的完整集成，包括开机Logo画面、按键交互和状态显示。

## 功能特点

### 🚀 开机画面管理
- **Logo显示**：开机时自动显示Logo画面（`screen`）
- **自动切换**：在配置的时间后自动切换到主界面
- **动画效果**：支持淡入淡出过渡动画

### 🎮 按键交互集成
- **完整状态机**：支持空闲、菜单导航、数值调节三种状态
- **实时响应**：按键事件直接映射到UI更新
- **视觉反馈**：选中参数闪烁显示

### 📱 智能显示管理
- **设备状态显示**：温度、电源、模式、风速等实时更新
- **连接状态指示**：心形图标显示设备连接状态
- **临时消息**：支持弹出式消息显示（如"SET OK"）

## 架构设计

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   主应用程序     │    │  设备控制器     │    │   UI界面       │
│   app_main.c    │    │ device_controller│    │  gui_guider    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         └───────────────────────┼───────────────────────┘
                                 │
                   ┌─────────────────┐
                   │  UI集成模块     │
                   │ui_integration.c │
                   └─────────────────┘
```

## 屏幕管理

### 屏幕类型
- `DC_UI_SCREEN_BOOT` - 开机Logo画面
- `DC_UI_SCREEN_MAIN` - 主设备状态显示
- `DC_UI_SCREEN_MENU` - 菜单导航模式
- `DC_UI_SCREEN_MESSAGE` - 临时消息显示

### 画面元素映射
- `screen_1_DeviceIndex` - 设备编号显示
- `screen_1_TempNum` - 温度数值
- `screen_1_OnOff` - 电源状态
- `screen_1_Mode` - 运行模式
- `screen_1_speed1/2/3` - 风速指示灯
- `screen_1_HeartReal/Empty` - 连接状态心形图标

## 配置选项

### Kconfig配置
```kconfig
CONFIG_DEVICE_CONTROLLER_BOOT_DISPLAY_TIME_MS=3000  # 开机画面显示时间
```

### 编译时配置
```c
#define DEVICE_CONTROLLER_BOOT_DISPLAY_TIME_MS  3000  // 开机时间(毫秒)
```

## 使用方法

### 1. 初始化流程
```c
#include "device_controller.h"
#include "gui_guider.h"

void app_main(void) {
    // 1. 初始化LVGL
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    
    // 2. 设置UI界面
    setup_ui(&guider_ui);
    
    // 3. 初始化设备控制器（自动包含UI集成）
    device_controller_init();
    
    // 4. 启动设备控制器（显示开机画面）
    device_controller_start();
    
    // 5. 主循环
    while(1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### 2. 开机流程
1. **启动**：调用`device_controller_start()`
2. **Logo显示**：自动显示开机Logo画面
3. **定时切换**：3秒后自动切换到主界面
4. **状态更新**：开始正常的设备状态显示

### 3. 按键交互流程
1. **空闲状态**：显示设备状态，支持设备切换
2. **双击中心键**：进入菜单导航模式
3. **上下键导航**：切换参数选择（闪烁显示）
4. **单击中心键**：进入数值调节模式
5. **左右键调节**：修改参数值（闪烁显示新值）
6. **单击中心键确认**：保存并返回空闲状态

## API接口

### 初始化
```c
esp_err_t dc_ui_integration_init(lv_ui *ui, const dc_ui_callbacks_t *callbacks);
esp_err_t dc_ui_integration_start(void);
```

### 显示更新
```c
esp_err_t dc_ui_integration_update_display(const dc_context_t *context);
esp_err_t dc_ui_integration_show_message(const char *message, uint32_t duration_ms);
```

### 屏幕控制
```c
esp_err_t dc_ui_integration_show_main_screen(void);
esp_err_t dc_ui_integration_show_menu_navigation(dc_parameter_t selected_param, bool is_blinking);
```

### 回调函数
```c
typedef struct {
    void (*on_screen_changed)(dc_ui_screen_t screen);    // 屏幕切换回调
    void (*on_boot_complete)(void);                      // 开机完成回调
} dc_ui_callbacks_t;
```

## 视觉效果

### 闪烁动画
- **参数选择**：选中的参数以500ms间隔闪烁
- **数值调节**：正在调节的数值闪烁显示
- **停止闪烁**：退出菜单模式时恢复正常显示

### 状态指示
- **电源状态**：ON(绿色) / OFF(红色)
- **连接状态**：实心❤️(已连接) / 空心♡(未连接)
- **风速等级**：1-3个风速图标亮度表示等级

### 动画过渡
- **开机切换**：Logo → 主界面（淡入效果，500ms）
- **消息显示**：半透明黑底白字，居中显示
- **参数闪烁**：透明度在30%和100%之间切换

## 故障排除

### 常见问题
1. **开机画面不显示**：检查`setup_scr_screen()`函数实现
2. **按键无响应**：确认按键模块正确初始化
3. **界面不更新**：检查LVGL定时器是否正常运行
4. **闪烁效果异常**：确认ESP定时器创建成功

### 调试日志
```c
// 启用日志级别
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

// 关键日志标签
- "DC_UI_INTEGRATION" - UI集成模块
- "DEVICE_CONTROLLER" - 设备控制器主模块
- "DC_DISPLAY" - 显示管理模块
```

## 扩展开发

### 添加新屏幕
1. 在`dc_ui_screen_t`枚举中添加新类型
2. 实现对应的设置函数
3. 在`dc_ui_integration_update_display()`中处理新屏幕

### 自定义动画
1. 修改`show_boot_screen()`中的动画参数
2. 调整`blink_timer_callback()`中的闪烁效果
3. 使用`ui_animation()`函数创建自定义动画

### 多语言支持
1. 修改字符串显示函数（如`get_mode_string()`）
2. 根据配置选择不同语言字符串
3. 更新UI文本内容

## 性能优化

- **定时器管理**：及时停止不需要的定时器
- **内存使用**：消息标签用完即删除
- **更新频率**：避免不必要的UI更新
- **动画效果**：合理设置动画时长和帧率 