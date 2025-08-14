# Device Controller Component User Guide

## Overview

The Device Controller is an ESP-IDF component that provides a button-based device control interface. It supports UP/DOWN/LEFT/RIGHT navigation buttons and a CENTER button (single/double click), implementing state machine-driven menu navigation and parameter adjustment functionality.

## Features

- **Three State Modes**:
  - `S_IDLE`: Main display mode showing device status
  - `S_MENU_NAVIGATE`: Menu navigation mode for parameter selection
  - `S_VALUE_ADJUST`: Value adjustment mode
- **Button Operations**: Supports 5 buttons (UP/DOWN/LEFT/RIGHT/CENTER)
- **Center Button**: Single and double click detection
- **Multi-Device Support**: Can manage multiple devices (configurable)
- **Timeout Mechanism**: Automatic return to main interface
- **Callback System**: State change, display update, and parameter change notifications

## File Structure

```
components/device_controller/
├── CMakeLists.txt                    # Component build configuration
├── Kconfig.projbuild                 # Component configuration options
├── device_controller.h               # Main API interface
├── device_controller.c               # Main implementation
├── device_controller_types.h         # Data type definitions
├── device_controller_config.h        # Configuration macros
├── device_controller_state_machine.h # State machine interface
├── device_controller_state_machine.c # State machine implementation
├── device_controller_buttons.h       # Button management interface
├── device_controller_buttons.c       # Button management implementation
├── device_controller_display.h       # Display interface
├── device_controller_display.c       # Display implementation
├── README.md                         # Chinese documentation
└── README_EN.md                      # This English documentation
```

## Quick Start

### 1. Add Component Dependency

Add the component dependency in your project's `main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES device_controller)
```

### 2. Configure GPIO and Parameters

Run `idf.py menuconfig` and configure in the "Device Controller Configuration" menu:

- **Button GPIO Pins**: GPIO numbers for UP/DOWN/LEFT/RIGHT/CENTER buttons
- **Button Parameters**: Button active level, long press time, double click time, etc.
- **Timeout**: Timeout for menu and adjustment modes
- **Multi-Device Support**: Enable/disable multi-device management

### 3. Basic Usage Example

```c
#include "device_controller.h"
#include "device_controller_types.h"

// State change callback
void on_state_change(dc_state_t old_state, dc_state_t new_state, void *user_data) {
    printf("State changed from %d to %d\n", old_state, new_state);
}

// Display update callback
void on_display_update(dc_context_t *context, void *user_data) {
    printf("Display update - State: %d, Device: %d\n", 
           context->current_state, context->current_device_idx);
}

// Parameter change callback
esp_err_t on_param_change(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data) {
    printf("Device %d - Parameter %d changed to %ld\n", device_id, param, value);
    // Implement actual device control logic here
    return ESP_OK;
}

void app_main() {
    // Initialize component
    esp_err_t ret = device_controller_init();
    if (ret != ESP_OK) {
        printf("Device controller init failed: %s\n", esp_err_to_name(ret));
        return;
    }

    // Start component
    ret = device_controller_start();
    if (ret != ESP_OK) {
        printf("Device controller start failed: %s\n", esp_err_to_name(ret));
        return;
    }

    printf("Device controller started successfully\n");
}
```

## API Reference

### Main Functions

#### `device_controller_init()`
Initialize the device controller component.

**Return Value:**
- `ESP_OK`: Success
- Other: Error code

#### `device_controller_start()`
Start the device controller service.

**Return Value:**
- `ESP_OK`: Success
- Other: Error code

#### `device_controller_stop()`
Stop the device controller service.

**Return Value:**
- `ESP_OK`: Success
- Other: Error code

#### `device_controller_deinit()`
Deinitialize the device controller component.

**Return Value:**
- `ESP_OK`: Success
- Other: Error code

## State Machine Description

### State Transition Diagram

```
       [Center Double Click]
S_IDLE ════════════> S_MENU_NAVIGATE
  ↑                        ↓ [Center Single Click]
  ↑                        ↓
  ↑ [Center Click/Double/Timeout] ↓
  ↑                        ↓
  ↑                  S_VALUE_ADJUST
  ↑                        ↓
  ↑ [Center Click/Double/Timeout] ↓
  ↑                        ↓
  ↑════════════════════════↓
```

### State Descriptions

1. **S_IDLE (Main Display State)**
   - Display current device status information
   - UP/DOWN: Switch devices (multi-device mode)
   - CENTER double click: Enter menu navigation
   - LEFT/RIGHT: No operation

2. **S_MENU_NAVIGATE (Menu Navigation State)**
   - Parameter selection interface with blinking current selection
   - UP/DOWN: Switch parameters (Power→Temperature→Fan Speed→Mode)
   - CENTER single click: Enter value adjustment
   - CENTER double click/timeout: Return to main interface
   - LEFT/RIGHT: No operation

3. **S_VALUE_ADJUST (Value Adjustment State)**
   - Adjust the value of selected parameter
   - UP/DOWN: Increase/decrease value
   - CENTER single click: Save and return to main interface
   - CENTER double click/timeout: Discard changes, return to main interface
   - LEFT/RIGHT: No operation

## Callback Functions

### State Change Callback
```c
typedef void (*dc_state_change_cb_t)(dc_state_t old_state, dc_state_t new_state, void *user_data);
```
Called when the state machine state changes.

### Display Update Callback
```c
typedef void (*dc_display_update_cb_t)(dc_context_t *context, void *user_data);
```
Called when display content needs to be updated, providing current context information.

### Parameter Change Callback
```c
typedef esp_err_t (*dc_param_change_cb_t)(uint8_t device_id, dc_parameter_t param, int32_t value, void *user_data);
```
Called when parameter values are modified and confirmed, used to execute actual device control.

## Configuration Options

| Configuration | Description | Default | Range |
|---------------|-------------|---------|-------|
| `DEVICE_CONTROLLER_BUTTON_UP_GPIO` | UP button GPIO | 0 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_DOWN_GPIO` | DOWN button GPIO | 1 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_LEFT_GPIO` | LEFT button GPIO | 2 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_RIGHT_GPIO` | RIGHT button GPIO | 3 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_CENTER_GPIO` | CENTER button GPIO | 4 | 0-48 |
| `DEVICE_CONTROLLER_BUTTON_ACTIVE_LEVEL` | Button active level | 0 | 0-1 |
| `DEVICE_CONTROLLER_LONG_PRESS_TIME_MS` | Long press time | 1000ms | 500-5000ms |
| `DEVICE_CONTROLLER_SHORT_PRESS_TIME_MS` | Short press time | 180ms | 50-500ms |
| `DEVICE_CONTROLLER_DOUBLE_CLICK_TIME_MS` | Double click time | 500ms | 100-1000ms |
| `DEVICE_CONTROLLER_TIMEOUT_MS` | Menu timeout | 30000ms | 5000-60000ms |
| `DEVICE_CONTROLLER_ENABLE_MULTI_DEVICE` | Multi-device support | Yes | Yes/No |
| `DEVICE_CONTROLLER_MAX_DEVICES` | Maximum devices | 3 | 1-10 |

## Parameter Types

### Supported Parameters
- **Power**: Boolean value, on/off
- **Temperature**: Integer value with configurable range
- **Fan Speed**: Enumerated value, multiple levels
- **Mode**: Enumerated value, cooling/heating/dehumidifying, etc.

### Data Types
- `DC_VALUE_TYPE_BOOL`: Boolean value
- `DC_VALUE_TYPE_INT`: Integer value (with range limits)
- `DC_VALUE_TYPE_ENUM`: Enumerated value (predefined options)

## Important Notes

1. **GPIO Configuration**: Ensure configured GPIO pins are not used by other components
2. **Button Hardware**: Buttons should be configured with pull-up or pull-down to match `BUTTON_ACTIVE_LEVEL` configuration
3. **Memory Usage**: Component creates timers and tasks, pay attention to memory allocation
4. **Thread Safety**: Callback functions execute in different tasks, ensure thread safety
5. **Error Handling**: Return error codes in parameter change callbacks to indicate operation failures

## Example Project

Refer to the example code in the `main` directory to understand how to integrate and use the device_controller component.

## Troubleshooting

### Common Issues

1. **Button Not Responding**
   - Check if GPIO configuration is correct
   - Check button active level setting
   - Confirm hardware connections are normal

2. **State Machine Abnormal**
   - Check callback function implementation
   - Review log output
   - Confirm timeout configuration is reasonable

3. **Compilation Errors**
   - Confirm component dependencies are correctly configured
   - Check `CMakeLists.txt` files
   - Verify include paths

### Debugging Suggestions

- Enable ESP_LOG debug output
- Use oscilloscope to check button signals
- Gradually verify each module's functionality
- Check timer and task running status 