/* main.c - Application main entry point */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "board.h"
#include "ble_mesh_example_init.h" // For bluetooth_init() and ble_mesh_get_dev_uuid() if still used here
#include "mesh_common.h" // For common definitions like MY_COMPANY_ID etc.
#include "ac_control.h"  // For ac_server_init()

#define TAG_MAIN "MAIN_APP" // Renamed TAG to avoid conflict

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG_MAIN, "Initializing Application...");

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    board_init();

    err = bluetooth_init(); // Initializes BT controller and Bluedroid stack
    if (err) {
        ESP_LOGE(TAG_MAIN, "Bluetooth_init failed (err %d)", err);
        return;
    }

    /* 
     * Initialize the AC BLE Mesh Server Module.
     * This function now handles all BLE Mesh stack initialization,
     * model registration, and callback setup.
     */
    err = ac_server_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "AC BLE Mesh Server Module init failed (err %d)", err);
        // Handle initialization failure (e.g., restart, error state)
        return;
    }

    ESP_LOGI(TAG_MAIN, "Application initialization complete. AC Server is running.");
}
