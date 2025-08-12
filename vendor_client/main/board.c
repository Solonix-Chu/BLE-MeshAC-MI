/* board.c - Board-specific hooks */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "iot_button.h"
#include "button_gpio.h" 
#include "ac_control.h"
#include "mesh_common.h"

#define TAG "BOARD"

#define BUTTON_IO_NUM       0
#define BUTTON_ACTIVE_LEVEL 0

// extern void example_ble_mesh_send_vendor_message(bool resend);

// Callback function signature must match: void (*button_cb_t)(void *button_handle, void *usr_data);
static void button_tap_cb(void *button_handle, void *user_data)
{
    // char* user_str = (char*)user_data;
    // ESP_LOGI(TAG, "Button event: %s", user_str ? user_str : "N/A");
    ESP_LOGI(TAG, "Button (GPIO %d) tap event!", BUTTON_IO_NUM);
    
    // example_ble_mesh_send_vendor_message(false);
    static uint8_t power_state = 0;

    ac_client_set_power(0x0005, power_state);
    power_state = !power_state;
}

static void board_button_init(void)
{
    ESP_LOGI(TAG, "Initializing button on GPIO %d", BUTTON_IO_NUM);

    // 1. Configure the GPIO characteristics for the button
    button_gpio_config_t gpio_btn_cfg = {
        .gpio_num = BUTTON_IO_NUM,
        .active_level = BUTTON_ACTIVE_LEVEL, // Level when button is pressed
        // .disable_pull = false, // Default: internal pull-up/down enabled based on active_level
        // .enable_power_save = false, // Default: power save disabled
    };

    // 2. Configure general button parameters (optional, can use defaults)
    button_config_t btn_cfg = {
        // .short_press_time_ms = CONFIG_BUTTON_SHORT_PRESS_TIME_MS, // Or your desired value
        // .long_press_time_ms = CONFIG_BUTTON_LONG_PRESS_TIME_MS,   // Or your desired value
    };

    button_handle_t btn_handle = NULL; // Initialize to NULL

    // 3. Create the button instance
    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &gpio_btn_cfg, &btn_handle);

    if (err == ESP_OK && btn_handle != NULL) {
        ESP_LOGI(TAG, "Button created successfully.");

        // 4. Register callback for a specific event (e.g., BUTTON_PRESS_UP which is like a release/tap)
        // The 3rd argument (event_args) to iot_button_register_cb must be present (can be NULL).
        // Pass a string or other data as user_data if needed in the callback.
        err = iot_button_register_cb(btn_handle, BUTTON_PRESS_UP, NULL, button_tap_cb, (void *)"BUTTON_TAP_EVENT");
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register button callback: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Failed to create button: %s (handle: %p)", esp_err_to_name(err), btn_handle);
    }
}

void board_init(void)
{
#ifndef CONFIG_WEB_ONLY
    board_button_init();
#endif
    ESP_LOGI(TAG, "Board init (UI/Buttons disabled)");
}
