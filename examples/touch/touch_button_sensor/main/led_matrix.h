/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define LED_MATRIX_WIDTH 8
#define LED_MATRIX_HEIGHT 8
#define LED_MATRIX_GPIO 22

// ---- Effect Selection ----
#define LED_MATRIX_EFFECT_NONE 0
#define LED_MATRIX_EFFECT_RADIAL 1
#define LED_MATRIX_EFFECT_NOISE 2
#define LED_MATRIX_EFFECT_GLOW 3
#define LED_MATRIX_EFFECT_PALETTE 4

#ifndef LED_MATRIX_EFFECT
#define LED_MATRIX_EFFECT LED_MATRIX_EFFECT_RADIAL
#endif

// Pixel structure for batch operations
typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_pixel_t;

// ---- Basic Functions ----
void led_matrix_init(void);
void led_matrix_clear(void);
void led_matrix_update(void);
void led_matrix_set_brightness(uint8_t brightness);
void led_matrix_fill_color(uint32_t color_hex);
void led_matrix_set_energy(int channel, uint8_t energy);

// ---- Preset Quick Commands ----
void led_matrix_show_number(int number, uint32_t color_hex);
void led_matrix_show_char(char c, uint32_t color_hex);
void led_matrix_show_icon(const char *icon_name, uint32_t color_hex);
void led_matrix_show_progress(int percent, uint32_t color_hex);
void led_matrix_scroll_text(const char *text, uint32_t color_hex, int speed_ms);
void led_matrix_stop_scroll(void);

// ---- Fine-grained Pixel Control ----
void led_matrix_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void led_matrix_set_pixels(const led_pixel_t *pixels, int count);
void led_matrix_set_bitmap(const uint8_t *rgb_data);
void led_matrix_draw_line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void led_matrix_draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, bool fill);

// ---- Utility ----
bool led_matrix_is_scrolling(void);

#ifdef __cplusplus
}
#endif

#endif // LED_MATRIX_H
