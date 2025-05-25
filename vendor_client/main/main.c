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
#include "esp_timer.h"

#include "ble_mesh_example_init.h"
#include "board.h"
#include "ac_control.h"

#define TAG "Client_Main"

void app_main(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing Client...");

    // Initialize NVS Flash - this is a prerequisite for BLE Mesh stack typically.
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    board_init(); // Initialize board specific things (LEDs, buttons etc)

    // Initialize Bluetooth controller and bluedroid stack 
    err = bluetooth_init(); 
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth_init failed (err %d)", err);
        return;
    }
    
    // Initialize AC control and BLE Mesh client functionality
    err = ac_client_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AC BLE Mesh client init failed (err %d)", err);
        return; 
    }

    ESP_LOGI(TAG, "Client Initialization Complete.");

    // Example usage (optional):
    // uint16_t server_addr = ac_get_server_addr();
    // if (server_addr != ESP_BLE_MESH_ADDR_UNASSIGNED) {
    //     ESP_LOGI(TAG, "Attempting to get power status from server 0x%04x", server_addr);
    //     ac_client_get_power(server_addr);
    // } else {
    //     ESP_LOGI(TAG, "No server provisioned yet. Waiting for provisioning...");
    // }
}
