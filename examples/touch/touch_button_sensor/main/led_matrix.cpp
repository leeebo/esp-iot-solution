/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "led_matrix.h"
#include <FastLED.h>
#include <math.h>
#include <inttypes.h>

#define NUM_LEDS (LED_MATRIX_WIDTH * LED_MATRIX_HEIGHT)

// LED array
CRGB leds[NUM_LEDS];

// 5x7 font: each entry is 5 columns of 7 bits (bit0 = top row)
static const uint8_t font_numbers[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}  // 9
};

// Progressive (row-major) layout
// Row 0: 0..7, Row 1: 8..15, etc.
static int get_led_index(int x, int y)
{
    if (x < 0 || x >= LED_MATRIX_WIDTH || y < 0 || y >= LED_MATRIX_HEIGHT) {
        return -1;
    }
    return y * LED_MATRIX_WIDTH + x;
}

// --- State ---
static uint8_t channel_energy[4] = {0};
static int current_number = -1;
static uint32_t current_color = 0;

// Center coordinates (pixel space)
static const float CX = (LED_MATRIX_WIDTH - 1) / 2.0f;  // 3.5
static const float CY = (LED_MATRIX_HEIGHT - 1) / 2.0f; // 3.5

// Returns max energy across all channels
static uint8_t get_max_energy(void)
{
    uint8_t m = 0;
    for (int i = 0; i < 4; i++)
        if (channel_energy[i] > m) {
            m = channel_energy[i];
        }
    return m;
}

// ---- Effect implementations ----

#if LED_MATRIX_EFFECT == LED_MATRIX_EFFECT_RADIAL
// Expanding concentric rings from center, speed/brightness driven by energy
static void apply_effect(uint8_t energy)
{
    static uint8_t phase = 0;
    phase += (energy >> 5) + 1; // faster with more energy

    for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
            float dx = x - CX, dy = y - CY;
            float dist = sqrtf(dx * dx + dy * dy); // 0 .. ~5
            uint8_t d = (uint8_t)(dist * 32);
            uint8_t wave = sin8(d - phase);
            uint8_t bri = scale8(wave, energy);
            if (bri > 15) {
                int idx = get_led_index(x, y);
                if (idx >= 0) {
                    leds[idx] += CHSV(phase + d, 220, bri);
                }
            }
        }
    }
}
#endif

#if LED_MATRIX_EFFECT == LED_MATRIX_EFFECT_NOISE
// Perlin noise bloom — organic, flows outward from center
static void apply_effect(uint8_t energy)
{
    static uint16_t t = 0;
    t += (energy >> 4) + 1;

    for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
            float dx = x - CX, dy = y - CY;
            float dist = sqrtf(dx * dx + dy * dy);
            // Fade by distance so inner area is brighter
            uint8_t dist_fade = (uint8_t)((1.0f - dist / 5.0f) * 255);
            uint8_t n = inoise8(x * 50, y * 50, t);
            uint8_t bri = scale8(scale8(qsub8(n, 80), dist_fade), energy);
            if (bri > 10) {
                int idx = get_led_index(x, y);
                if (idx >= 0) {
                    leds[idx] += CHSV(n + t / 4, 210, bri);
                }
            }
        }
    }
}
#endif

#if LED_MATRIX_EFFECT == LED_MATRIX_EFFECT_GLOW
// Radial glow: brightest at center, expands outward; blur spreads it
static void apply_effect(uint8_t energy)
{
    static uint8_t hue = 0;
    hue += (energy >> 6) + 1;

    for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
            float dx = x - CX, dy = y - CY;
            float dist = sqrtf(dx * dx + dy * dy);
            // Inner region gets full brightness
            uint8_t bri = scale8((uint8_t)((1.0f - dist / 5.2f) * 255), energy);
            if (bri > 10) {
                int idx = get_led_index(x, y);
                if (idx >= 0) {
                    leds[idx] += CHSV(hue, 150, bri);
                }
            }
        }
    }
    // Blur spreads glow outward; amount increases with energy
    if (energy > 40) {
        uint8_t blur_amt = scale8(energy, 120);
        blur2d(leds, LED_MATRIX_WIDTH, LED_MATRIX_HEIGHT, blur_amt);
    }
}
#endif

#if LED_MATRIX_EFFECT == LED_MATRIX_EFFECT_PALETTE
// Rainbow spiral expanding outward from center
static void apply_effect(uint8_t energy)
{
    static uint8_t hue_off = 0;
    hue_off += (energy >> 5) + 1;

    for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
            float dx = x - CX, dy = y - CY;
            float dist = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx); // -pi .. pi
            uint8_t a = (uint8_t)((angle + (float)M_PI) / (2.0f * (float)M_PI) * 255);
            uint8_t d = (uint8_t)(dist * 30);
            // Inner pixels brighter
            uint8_t bri = scale8(qsub8(200, d * 6), energy);
            if (bri > 10) {
                int idx = get_led_index(x, y);
                if (idx >= 0) {
                    leds[idx] += CHSV(hue_off + a + d, 230, bri);
                }
            }
        }
    }
}
#endif

// ---- Public API ----

void led_matrix_init(void)
{
    FastLED.addLeds<WS2812, LED_MATRIX_GPIO, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(40);
    FastLED.clear(true);
}

void led_matrix_show_number(int number, uint32_t color_hex)
{
    if (number < 0 || number > 9) {
        current_number = -1;
        return;
    }
    current_number = number;
    current_color = color_hex;
}

void led_matrix_set_energy(int channel, uint8_t energy)
{
    if (channel < 1 || channel > 4) {
        return;
    }
    channel_energy[channel - 1] = energy;
}

void led_matrix_update(void)
{
    // Natural decay (trail effect)
    fadeToBlackBy(leds, NUM_LEDS, 35);

    // Draw base number on top of faded buffer
    if (current_number >= 0) {
        CRGB base_color = current_color;
        for (int x = 0; x < 5; x++) {
            uint8_t col_data = font_numbers[current_number][x];
            for (int y = 0; y < 7; y++) {
                if (col_data & (1 << y)) {
                    int idx = get_led_index(x + 1, y);
                    if (idx >= 0) {
                        leds[idx] = base_color;    // hard set to stay bright
                    }
                }
            }
        }
    }

#if LED_MATRIX_EFFECT != LED_MATRIX_EFFECT_NONE
    // Apply overlay effect driven by max energy
    uint8_t energy = get_max_energy();
    if (energy > 10) {
        apply_effect(energy);
    }
#endif

    FastLED.show();
}

void led_matrix_clear(void)
{
    // Don't hard-clear: let the fade-to-black in led_matrix_update() handle it smoothly
    current_number = -1;
    for (int i = 0; i < 4; i++) {
        channel_energy[i] = 0;
    }
}

void led_matrix_fill_color(uint32_t color_hex)
{
    CRGB color = color_hex;
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = color;
    }
    FastLED.show();
}

void led_matrix_set_brightness(uint8_t brightness)
{
    FastLED.setBrightness(brightness);
}
