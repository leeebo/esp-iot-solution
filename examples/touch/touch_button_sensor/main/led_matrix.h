/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define LED_MATRIX_WIDTH 8
#define LED_MATRIX_HEIGHT 8
#define LED_MATRIX_GPIO 22

// ---- Effect Selection ----
// Set LED_MATRIX_EFFECT to choose the overlay animation style.
// Can be overridden in CMakeLists.txt with -DLED_MATRIX_EFFECT=N
#define LED_MATRIX_EFFECT_NONE 0    // No effects, number only
#define LED_MATRIX_EFFECT_RADIAL 1  // Radial Pulse Ring: expanding rings from center
#define LED_MATRIX_EFFECT_NOISE 2   // Noise Bloom: Perlin noise organic bloom
#define LED_MATRIX_EFFECT_GLOW 3    // Center Glow: inner glow expanding outward
#define LED_MATRIX_EFFECT_PALETTE 4 // Palette Sweep: rainbow spiral from center

#ifndef LED_MATRIX_EFFECT
#define LED_MATRIX_EFFECT LED_MATRIX_EFFECT_RADIAL // Default effect
#endif

/**
 * @brief Initialize the LED matrix
 */
void led_matrix_init(void);

/**
 * @brief Show a number on the LED matrix (sets state for animation, does not draw immediately)
 *
 * @param number The number to show (0-9), or -1 to clear
 * @param color_hex Hex color (e.g. 0xFF0000 for red)
 */
void led_matrix_show_number(int number, uint32_t color_hex);

/**
 * @brief Clear the LED matrix (let animation fade out naturally)
 */
void led_matrix_clear(void);

/**
 * @brief Set the animation energy/intensity for a channel (0-255)
 */
void led_matrix_set_energy(int channel, uint8_t energy);

/**
 * @brief Update the LED matrix animation frame (call at ~50 FPS)
 */
void led_matrix_update(void);

/**
 * @brief Fill the entire LED matrix with a solid color
 *
 * @param color_hex Hex color (e.g. 0xFF0000 for red)
 */
void led_matrix_fill_color(uint32_t color_hex);

/**
 * @brief Set the global brightness of the LED matrix
 *
 * @param brightness Brightness value (0-255)
 */
void led_matrix_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif // LED_MATRIX_H
