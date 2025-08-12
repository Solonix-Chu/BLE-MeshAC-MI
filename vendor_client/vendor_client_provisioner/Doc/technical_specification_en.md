# Technical Specification: Device Control Interface

**Version:** 1.0
**Date:** 2024-05-16

**1. Introduction**

This document outlines the technical specifications for the state machine governing the user interface of the device controller. The interface utilizes a set of physical buttons (Up, Down, Left, Right, and a multi-function Center button capable of single and double clicks) to navigate through different operational states and adjust device settings.

**2. System States**

The system operates based on three primary states:

*   **`S_IDLE` (Idle / Main Display Mode):** The default operational state.
    *   **Display:** Shows the current status of the connected device(s), including parameters like temperature, on/off status, current mode, etc.
    *   **Associated Data:** Current device information, list of managed devices (if applicable).
*   **`S_MENU_NAVIGATE` (Menu Navigation / Settings Selection Mode):** Allows the user to select a parameter they wish to modify.
    *   **Display:** Indicates entry into settings mode. The currently selectable parameter (e.g., Power, Mode, Fan Speed, Temperature) will be highlighted or flash.
    *   **Associated Data:** List of configurable parameters, index of the currently highlighted parameter.
*   **`S_VALUE_ADJUST` (Parameter Adjustment Mode):** Allows the user to change the value of the parameter selected in `S_MENU_NAVIGATE`.
    *   **Display:** Shows the currently selected parameter and its editable value.
    *   **Associated Data:** The parameter currently being edited (`selected_parameter`), its current value being modified (`editing_value`).

**3. Input Events**

The following input events trigger state transitions and actions:

*   **`EV_UP_PRESS`**: Up button pressed.
*   **`EV_DOWN_PRESS`**: Down button pressed.
*   **`EV_LEFT_PRESS`**: Left button pressed.
*   **`EV_RIGHT_PRESS`**: Right button pressed.
*   **`EV_CENTER_SINGLE_CLICK`**: Center button single-clicked.
*   **`EV_CENTER_DOUBLE_CLICK`**: Center button double-clicked.
*   **`EV_TIMEOUT`**: A predefined period of inactivity.

**4. State Transitions and Actions**

The following table details the state transitions, triggering events, conditions, actions performed, and the next state.

| Current State       | Event / Input             | Guard Condition                      | Actions                                                                                                                                                                                             | Next State        |
| :------------------ | :------------------------ | :----------------------------------- | :-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---------------- |
| **`S_IDLE`**        | `EV_CENTER_DOUBLE_CLICK`  | -                                    | 1. Initialize menu: Set the first parameter (e.g., "Power") as the `current_selection`. <br>2. Update display: Highlight/flash `current_selection`.                                               | `S_MENU_NAVIGATE` |
| `S_IDLE`            | `EV_UP_PRESS` / `EV_LEFT_PRESS` | Multi-device supported             | 1. Switch to the previous device in the managed list. <br>2. Refresh display with the new device's information.                                                                                 | `S_IDLE` (refresh)|
| `S_IDLE`            | `EV_DOWN_PRESS` / `EV_RIGHT_PRESS`| Multi-device supported             | 1. Switch to the next device in the managed list. <br>2. Refresh display with the new device's information.                                                                                     | `S_IDLE` (refresh)|
| **`S_MENU_NAVIGATE`** | `EV_UP_PRESS` / `EV_LEFT_PRESS` | -                                    | 1. Cycle `current_selection` to the previous parameter in the defined order (e.g., Power <- Mode <- Fan Speed <- Temperature <- Power...).<br>2. Update display: Flash new `current_selection`.     | `S_MENU_NAVIGATE` |
| `S_MENU_NAVIGATE`   | `EV_DOWN_PRESS` / `EV_RIGHT_PRESS`| -                                    | 1. Cycle `current_selection` to the next parameter in the defined order (e.g., Power -> Temperature -> Fan Speed -> Mode -> Power...).<br>2. Update display: Flash new `current_selection`.          | `S_MENU_NAVIGATE` |
| `S_MENU_NAVIGATE`   | `EV_CENTER_SINGLE_CLICK`  | -                                    | 1. Set `selected_parameter` = `current_selection`.<br>2. Read `selected_parameter`'s current value into `editing_value`.<br>3. Update display to show `selected_parameter` and `editing_value`. | `S_VALUE_ADJUST`  |
| `S_MENU_NAVIGATE`   | `EV_TIMEOUT`              | Configurable timeout enabled         | 1. Discard any intermediate selection state.                                                                                                                                                        | `S_IDLE`          |
| `S_MENU_NAVIGATE`   | `EV_CENTER_DOUBLE_CLICK`  | -                                    | 1. Discard any intermediate selection state.                                                                                                                                                        | `S_IDLE`          |
| **`S_VALUE_ADJUST`**  | `EV_UP_PRESS` / `EV_LEFT_PRESS` | -                                    | 1. Modify `editing_value` based on `selected_parameter`'s type (e.g., increment, decrement, toggle, cycle next option).<br>2. Update display to show the new `editing_value`.                     | `S_VALUE_ADJUST`  |
| `S_VALUE_ADJUST`  | `EV_DOWN_PRESS` / `EV_RIGHT_PRESS`| -                                    | 1. Modify `editing_value` based on `selected_parameter`'s type (e.g., decrement, increment, toggle, cycle previous option).<br>2. Update display to show the new `editing_value`.                 | `S_VALUE_ADJUST`  |
| `S_VALUE_ADJUST`  | `EV_CENTER_SINGLE_CLICK`  | -                                    | 1. Apply `editing_value` to `selected_parameter`.<br>2. Persist the change (e.g., send command to hardware/system).<br>3. (Optional) Display confirmation message (e.g., "SET OK") briefly.<br>4. Return to main display. | `S_IDLE`          |
| `S_VALUE_ADJUST`  | `EV_TIMEOUT`              | Configurable timeout enabled         | 1. Discard `editing_value` (changes are not saved).                                                                                                                                              | `S_IDLE`          |
| `S_VALUE_ADJUST`  | `EV_CENTER_DOUBLE_CLICK`  | -                                    | 1. Discard `editing_value` (changes are not saved).                                                                                                                                              | `S_IDLE`          |

**5. Parameter Navigation Order (S_MENU_NAVIGATE)**

When in `S_MENU_NAVIGATE` state, the Up/Down (or Left/Right) buttons cycle through the available settings parameters. The default order is:
1.  Power (On/Off)
2.  Temperature
3.  Fan Speed
4.  Mode

The selection wraps around. Upon entering `S_MENU_NAVIGATE` from `S_IDLE`, "Power" is the initially selected/flashing item.

**6. User Experience Considerations**

*   **Timeout:** Configurable timeouts in `S_MENU_NAVIGATE` and `S_VALUE_ADJUST` states will revert the system to `S_IDLE` to prevent it from being stuck in a settings menu. Changes in `S_VALUE_ADJUST` are discarded on timeout.
*   **Quick Exit:** A double-click of the Center button in either `S_MENU_NAVIGATE` or `S_VALUE_ADJUST` will immediately return the system to `S_IDLE`, discarding any unconfirmed changes.
*   **Feedback:** Upon successful application of a setting (transition from `S_VALUE_ADJUST` to `S_IDLE` via Center single click), a brief confirmation message like "SET OK" should be displayed.

**7. Multi-Device Management (Optional)**

If the controller manages multiple devices:
*   In `S_IDLE` state, the Up/Down (or Left/Right) buttons will cycle through the available devices.
*   The display will update to show the status of the currently selected device.
*   All settings adjustments will apply to the device that was selected in `S_IDLE` when the settings mode was entered. 