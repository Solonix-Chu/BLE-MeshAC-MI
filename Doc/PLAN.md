# BLE Mesh 多控制端与手机桥接改造计划

## Summary
- 保留唯一配网设备，由它负责发现、配网、AppKey 绑定、节点配置和设备目录维护。
- 新增通用控制端能力，让配网器、遥控器、网关/中继设备都能获取网络设备状态并下发控制命令。
- 新增普通 BLE GATT 手机桥接服务，让手机 App 通过连接配网器/遥控器/网关读取设备状态、订阅状态变化、下发控制命令。
- 不自动编译；实现完成后只提供手动编译命令，由你手动验证。

## Key Changes
- 把当前 `smarthome_client` 中的“配网管理”和“设备控制/状态缓存”职责拆开：
  - 配网器继续负责未配网设备发现、Mesh 配网、AppKey 添加、Model 绑定、被控设备组订阅。
  - 新增共享 `smarthome_controller` 层，统一管理设备列表、profile 缓存、feature 状态、单设备控制、组控和状态回调。
- 支持多个 Mesh 控制端：
  - 遥控器/网关作为已配网 Mesh 节点加入网络，不拥有配网权限。
  - 配网器为控制端绑定 Vendor Client Model，并同步当前被控设备目录。
  - 控制端接收目录后，对所有被控设备执行 profile/status refresh，完成接入即状态同步。
- 改造被控设备 node 行为：
  - 不再只记录一个 `s_client_addr` 作为唯一控制端。
  - 允许多个控制端发送 `PROFILE_GET`、`FEATURE_GET`、`FEATURE_SET`。
  - feature 改变后发布 `FEATURE_STATUS` 到控制端可订阅的状态组，保证多个遥控器和网关状态最终一致。
- 新增普通 BLE App Bridge：
  - GATT Read：设备列表、单设备状态快照。
  - GATT Notify：设备上线/离线、profile 更新、feature 状态变化。
  - GATT Write：单设备 feature set、组控 feature set、刷新全部状态。
  - Bridge 内部只调用 `smarthome_controller` API，不直接维护 Mesh 状态。
- 保留现有 `ac_control` 兼容层和 UI 调用方式：
  - `ac_control` 底层迁移到 `smarthome_controller`。
  - `device_controller` 和现有 UI 尽量少改，只适配新增控制端状态同步回调。

## Public Interfaces
- 新增 `shared_components/smarthome_mesh/include/smarthome_controller.h`：
  - `sh_controller_init(role, callbacks)`
  - `sh_controller_get_device_list(...)`
  - `sh_controller_get_device_by_addr(...)`
  - `sh_controller_refresh_device(addr)`
  - `sh_controller_refresh_all()`
  - `sh_controller_set_feature(addr, feature_id, value)`
  - `sh_controller_group_set_feature(feature_id, value)`
- 新增 `shared_components/smarthome_ble_bridge`：
  - `sh_ble_bridge_init(...)`
  - `sh_ble_bridge_notify_device_status(...)`
  - GATT 协议第一版使用二进制 TLV，复用现有 `feature_id/type/value` 表达方式。
- 新增角色配置：
  - `PROVISIONER_CONTROLLER`：唯一配网器 + 控制 + 可选手机桥接。
  - `REMOTE_CONTROLLER`：遥控器控制端 + 可选手机桥接。
  - `GATEWAY_RELAY`：Relay + 控制端 + 手机桥接。
- 现有 `sh_client_*` API 尽量保留为兼容 facade，减少上层改动。

## Test Plan
- 不自动运行 `idf.py build`。
- 我完成代码后只做静态阅读检查、符号引用检查、必要的只读搜索确认。
- 你手动验证：
  - `vendor_client` 配网器角色可编译。
  - `vendor_client` 遥控器/网关角色可编译。
  - `vendor_server` 被控设备可编译。
  - 配网器配入多个被控设备后，设备状态能完整同步。
  - 新遥控器加入后能同步所有被控设备状态并控制设备。
  - 手机连接 GATT Bridge 后能读设备列表、收到状态通知、下发控制命令。
  - 多个遥控器/手机控制同一设备后，各端状态最终一致。

## Assumptions
- Mesh 网络内只有一个配网设备。
- 多个遥控器/网关通过配网器授权加入网络，不采用预烧录共享密钥。
- 第一版手机 App 不做 Mesh 配网/移除设备，只做状态与控制。
- 普通 BLE GATT 与 BLE Mesh 共用当前 Bluedroid 配置；如后续出现广播或连接资源冲突，再针对连接参数和广播策略单独优化。
