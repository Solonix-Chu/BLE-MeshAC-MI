/* board.h - Board-specific hooks */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BOARD_H_
#define _BOARD_H_

#ifdef __cplusplus
extern "C" {
#endif /**< __cplusplus */

#include <stdint.h> // For uint8_t
#include "driver/gpio.h" // For GPIO_NUM_48 definition

/* WS2812 LED Configuration */
#define WS2812_LED_GPIO  GPIO_NUM_21
#define WS2812_LED_COUNT 1

/* LED Status/Color representations */
// These can be used as arguments to a new board_led_operation or similar
// Or map them to specific colors in board.c
#define LED_STATE_OFF     0 // Turn LED off
#define LED_STATE_PROV    1 // Provisioning (e.g., Blue)
#define LED_STATE_SUCCESS 2 // Operation Succeeded (e.g., Green)
#define LED_STATE_ERROR   3 // Operation Failed (e.g., Red)
#define LED_STATE_HEARTBEAT 4 // Blinking or specific color for heartbeat

// Old LED_R, LED_G, LED_B, LED_ON, LED_OFF macros are removed or repurposed.
// The old struct _led_state is no longer needed.

/**
 * @brief Initialize the WS2812 LED strip.
 */
void board_ws2812_init(void);

/**
 * @brief Set the color of the WS2812 LED.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 */
void board_ws2812_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off the WS2812 LED (set color to black).
 */
void board_ws2812_turn_off(void);

/**
 * @brief Sets the global brightness for the WS2812 LED.
 * 
 * @param brightness Brightness value from 0 (off) to 255 (full).
 */
void board_ws2812_set_brightness(uint8_t brightness);

/**
 * @brief Perform an LED operation based on a state.
 *        This function will map states to specific WS2812 colors.
 *
 * @param state One of the LED_STATE_ macros (e.g., LED_STATE_PROV)
 */
void board_led_operation(uint8_t state);

/**
 * @brief Initialize the board (currently only initializes WS2812 LED).
 */
void board_init(void);

#ifdef __cplusplus
}
#endif /**< __cplusplus */

#endif /* _BOARD_H_ */
