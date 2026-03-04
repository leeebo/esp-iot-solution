/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "led_matrix.h"
#include <FastLED.h>
#include <math.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "led_matrix";

#define NUM_LEDS (LED_MATRIX_WIDTH * LED_MATRIX_HEIGHT)

// LED array
CRGB leds[NUM_LEDS];

// Scroll text state
static char s_scroll_text[64] = {0};
static uint32_t s_scroll_color = 0xFFFFFF;
static int s_scroll_speed_ms = 100;
static volatile bool s_scroll_active = false;
static TaskHandle_t s_scroll_task_handle = NULL;
static SemaphoreHandle_t s_led_mutex = NULL;

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

// 5x7 font for uppercase letters A-Z
static const uint8_t font_letters[26][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

// 8x8 icon bitmaps (each row is one byte, MSB=left)
typedef struct {
    const char *name;
    uint8_t data[8];
} icon_bitmap_t;

static const icon_bitmap_t icons[] = {
    {"check",      {0x00, 0x01, 0x02, 0x04, 0x88, 0x50, 0x20, 0x00}},
    {"cross",      {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}},
    {"heart",      {0x0C, 0x1E, 0x3E, 0x7C, 0x7C, 0x3E, 0x1E, 0x0C}},
    {"star",       {0x08, 0x08, 0x3E, 0x1C, 0x1C, 0x36, 0x22, 0x00}},
    {"smile",      {0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C}},
    {"sad",        {0x3C, 0x42, 0xA5, 0x81, 0x99, 0xA5, 0x42, 0x3C}},
    {"arrow_up",   {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x00}},
    {"arrow_down", {0x00, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18}},
    {"arrow_left", {0x10, 0x30, 0x7F, 0xFF, 0x7F, 0x30, 0x10, 0x00}},
    {"arrow_right", {0x08, 0x0C, 0xFE, 0xFF, 0xFE, 0x0C, 0x08, 0x00}},
    {"play",       {0x00, 0x7E, 0x7E, 0x3C, 0x3C, 0x18, 0x18, 0x00}},
    {"pause",      {0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00}},
    {"stop",       {0x00, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x00}},
    {"power",      {0x18, 0x18, 0x5A, 0x7E, 0x7E, 0x7E, 0x3C, 0x18}},
    {"wifi",       {0x00, 0x7E, 0x42, 0x3C, 0x24, 0x18, 0x00, 0x18}},
    {"bluetooth",  {0x08, 0x4A, 0x2C, 0x18, 0x18, 0x2C, 0x4A, 0x08}},
    {"bell",       {0x18, 0x3C, 0x3C, 0x3C, 0x7E, 0x7E, 0x00, 0x18}},
    {"sun",        {0x81, 0x42, 0x3C, 0x7E, 0x7E, 0x3C, 0x42, 0x81}},
    {"moon",       {0x1C, 0x3E, 0x7E, 0x7E, 0x7C, 0x78, 0x30, 0x00}},
    {"music",      {0x0F, 0x09, 0x09, 0x79, 0x79, 0x09, 0x0F, 0x0E}},
    {"lock",       {0x3C, 0x42, 0x42, 0xFF, 0xFF, 0xDB, 0xFF, 0xFF}},
    {"unlock",     {0x3C, 0x02, 0x02, 0xFF, 0xFF, 0xDB, 0xFF, 0xFF}},
    {"home",       {0x18, 0x3C, 0x7E, 0xFF, 0x66, 0x66, 0x7E, 0x00}},
    {"gear",       {0x18, 0x5A, 0xFF, 0x7E, 0x7E, 0xFF, 0x5A, 0x18}},
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
    if (!s_led_mutex) {
        s_led_mutex = xSemaphoreCreateMutex();
    }
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

// ---- Preset Quick Commands ----

void led_matrix_show_char(char c, uint32_t color_hex)
{
    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    FastLED.clear();
    CRGB color = color_hex;
    const uint8_t *font_data = NULL;

    if (c >= '0' && c <= '9') {
        font_data = font_numbers[c - '0'];
    } else if (c >= 'A' && c <= 'Z') {
        font_data = font_letters[c - 'A'];
    } else if (c >= 'a' && c <= 'z') {
        font_data = font_letters[c - 'a'];
    }

    if (font_data) {
        for (int x = 0; x < 5; x++) {
            uint8_t col_data = font_data[x];
            for (int y = 0; y < 7; y++) {
                if (col_data & (1 << y)) {
                    int idx = get_led_index(x + 1, y);
                    if (idx >= 0) {
                        leds[idx] = color;
                    }
                }
            }
        }
    }
    FastLED.show();

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

void led_matrix_show_icon(const char *icon_name, uint32_t color_hex)
{
    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    FastLED.clear();
    CRGB color = color_hex;

    const icon_bitmap_t *found = NULL;
    int num_icons = sizeof(icons) / sizeof(icons[0]);
    for (int i = 0; i < num_icons; i++) {
        if (strcmp(icons[i].name, icon_name) == 0) {
            found = &icons[i];
            break;
        }
    }

    if (found) {
        for (int y = 0; y < 8; y++) {
            uint8_t row_data = found->data[y];
            for (int x = 0; x < 8; x++) {
                if (row_data & (0x80 >> x)) {
                    int idx = get_led_index(x, y);
                    if (idx >= 0) {
                        leds[idx] = color;
                    }
                }
            }
        }
    }
    FastLED.show();

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

void led_matrix_show_progress(int percent, uint32_t color_hex)
{
    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    FastLED.clear();
    CRGB color = color_hex;

    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }

    int filled_pixels = (percent * 64) / 100;

    for (int i = 0; i < filled_pixels; i++) {
        int x = i % 8;
        int y = i / 8;
        int idx = get_led_index(x, y);
        if (idx >= 0) {
            leds[idx] = color;
        }
    }
    FastLED.show();

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

static void draw_char_at_offset(char c, int x_offset, uint32_t color_hex)
{
    CRGB color = color_hex;
    const uint8_t *font_data = NULL;

    if (c >= '0' && c <= '9') {
        font_data = font_numbers[c - '0'];
    } else if (c >= 'A' && c <= 'Z') {
        font_data = font_letters[c - 'A'];
    } else if (c >= 'a' && c <= 'z') {
        font_data = font_letters[c - 'a'];
    }

    if (font_data) {
        for (int cx = 0; cx < 5; cx++) {
            int x = x_offset + cx;
            if (x < 0 || x >= 8) {
                continue;
            }
            uint8_t col_data = font_data[cx];
            for (int y = 0; y < 7; y++) {
                if (col_data & (1 << y)) {
                    int idx = get_led_index(x, y);
                    if (idx >= 0) {
                        leds[idx] = color;
                    }
                }
            }
        }
    }
}

static void scroll_text_task(void *arg)
{
    while (s_scroll_active) {
        int text_len = strlen(s_scroll_text);
        int total_width = text_len * 6 + 8;

        for (int offset = 8; offset >= -total_width && s_scroll_active; offset--) {
            if (s_led_mutex) {
                xSemaphoreTake(s_led_mutex, portMAX_DELAY);
            }

            FastLED.clear();
            for (int i = 0; i < text_len; i++) {
                int char_x = offset + i * 6;
                if (char_x > -6 && char_x < 8) {
                    draw_char_at_offset(s_scroll_text[i], char_x, s_scroll_color);
                }
            }
            FastLED.show();

            if (s_led_mutex) {
                xSemaphoreGive(s_led_mutex);
            }

            vTaskDelay(pdMS_TO_TICKS(s_scroll_speed_ms));
        }
    }

    s_scroll_task_handle = NULL;
    vTaskDelete(NULL);
}

void led_matrix_scroll_text(const char *text, uint32_t color_hex, int speed_ms)
{
    led_matrix_stop_scroll();

    strncpy(s_scroll_text, text, sizeof(s_scroll_text) - 1);
    s_scroll_text[sizeof(s_scroll_text) - 1] = '\0';
    s_scroll_color = color_hex;
    s_scroll_speed_ms = speed_ms > 0 ? speed_ms : 100;
    s_scroll_active = true;

    xTaskCreate(scroll_text_task, "scroll_text", 2048, NULL, 5, &s_scroll_task_handle);
    ESP_LOGI(TAG, "Started scrolling: %s", text);
}

void led_matrix_stop_scroll(void)
{
    if (s_scroll_active) {
        s_scroll_active = false;
        vTaskDelay(pdMS_TO_TICKS(s_scroll_speed_ms + 50));
    }
}

bool led_matrix_is_scrolling(void)
{
    return s_scroll_active;
}

// ---- Fine-grained Pixel Control ----

void led_matrix_set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    int idx = get_led_index(x, y);
    if (idx >= 0) {
        leds[idx] = CRGB(r, g, b);
        FastLED.show();
    }

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

void led_matrix_set_pixels(const led_pixel_t *pixels, int count)
{
    if (!pixels || count <= 0) {
        return;
    }

    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    for (int i = 0; i < count; i++) {
        int idx = get_led_index(pixels[i].x, pixels[i].y);
        if (idx >= 0) {
            leds[idx] = CRGB(pixels[i].r, pixels[i].g, pixels[i].b);
        }
    }
    FastLED.show();

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

void led_matrix_set_bitmap(const uint8_t *rgb_data)
{
    if (!rgb_data) {
        return;
    }

    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(rgb_data[i * 3], rgb_data[i * 3 + 1], rgb_data[i * 3 + 2]);
    }
    FastLED.show();

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

void led_matrix_draw_line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    CRGB color = CRGB(r, g, b);

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        int idx = get_led_index(x0, y0);
        if (idx >= 0) {
            leds[idx] = color;
        }

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    FastLED.show();

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

void led_matrix_draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, bool fill)
{
    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }

    CRGB color = CRGB(r, g, b);

    if (fill) {
        for (int py = y; py < y + h; py++) {
            for (int px = x; px < x + w; px++) {
                int idx = get_led_index(px, py);
                if (idx >= 0) {
                    leds[idx] = color;
                }
            }
        }
    } else {
        for (int px = x; px < x + w; px++) {
            int idx1 = get_led_index(px, y);
            int idx2 = get_led_index(px, y + h - 1);
            if (idx1 >= 0) {
                leds[idx1] = color;
            }
            if (idx2 >= 0) {
                leds[idx2] = color;
            }
        }
        for (int py = y; py < y + h; py++) {
            int idx1 = get_led_index(x, py);
            int idx2 = get_led_index(x + w - 1, py);
            if (idx1 >= 0) {
                leds[idx1] = color;
            }
            if (idx2 >= 0) {
                leds[idx2] = color;
            }
        }
    }
    FastLED.show();

    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}
