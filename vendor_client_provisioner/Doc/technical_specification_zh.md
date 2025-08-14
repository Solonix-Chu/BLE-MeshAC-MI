# 技术规格说明：设备控制界面

**版本：** 1.0
**日期：** 2024-05-16

**1. 引言**

本文档概述了设备控制器用户界面状态机的技术规格。该界面利用一组物理按键（上、下、左、右键，以及一个支持单击和双击的多功能中心按键）在不同的操作状态之间导航并调整设备设置。

**2. 系统状态**

系统基于三种主要状态运行：

*   **`S_IDLE`（空闲/主显示模式）：** 默认操作状态。
    *   **显示：** 显示已连接设备的当前状态，包括温度、开关状态、当前模式等参数。
    *   **关联数据：** 当前设备信息，受管设备列表（如果适用）。
*   **`S_MENU_NAVIGATE`（菜单导航/设置选择模式）：** 允许用户选择他们希望修改的参数。
    *   **显示：** 指示已进入设置模式。当前可选择的参数（例如，电源、模式、风速、温度）将被高亮显示或闪烁。
    *   **关联数据：** 可配置参数列表，当前高亮显示参数的索引。
*   **`S_VALUE_ADJUST`（参数调整模式）：** 允许用户更改在 `S_MENU_NAVIGATE` 中选择的参数的值。
    *   **显示：** 显示当前选择的参数及其可编辑的值。
    *   **关联数据：** 当前正在编辑的参数（`selected_parameter`），其当前正在修改的值（`editing_value`）。

**3. 输入事件**

以下输入事件触发状态转换和操作：

*   **`EV_UP_PRESS`**：按下上键。
*   **`EV_DOWN_PRESS`**：按下下键。
*   **`EV_LEFT_PRESS`**：按下左键。
*   **`EV_RIGHT_PRESS`**：按下右键。
*   **`EV_CENTER_SINGLE_CLICK`**：中心按键单击。
*   **`EV_CENTER_DOUBLE_CLICK`**：中心按键双击。
*   **`EV_TIMEOUT`**：预定义的不活动时间段。

**4. 状态转换与操作**

下表详细说明了状态转换、触发事件、条件、执行的操作以及下一个状态。

| 当前状态          | 事件/输入                 | 防护条件                         | 操作                                                                                                                                                                                                  | 下一个状态        |
| :------------------ | :------------------------ | :----------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | :---------------- |
| **`S_IDLE`**        | `EV_CENTER_DOUBLE_CLICK`  | -                                    | 1. 初始化菜单：将第一个参数（例如"电源"）设置为 `current_selection`。<br>2. 更新显示：高亮/闪烁 `current_selection`。                                                               | `S_MENU_NAVIGATE` |
| `S_IDLE`            | `EV_UP_PRESS` / `EV_LEFT_PRESS` | 支持多设备                       | 1. 切换到受管列表中的上一个设备。<br>2. 使用新设备的信息刷新显示。                                                                                                                               | `S_IDLE` (刷新)   |
| `S_IDLE`            | `EV_DOWN_PRESS` / `EV_RIGHT_PRESS`| 支持多设备                       | 1. 切换到受管列表中的下一个设备。<br>2. 使用新设备的信息刷新显示。                                                                                                                               | `S_IDLE` (刷新)   |
| **`S_MENU_NAVIGATE`** | `EV_UP_PRESS` / `EV_LEFT_PRESS` | -                                    | 1. 将 `current_selection` 循环到定义顺序中的上一个参数（例如，电源 <- 模式 <- 风速 <- 温度 <- 电源...）。<br>2. 更新显示：闪烁新的 `current_selection`。                                        | `S_MENU_NAVIGATE` |
| `S_MENU_NAVIGATE`   | `EV_DOWN_PRESS` / `EV_RIGHT_PRESS`| -                                    | 1. 将 `current_selection` 循环到定义顺序中的下一个参数（例如，电源 -> 温度 -> 风速 -> 模式 -> 电源...）。<br>2. 更新显示：闪烁新的 `current_selection`。                                            | `S_MENU_NAVIGATE` |
| `S_MENU_NAVIGATE`   | `EV_CENTER_SINGLE_CLICK`  | -                                    | 1. 设置 `selected_parameter` = `current_selection`。<br>2. 将 `selected_parameter` 的当前值读入 `editing_value`。<br>3. 更新显示以显示 `selected_parameter` 和 `editing_value`。     | `S_VALUE_ADJUST`  |
| `S_MENU_NAVIGATE`   | `EV_TIMEOUT`              | 可配置的超时已启用               | 1. 丢弃任何中间选择状态。                                                                                                                                                                   | `S_IDLE`          |
| `S_MENU_NAVIGATE`   | `EV_CENTER_DOUBLE_CLICK`  | -                                    | 1. 丢弃任何中间选择状态。                                                                                                                                                                   | `S_IDLE`          |
| **`S_VALUE_ADJUST`**  | `EV_UP_PRESS` / `EV_LEFT_PRESS` | -                                    | 1. 根据 `selected_parameter` 的类型修改 `editing_value`（例如，增加、减少、切换、循环到下一个选项）。<br>2. 更新显示以显示新的 `editing_value`。                                                | `S_VALUE_ADJUST`  |
| `S_VALUE_ADJUST`  | `EV_DOWN_PRESS` / `EV_RIGHT_PRESS`| -                                    | 1. 根据 `selected_parameter` 的类型修改 `editing_value`（例如，减少、增加、切换、循环到上一个选项）。<br>2. 更新显示以显示新的 `editing_value`。                                            | `S_VALUE_ADJUST`  |
| `S_VALUE_ADJUST`  | `EV_CENTER_SINGLE_CLICK`  | -                                    | 1. 将 `editing_value` 应用于 `selected_parameter`。<br>2. 持久化更改（例如，向硬件/系统发送命令）。<br>3.（可选）短暂显示确认消息（例如，"SET OK"）。<br>4. 返回主显示。                      | `S_IDLE`          |
| `S_VALUE_ADJUST`  | `EV_TIMEOUT`              | 可配置的超时已启用               | 1. 丢弃 `editing_value`（更改未保存）。                                                                                                                                                               | `S_IDLE`          |
| `S_VALUE_ADJUST`  | `EV_CENTER_DOUBLE_CLICK`  | -                                    | 1. 丢弃 `editing_value`（更改未保存）。                                                                                                                                                               | `S_IDLE`          |

**5. 参数导航顺序 (`S_MENU_NAVIGATE`)**

在 `S_MENU_NAVIGATE` 状态下，上/下（或左/右）按键循环浏览可用的设置参数。默认顺序是：
1.  电源 (开/关)
2.  温度
3.  风速
4.  模式

选择会循环。从 `S_IDLE` 进入 `S_MENU_NAVIGATE` 时，"电源"是初始选择/闪烁的项目。

**6. 用户体验注意事项**

*   **超时：** 在 `S_MENU_NAVIGATE` 和 `S_VALUE_ADJUST` 状态下的可配置超时会将系统恢复到 `S_IDLE`，以防止其卡在设置菜单中。在 `S_VALUE_ADJUST` 中的更改在超时后会被丢弃。
*   **快速退出：** 在 `S_MENU_NAVIGATE` 或 `S_VALUE_ADJUST` 中双击中心按键将立即使系统返回 `S_IDLE`，并丢弃任何未确认的更改。
*   **反馈：** 成功应用设置后（通过中心按键单击从 `S_VALUE_ADJUST` 转换到 `S_IDLE`），应短暂显示确认消息，如 "SET OK"。

**7. 多设备管理（可选）**

如果控制器管理多个设备：
*   在 `S_IDLE` 状态下，上/下（或左/右）按键将循环浏览可用的设备。
*   显示将更新以显示当前所选设备的状态。
*   所有设置调整将应用于进入设置模式时在 `S_IDLE` 中选择的设备。 