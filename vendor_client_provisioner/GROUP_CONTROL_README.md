# BLE Mesh 组播控制功能说明

## 功能概述

本系统实现了基于BLE Mesh的组播控制功能，允许用户通过一个虚拟的"All Device"设备对所有连接的空调设备进行群控操作。

## 主要特性

### 1. 组播地址管理
- **组播地址**: `0xC000`
- **自动配置**: 设备配网完成后自动加入组播组
- **统一管理**: 所有设备使用同一个组播地址

### 2. 虚拟设备界面
- **设备名称**: "All Device"
- **设备位置**: 在设备列表的最后一个位置
- **状态显示**: 始终显示为在线状态
- **操作界面**: 与普通设备相同的控制界面

### 3. 群控功能
- **电源控制**: 同时开启/关闭所有设备
- **温度设置**: 统一设置所有设备的目标温度
- **模式切换**: 同时切换所有设备的运行模式
- **风速调节**: 统一调节所有设备的风速

## 技术实现

### 1. BLE Mesh 组播
```c
// 组播地址定义
#define AC_GROUP_ADDR 0xC000

// 设备自动加入组播组
esp_err_t ac_add_device_to_group(uint16_t device_addr);

// 发送组播命令
esp_err_t ac_send_group_command(ac_status_type_t command_type, uint8_t value);
```

### 2. 虚拟设备管理
```c
// 虚拟设备标识
#define AC_ALL_DEVICE_NAME "All Device"

// 检测虚拟设备
if (device_id == actual_device_count) {
    // 处理群控命令
    ac_send_group_command(ac_param, (uint8_t)value);
}
```

### 3. 设备配置流程
1. 设备配网完成
2. 自动添加到组播组
3. 配置组播订阅
4. 保存状态到NVS

## 使用方法

### 1. 遥控器操作
1. **切换到群控模式**:
   - 使用左右键切换设备
   - 切换到"All Device"设备

2. **群控操作**:
   - 双击进入菜单模式
   - 选择要调节的参数
   - 调节参数值
   - 确认设置

3. **状态反馈**:
   - 显示"GROUP SET OK"表示成功
   - 显示"GROUP SET ERROR"表示失败

### 2. API调用
```c
// 初始化AC控制系统
ac_client_init();

// 发送群控命令
ac_send_group_command(AC_STATUS_POWER, AC_POWER_ON);
ac_send_group_command(AC_STATUS_TEMPERATURE, 26);
ac_send_group_command(AC_STATUS_MODE, AC_MODE_COOL);
ac_send_group_command(AC_STATUS_FAN_SPEED, AC_FAN_SPEED_MEDIUM);

// 检查设备组播状态
bool in_group = ac_is_device_in_group(device_addr);
```

## 配置参数

### 1. 组播地址
```c
#define AC_GROUP_ADDR 0xC000  // 可根据需要修改
```

### 2. 虚拟设备配置
```c
#define AC_ALL_DEVICE_NAME "All Device"  // 可自定义名称
```

### 3. 设备数量限制
- 最大支持设备数量由 `DEVICE_CONTROLLER_MAX_DEVICES` 定义
- 虚拟设备占用一个设备位置

## 状态监控

### 1. 日志输出
```
I (12345) AC_CLIENT: Adding device 0x0005 to multicast group 0xC000
I (12346) AC_CLIENT: Successfully initiated group subscription for device 0x0005
I (12347) DEVICE_CONTROLLER: Added virtual device: "All Device" for group control at index 1
I (12348) DEVICE_CONTROLLER: Processing group control command for All Device
I (12349) AC_CLIENT: Sending group command type 0 with value 1
I (12350) AC_CLIENT: Successfully sent group message type 0 to group 0xC000
```

### 2. 错误处理
- 网络错误: 自动重试机制
- 设备离线: 跳过离线设备
- 配置失败: 记录错误日志

## 测试功能

### 1. 测试程序
```c
// 测试组播功能
void test_group_control_functionality(void);

// 检查设备组播状态
void test_device_group_status(void);
```

### 2. 测试步骤
1. 确保有设备连接
2. 调用测试函数
3. 观察日志输出
4. 验证设备响应

## 注意事项

### 1. 网络性能
- 组播消息不需要响应确认
- 避免频繁发送组播命令
- 建议命令间隔至少500ms

### 2. 设备同步
- 组播命令是单向的
- 无法获取所有设备的状态反馈
- 建议定期同步单个设备状态

### 3. 错误恢复
- 设备重启后需重新加入组播组
- 网络异常时可能需要手动重新配置
- 建议实现自动恢复机制

## 扩展功能

### 1. 分组控制
- 可以定义多个组播地址
- 实现设备分组管理
- 支持层级控制

### 2. 场景模式
- 预设常用的设备组合
- 一键切换场景模式
- 支持定时任务

### 3. 状态同步
- 实现组播状态查询
- 设备状态聚合显示
- 异常设备检测

## 故障排除

### 1. 群控无响应
- 检查设备是否在组播组中
- 验证组播地址配置
- 检查网络连接状态

### 2. 部分设备无响应
- 检查设备在线状态
- 验证设备组播订阅
- 重新添加设备到组播组

### 3. 虚拟设备不显示
- 检查设备初始化顺序
- 验证设备数量配置
- 检查UI更新逻辑 