# MeshAC Mobile Monitor

Flutter 手机监控终端，用于连接 BLE-MeshAC-MI 控制端暴露的普通 BLE GATT Bridge。App 不参与 Mesh 配网，只负责监控设备状态、刷新状态、单设备控制和群组控制。

完整四端说明见 [Doc/四端使用说明.md](../Doc/四端使用说明.md)。

## 连接对象

App 扫描并连接广播名为 `MeshAC Bridge` 的 BLE 设备。该 Bridge 由配网端 `vendor_client/` 或遥控端 `vendor_remote/` 在 `CONFIG_SH_BLE_BRIDGE_ENABLE=y` 时提供。

GATT 协议：

| UUID | 用途 |
| --- | --- |
| Service `0xA100` | Bridge 服务 |
| Characteristic `0xA101` | 设备列表读取 |
| Characteristic `0xA102` | 状态快照读取 |
| Characteristic `0xA103` | 命令写入 |
| Characteristic `0xA104` | 状态通知 |

## 运行

```bash
flutter pub get
flutter devices
flutter run -d <ANDROID_DEVICE_ID>
```

或构建并安装调试 APK：

```bash
flutter build apk --debug
adb install -r build/app/outputs/flutter-apk/app-debug.apk
```

Android 首次运行需要授予蓝牙扫描/连接权限；部分系统还会要求位置权限才能扫描 BLE。

## 使用

1. 启动配网端或遥控端，并确认 `MeshAC Bridge` 正在广播。
2. 打开 App，扫描并选择 `MeshAC Bridge`。
3. 连接成功后 App 会自动读取设备列表和状态快照，并订阅实时状态通知。
4. 在设备列表查看在线状态和 profile，进入详情页修改 feature。
5. 使用刷新入口重新读取设备列表和快照。
6. 使用群控入口下发 groupable feature。

当前 Bridge 只主动通知 feature 变化；设备上下线和 profile 变化需要通过刷新或重连重新读取。
