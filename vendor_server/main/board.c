/* board.c - Board-specific hooks */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "board.h"        // Contains WS2812_LED_GPIO, LED_STATE_ definitions
#include "led_strip.h"    // ESP-IDF RMT LED strip driver
#include "sdkconfig.h"    // For KConfig options if any are relevant (e.g. RMT channel count)

#define TAG "BOARD_RMT_WS2812"

// Global handle for the LED strip
static led_strip_handle_t led_strip;

// Global brightness control (0-255, where 255 is full brightness)
// Let's default to a dimmer value, e.g., 64 for ~25% brightness or 128 for ~50%
static uint8_t g_led_brightness = 16; 

/**
 * @brief Sets the global brightness for the WS2812 LED.
 * 
 * @param brightness Brightness value from 0 (off) to 255 (full).
 */
void board_ws2812_set_brightness(uint8_t brightness)
{
    g_led_brightness = brightness;
    ESP_LOGI(TAG, "Global LED brightness set to %u/255", g_led_brightness);
    // Note: This doesn't immediately update the current LED color.
    // The new brightness will be applied the next time a color is set.
    // If you want immediate update, you might need to re-apply the current color.
}

void board_ws2812_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return;
    }

    // Apply global brightness
    uint8_t final_r = (uint8_t)(((uint32_t)r * g_led_brightness) / 255);
    uint8_t final_g = (uint8_t)(((uint32_t)g * g_led_brightness) / 255);
    uint8_t final_b = (uint8_t)(((uint32_t)b * g_led_brightness) / 255);

    // Set the pixel color for the single LED (index 0)
    esp_err_t err = led_strip_set_pixel(led_strip, 0, final_r, final_g, final_b);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set pixel color (R:%d G:%d B:%d) with brightness %u (err %d)", 
                 final_r, final_g, final_b, g_led_brightness, err);
        return;
    }
    // Refresh the strip to send data
    err = led_strip_refresh(led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to refresh LED strip (err %d)", err);
    }
}

void board_ws2812_turn_off(void)
{
    if (!led_strip) {
        // ESP_LOGE(TAG, "LED strip not initialized, cannot turn off");
        return; // Avoid log spam if called before init or after failed init
    }
    // Clear all pixels (turns them off)
    esp_err_t err = led_strip_clear(led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED strip (err %d)", err);
    }
}

void board_led_operation(uint8_t state)
{
    if (!led_strip && state != LED_STATE_OFF) { // Allow attempting to turn off even if handle is null
        ESP_LOGE(TAG, "LED strip not initialized, cannot perform LED operation for state %d", state);
        return;
    }

    switch (state) {
        case LED_STATE_OFF:
            board_ws2812_turn_off();
            ESP_LOGI(TAG, "LED: Switched OFF");
            break;
        case LED_STATE_PROV: // Provisioning - Blue
            board_ws2812_set_color(0, 0, 255);
            ESP_LOGI(TAG, "LED: Provisioning (Blue)");
            break;
        case LED_STATE_SUCCESS: // Success - Green
            board_ws2812_set_color(0, 255, 0);
            ESP_LOGI(TAG, "LED: Success (Green)");
            break;
        case LED_STATE_ERROR: // Error - Red
            board_ws2812_set_color(255, 0, 0);
            ESP_LOGI(TAG, "LED: Error (Red)");
            break;
        case LED_STATE_HEARTBEAT: // Example: Yellow for heartbeat
            board_ws2812_set_color(255, 255, 0); // Yellow
            ESP_LOGI(TAG, "LED: Heartbeat (Yellow)");
            break;
        default:
            ESP_LOGW(TAG, "Unknown LED state: %d, turning LED off.", state);
            board_ws2812_turn_off();
            break;
    }
}

void board_ws2812_init(void)
{
    ESP_LOGI(TAG, "Initializing WS2812 LED strip using ESP-IDF RMT driver...");

    led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812_LED_GPIO,         // GPIO_NUM_48 from board.h
        .max_leds = WS2812_LED_COUNT,            // 1 LED from board.h
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Common for WS2812
        .led_model = LED_MODEL_WS2812,            // Explicitly WS2812
        .flags.invert_out = false,                // Usually false for WS2812
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,       // Default RMT clock source
        .resolution_hz = 10 * 1000 * 1000,    // 10MHz resolution, standard for WS2812
        // .mem_block_symbols can be left to default for a small number of LEDs by not specifying it,
        // or set it like the blink example if needed for specific ESP32 variants (e.g. 64 for ESP32).
        // For ESP32, default is 64. For others like S2/C3 it might be less.
        // If not specified, the driver uses SOC_RMT_MEM_WORDS_PER_CHANNEL.
        // Let's be explicit for clarity or use a conditional compile for it.
        // For now, let driver use its default by not setting mem_block_symbols.
        .flags.with_dma = false, // DMA not typically needed for 1 LED, keep it simple
    };

    ESP_LOGI(TAG, "Attempting to create RMT device for WS2812 on GPIO %d", WS2812_LED_GPIO);
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create new RMT LED strip (err %d). LED will not function.", err);
        led_strip = NULL; // Ensure handle is NULL if init fails
        return;
    }

    ESP_LOGI(TAG, "WS2812 RMT LED strip initialized successfully.");
    err = led_strip_clear(led_strip); // Turn off LED on successful init
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED strip on init (err %d)", err);
    }
    ESP_LOGI(TAG, "Initial WS2812 LED state: OFF");
}

void board_init(void)
{
    board_ws2812_init();
    ESP_LOGI(TAG, "Board initialized with ESP-IDF RMT WS2812 LED driver.");
}
