/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "mdns.h"

#include "esp_mcp_mgr.h"
#include "esp_mcp_engine.h"
#include "esp_mcp_tool.h"
#include "esp_mcp_property.h"
#include "esp_mcp_data.h"

#include "mcp_server_init.h"
#include "led_matrix.h"

static const char *TAG = "mcp_server";

static esp_mcp_t *s_mcp = NULL;
static esp_mcp_mgr_handle_t s_mcp_mgr_handle = 0;

static bool s_button_states[4] = {false, false, false, false};
static int s_current_volume = 80;

extern void audio_play_beep(int tone_index);
extern void audio_set_volume(int volume);
extern void audio_play_frequency(uint32_t freq, uint32_t duration_ms);
extern void audio_play_note(const char *note, uint32_t duration_ms);
extern void audio_play_sound(const char *name);
extern void audio_play_melody(const char *name);
extern void audio_stop(void);

void mcp_server_update_button_state(int channel, bool active)
{
    if (channel >= 1 && channel <= 4) {
        s_button_states[channel - 1] = active;
    }
}

static esp_mcp_value_t device_get_status_callback(const esp_mcp_property_list_t *properties)
{
    ESP_LOGI(TAG, "device.get_status called");

    cJSON *status = cJSON_CreateObject();
    if (!status) {
        return esp_mcp_value_create_string("Error: Failed to create JSON");
    }

    cJSON *buttons = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON_AddItemToArray(buttons, cJSON_CreateBool(s_button_states[i]));
    }
    cJSON_AddItemToObject(status, "buttons", buttons);
    cJSON_AddNumberToObject(status, "volume", s_current_volume);
    cJSON_AddStringToObject(status, "led_effect", "radial");

    char *json_str = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);

    if (!json_str) {
        return esp_mcp_value_create_string("Error: Failed to serialize JSON");
    }

    esp_mcp_value_t result = esp_mcp_value_create_string(json_str);
    free(json_str);
    return result;
}

static esp_mcp_value_t led_show_number_callback(const esp_mcp_property_list_t *properties)
{
    int number = esp_mcp_property_list_get_property_int(properties, "number");
    const char *color_str = esp_mcp_property_list_get_property_object(properties, "color");

    uint32_t color = 0xFF0000;
    if (color_str) {
        cJSON *color_json = cJSON_Parse(color_str);
        if (color_json) {
            int r = cJSON_GetObjectItem(color_json, "r") ? cJSON_GetObjectItem(color_json, "r")->valueint : 255;
            int g = cJSON_GetObjectItem(color_json, "g") ? cJSON_GetObjectItem(color_json, "g")->valueint : 0;
            int b = cJSON_GetObjectItem(color_json, "b") ? cJSON_GetObjectItem(color_json, "b")->valueint : 0;
            color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            cJSON_Delete(color_json);
        }
    }

    ESP_LOGI(TAG, "led.show_number: %d, color: 0x%06" PRIx32, number, color);
    led_matrix_show_number(number, color);
    led_matrix_set_energy(1, 200);

    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_set_color_callback(const esp_mcp_property_list_t *properties)
{
    int r = esp_mcp_property_list_get_property_int(properties, "r");
    int g = esp_mcp_property_list_get_property_int(properties, "g");
    int b = esp_mcp_property_list_get_property_int(properties, "b");

    ESP_LOGI(TAG, "led.set_color: r=%d, g=%d, b=%d", r, g, b);
    uint32_t color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    led_matrix_fill_color(color);

    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_clear_callback(const esp_mcp_property_list_t *properties)
{
    ESP_LOGI(TAG, "led.clear called");
    led_matrix_clear();
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_set_brightness_callback(const esp_mcp_property_list_t *properties)
{
    int brightness = esp_mcp_property_list_get_property_int(properties, "brightness");
    ESP_LOGI(TAG, "led.set_brightness: %d", brightness);
    led_matrix_set_brightness(brightness);
    return esp_mcp_value_create_bool(true);
}

#if CONFIG_IDF_TARGET_ESP32P4
static esp_mcp_value_t audio_play_tone_callback(const esp_mcp_property_list_t *properties)
{
    int tone = esp_mcp_property_list_get_property_int(properties, "tone");
    ESP_LOGI(TAG, "audio.play_tone: %d", tone);

    if (tone >= 1 && tone <= 4) {
        audio_play_beep(tone - 1);
    }

    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t audio_set_volume_callback(const esp_mcp_property_list_t *properties)
{
    int volume = esp_mcp_property_list_get_property_int(properties, "volume");
    ESP_LOGI(TAG, "audio.set_volume: %d", volume);

    if (volume >= 0 && volume <= 100) {
        s_current_volume = volume;
        audio_set_volume(volume);
    }

    return esp_mcp_value_create_bool(true);
}
#endif

static esp_mcp_value_t button_get_state_callback(const esp_mcp_property_list_t *properties)
{
    ESP_LOGI(TAG, "button.get_state called");

    cJSON *states = cJSON_CreateObject();
    if (!states) {
        return esp_mcp_value_create_string("Error: Failed to create JSON");
    }

    for (int i = 0; i < 4; i++) {
        char key[16];
        snprintf(key, sizeof(key), "button%d", i + 1);
        cJSON_AddBoolToObject(states, key, s_button_states[i]);
    }

    char *json_str = cJSON_PrintUnformatted(states);
    cJSON_Delete(states);

    if (!json_str) {
        return esp_mcp_value_create_string("Error: Failed to serialize JSON");
    }

    esp_mcp_value_t result = esp_mcp_value_create_string(json_str);
    free(json_str);
    return result;
}

// ---- NEW LED Preset Tools ----

static esp_mcp_value_t led_show_char_callback(const esp_mcp_property_list_t *properties)
{
    const char *char_str = esp_mcp_property_list_get_property_string(properties, "char");
    const char *color_str = esp_mcp_property_list_get_property_object(properties, "color");

    char c = (char_str && char_str[0]) ? char_str[0] : '0';
    uint32_t color = 0x00FF00;
    if (color_str) {
        cJSON *color_json = cJSON_Parse(color_str);
        if (color_json) {
            int r = cJSON_GetObjectItem(color_json, "r") ? cJSON_GetObjectItem(color_json, "r")->valueint : 0;
            int g = cJSON_GetObjectItem(color_json, "g") ? cJSON_GetObjectItem(color_json, "g")->valueint : 255;
            int b = cJSON_GetObjectItem(color_json, "b") ? cJSON_GetObjectItem(color_json, "b")->valueint : 0;
            color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            cJSON_Delete(color_json);
        }
    }

    ESP_LOGI(TAG, "led.show_char: '%c', color: 0x%06" PRIx32, c, color);
    led_matrix_show_char(c, color);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_show_icon_callback(const esp_mcp_property_list_t *properties)
{
    const char *icon = esp_mcp_property_list_get_property_string(properties, "icon");
    const char *color_str = esp_mcp_property_list_get_property_object(properties, "color");

    uint32_t color = 0x00FF00;
    if (color_str) {
        cJSON *color_json = cJSON_Parse(color_str);
        if (color_json) {
            int r = cJSON_GetObjectItem(color_json, "r") ? cJSON_GetObjectItem(color_json, "r")->valueint : 0;
            int g = cJSON_GetObjectItem(color_json, "g") ? cJSON_GetObjectItem(color_json, "g")->valueint : 255;
            int b = cJSON_GetObjectItem(color_json, "b") ? cJSON_GetObjectItem(color_json, "b")->valueint : 0;
            color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            cJSON_Delete(color_json);
        }
    }

    ESP_LOGI(TAG, "led.show_icon: %s, color: 0x%06" PRIx32, icon ? icon : "null", color);
    if (icon) {
        led_matrix_show_icon(icon, color);
    }
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_show_progress_callback(const esp_mcp_property_list_t *properties)
{
    int percent = esp_mcp_property_list_get_property_int(properties, "percent");
    const char *color_str = esp_mcp_property_list_get_property_object(properties, "color");

    uint32_t color = 0x00FF00;
    if (color_str) {
        cJSON *color_json = cJSON_Parse(color_str);
        if (color_json) {
            int r = cJSON_GetObjectItem(color_json, "r") ? cJSON_GetObjectItem(color_json, "r")->valueint : 0;
            int g = cJSON_GetObjectItem(color_json, "g") ? cJSON_GetObjectItem(color_json, "g")->valueint : 255;
            int b = cJSON_GetObjectItem(color_json, "b") ? cJSON_GetObjectItem(color_json, "b")->valueint : 0;
            color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            cJSON_Delete(color_json);
        }
    }

    ESP_LOGI(TAG, "led.show_progress: %d%%, color: 0x%06" PRIx32, percent, color);
    led_matrix_show_progress(percent, color);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_scroll_text_callback(const esp_mcp_property_list_t *properties)
{
    const char *text = esp_mcp_property_list_get_property_string(properties, "text");
    int speed = esp_mcp_property_list_get_property_int(properties, "speed");
    const char *color_str = esp_mcp_property_list_get_property_object(properties, "color");

    uint32_t color = 0x00FF00;
    if (color_str) {
        cJSON *color_json = cJSON_Parse(color_str);
        if (color_json) {
            int r = cJSON_GetObjectItem(color_json, "r") ? cJSON_GetObjectItem(color_json, "r")->valueint : 0;
            int g = cJSON_GetObjectItem(color_json, "g") ? cJSON_GetObjectItem(color_json, "g")->valueint : 255;
            int b = cJSON_GetObjectItem(color_json, "b") ? cJSON_GetObjectItem(color_json, "b")->valueint : 0;
            color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            cJSON_Delete(color_json);
        }
    }

    if (speed <= 0) {
        speed = 100;
    }

    ESP_LOGI(TAG, "led.scroll_text: %s, speed: %d, color: 0x%06" PRIx32, text ? text : "null", speed, color);
    if (text) {
        led_matrix_scroll_text(text, color, speed);
    }
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_stop_scroll_callback(const esp_mcp_property_list_t *properties)
{
    ESP_LOGI(TAG, "led.stop_scroll called");
    led_matrix_stop_scroll();
    return esp_mcp_value_create_bool(true);
}

// ---- NEW LED Fine-grained Tools ----

static esp_mcp_value_t led_set_pixel_callback(const esp_mcp_property_list_t *properties)
{
    int x = esp_mcp_property_list_get_property_int(properties, "x");
    int y = esp_mcp_property_list_get_property_int(properties, "y");
    int r = esp_mcp_property_list_get_property_int(properties, "r");
    int g = esp_mcp_property_list_get_property_int(properties, "g");
    int b = esp_mcp_property_list_get_property_int(properties, "b");

    ESP_LOGI(TAG, "led.set_pixel: (%d,%d) rgb(%d,%d,%d)", x, y, r, g, b);
    led_matrix_set_pixel(x, y, r, g, b);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_set_pixels_callback(const esp_mcp_property_list_t *properties)
{
    const char *pixels_str = esp_mcp_property_list_get_property_object(properties, "pixels");
    if (!pixels_str) {
        return esp_mcp_value_create_string("Error: pixels array required");
    }

    cJSON *pixels_json = cJSON_Parse(pixels_str);
    if (!pixels_json || !cJSON_IsArray(pixels_json)) {
        if (pixels_json) {
            cJSON_Delete(pixels_json);
        }
        return esp_mcp_value_create_string("Error: invalid pixels array");
    }

    int count = cJSON_GetArraySize(pixels_json);
    led_pixel_t *pixels = (led_pixel_t *)malloc(count * sizeof(led_pixel_t));
    if (!pixels) {
        cJSON_Delete(pixels_json);
        return esp_mcp_value_create_string("Error: memory allocation failed");
    }

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(pixels_json, i);
        pixels[i].x = cJSON_GetObjectItem(item, "x") ? cJSON_GetObjectItem(item, "x")->valueint : 0;
        pixels[i].y = cJSON_GetObjectItem(item, "y") ? cJSON_GetObjectItem(item, "y")->valueint : 0;
        pixels[i].r = cJSON_GetObjectItem(item, "r") ? cJSON_GetObjectItem(item, "r")->valueint : 0;
        pixels[i].g = cJSON_GetObjectItem(item, "g") ? cJSON_GetObjectItem(item, "g")->valueint : 0;
        pixels[i].b = cJSON_GetObjectItem(item, "b") ? cJSON_GetObjectItem(item, "b")->valueint : 0;
    }

    ESP_LOGI(TAG, "led.set_pixels: %d pixels", count);
    led_matrix_set_pixels(pixels, count);

    free(pixels);
    cJSON_Delete(pixels_json);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_set_bitmap_callback(const esp_mcp_property_list_t *properties)
{
    const char *data_str = esp_mcp_property_list_get_property_object(properties, "data");
    if (!data_str) {
        return esp_mcp_value_create_string("Error: data array required");
    }

    cJSON *data_json = cJSON_Parse(data_str);
    if (!data_json || !cJSON_IsArray(data_json)) {
        if (data_json) {
            cJSON_Delete(data_json);
        }
        return esp_mcp_value_create_string("Error: invalid data array");
    }

    int count = cJSON_GetArraySize(data_json);
    if (count != 64) {
        cJSON_Delete(data_json);
        return esp_mcp_value_create_string("Error: data must have 64 RGB arrays");
    }

    uint8_t rgb_data[64 * 3];
    for (int i = 0; i < 64; i++) {
        cJSON *pixel = cJSON_GetArrayItem(data_json, i);
        if (cJSON_IsArray(pixel) && cJSON_GetArraySize(pixel) >= 3) {
            rgb_data[i * 3] = cJSON_GetArrayItem(pixel, 0)->valueint;
            rgb_data[i * 3 + 1] = cJSON_GetArrayItem(pixel, 1)->valueint;
            rgb_data[i * 3 + 2] = cJSON_GetArrayItem(pixel, 2)->valueint;
        } else {
            rgb_data[i * 3] = rgb_data[i * 3 + 1] = rgb_data[i * 3 + 2] = 0;
        }
    }

    ESP_LOGI(TAG, "led.set_bitmap: 64 pixels");
    led_matrix_set_bitmap(rgb_data);

    cJSON_Delete(data_json);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_draw_line_callback(const esp_mcp_property_list_t *properties)
{
    int x0 = esp_mcp_property_list_get_property_int(properties, "x0");
    int y0 = esp_mcp_property_list_get_property_int(properties, "y0");
    int x1 = esp_mcp_property_list_get_property_int(properties, "x1");
    int y1 = esp_mcp_property_list_get_property_int(properties, "y1");
    int r = esp_mcp_property_list_get_property_int(properties, "r");
    int g = esp_mcp_property_list_get_property_int(properties, "g");
    int b = esp_mcp_property_list_get_property_int(properties, "b");

    ESP_LOGI(TAG, "led.draw_line: (%d,%d)->(%d,%d) rgb(%d,%d,%d)", x0, y0, x1, y1, r, g, b);
    led_matrix_draw_line(x0, y0, x1, y1, r, g, b);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t led_draw_rect_callback(const esp_mcp_property_list_t *properties)
{
    int x = esp_mcp_property_list_get_property_int(properties, "x");
    int y = esp_mcp_property_list_get_property_int(properties, "y");
    int w = esp_mcp_property_list_get_property_int(properties, "w");
    int h = esp_mcp_property_list_get_property_int(properties, "h");
    int r = esp_mcp_property_list_get_property_int(properties, "r");
    int g = esp_mcp_property_list_get_property_int(properties, "g");
    int b = esp_mcp_property_list_get_property_int(properties, "b");
    int fill = esp_mcp_property_list_get_property_int(properties, "fill");

    ESP_LOGI(TAG, "led.draw_rect: (%d,%d) %dx%d fill=%d rgb(%d,%d,%d)", x, y, w, h, fill, r, g, b);
    led_matrix_draw_rect(x, y, w, h, r, g, b, fill != 0);
    return esp_mcp_value_create_bool(true);
}

// ---- NEW Audio Preset Tools ----

#if CONFIG_IDF_TARGET_ESP32P4
static esp_mcp_value_t audio_play_sound_callback(const esp_mcp_property_list_t *properties)
{
    const char *name = esp_mcp_property_list_get_property_string(properties, "name");
    ESP_LOGI(TAG, "audio.play_sound: %s", name ? name : "null");
    if (name) {
        audio_play_sound(name);
    }
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t audio_play_melody_callback(const esp_mcp_property_list_t *properties)
{
    const char *name = esp_mcp_property_list_get_property_string(properties, "name");
    ESP_LOGI(TAG, "audio.play_melody: %s", name ? name : "null");
    if (name) {
        audio_play_melody(name);
    }
    return esp_mcp_value_create_bool(true);
}

// ---- NEW Audio Fine-grained Tools ----

static esp_mcp_value_t audio_play_freq_callback(const esp_mcp_property_list_t *properties)
{
    int freq = esp_mcp_property_list_get_property_int(properties, "freq");
    int duration = esp_mcp_property_list_get_property_int(properties, "duration_ms");

    ESP_LOGI(TAG, "audio.play_freq: %d Hz, %d ms", freq, duration);
    audio_play_frequency(freq, duration);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t audio_play_note_callback(const esp_mcp_property_list_t *properties)
{
    const char *note = esp_mcp_property_list_get_property_string(properties, "note");
    int duration = esp_mcp_property_list_get_property_int(properties, "duration_ms");

    ESP_LOGI(TAG, "audio.play_note: %s, %d ms", note ? note : "null", duration);
    if (note) {
        audio_play_note(note, duration);
    }
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t audio_play_sequence_callback(const esp_mcp_property_list_t *properties)
{
    const char *notes_str = esp_mcp_property_list_get_property_object(properties, "notes");
    if (!notes_str) {
        return esp_mcp_value_create_string("Error: notes array required");
    }

    cJSON *notes_json = cJSON_Parse(notes_str);
    if (!notes_json || !cJSON_IsArray(notes_json)) {
        if (notes_json) {
            cJSON_Delete(notes_json);
        }
        return esp_mcp_value_create_string("Error: invalid notes array");
    }

    int count = cJSON_GetArraySize(notes_json);
    ESP_LOGI(TAG, "audio.play_sequence: %d notes", count);

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(notes_json, i);
        const char *note = cJSON_GetObjectItem(item, "note") ? cJSON_GetObjectItem(item, "note")->valuestring : NULL;
        int duration = cJSON_GetObjectItem(item, "duration_ms") ? cJSON_GetObjectItem(item, "duration_ms")->valueint : 200;

        if (note) {
            audio_play_note(note, duration);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    cJSON_Delete(notes_json);
    return esp_mcp_value_create_bool(true);
}

static esp_mcp_value_t audio_stop_callback(const esp_mcp_property_list_t *properties)
{
    ESP_LOGI(TAG, "audio.stop called");
    audio_stop();
    return esp_mcp_value_create_bool(true);
}
#endif

static esp_err_t register_mcp_tools(void)
{
    esp_mcp_tool_t *tool = NULL;
    esp_mcp_property_t *prop = NULL;

    tool = esp_mcp_tool_create("device.get_status",
                               "Get device status including button states, volume, and LED effect",
                               device_get_status_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create device.get_status tool");
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.show_number",
                               "Display a number (0-9) on the 8x8 LED matrix with specified RGB color",
                               led_show_number_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create led.show_number tool");
    prop = esp_mcp_property_create_with_range("number", 0, 9);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_object("color", "{\"r\":255,\"g\":0,\"b\":0}");
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.set_color",
                               "Fill the entire LED matrix with a solid RGB color",
                               led_set_color_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create led.set_color tool");
    prop = esp_mcp_property_create_with_range("r", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("g", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("b", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.clear",
                               "Clear the LED matrix display",
                               led_clear_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create led.clear tool");
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.set_brightness",
                               "Set LED matrix global brightness (0-255)",
                               led_set_brightness_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create led.set_brightness tool");
    prop = esp_mcp_property_create_with_range("brightness", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

#if CONFIG_IDF_TARGET_ESP32P4
    tool = esp_mcp_tool_create("audio.play_tone",
                               "Play a beep tone (1-4 for different frequencies: 880Hz, 988Hz, 1047Hz, 1319Hz)",
                               audio_play_tone_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create audio.play_tone tool");
    prop = esp_mcp_property_create_with_range("tone", 1, 4);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("audio.set_volume",
                               "Set audio speaker volume (0-100)",
                               audio_set_volume_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create audio.set_volume tool");
    prop = esp_mcp_property_create_with_range("volume", 0, 100);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);
#endif

    tool = esp_mcp_tool_create("button.get_state",
                               "Get the current state of all 4 touch buttons",
                               button_get_state_callback);
    ESP_RETURN_ON_FALSE(tool, ESP_FAIL, TAG, "Failed to create button.get_state tool");
    esp_mcp_add_tool(s_mcp, tool);

    // LED Preset Tools
    tool = esp_mcp_tool_create("led.show_char", "Display a character (0-9, A-Z) on LED matrix", led_show_char_callback);
    prop = esp_mcp_property_create("char", ESP_MCP_PROPERTY_TYPE_STRING);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_object("color", "{\"r\":0,\"g\":255,\"b\":0}");
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.show_icon", "Display icon (check/cross/heart/star/smile/sad/arrow_up/down/left/right/play/pause/stop/power/wifi/bluetooth/bell/sun/moon/music/lock/unlock/home/gear)", led_show_icon_callback);
    prop = esp_mcp_property_create("icon", ESP_MCP_PROPERTY_TYPE_STRING);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_object("color", "{\"r\":0,\"g\":255,\"b\":0}");
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.show_progress", "Display progress bar (0-100%) on LED matrix", led_show_progress_callback);
    prop = esp_mcp_property_create_with_range("percent", 0, 100);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_object("color", "{\"r\":0,\"g\":255,\"b\":0}");
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.scroll_text", "Scroll text across LED matrix", led_scroll_text_callback);
    prop = esp_mcp_property_create("text", ESP_MCP_PROPERTY_TYPE_STRING);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("speed", 50, 500);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_object("color", "{\"r\":0,\"g\":255,\"b\":0}");
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.stop_scroll", "Stop scrolling text", led_stop_scroll_callback);
    esp_mcp_add_tool(s_mcp, tool);

    // LED Fine-grained Tools
    tool = esp_mcp_tool_create("led.set_pixel", "Set single pixel color at (x,y)", led_set_pixel_callback);
    prop = esp_mcp_property_create_with_range("x", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("y", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("r", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("g", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("b", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.set_pixels", "Set multiple pixels [{x,y,r,g,b},...]", led_set_pixels_callback);
    prop = esp_mcp_property_create("pixels", ESP_MCP_PROPERTY_TYPE_OBJECT);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.set_bitmap", "Set all 64 pixels [[r,g,b],...]", led_set_bitmap_callback);
    prop = esp_mcp_property_create("data", ESP_MCP_PROPERTY_TYPE_OBJECT);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.draw_line", "Draw line from (x0,y0) to (x1,y1)", led_draw_line_callback);
    prop = esp_mcp_property_create_with_range("x0", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("y0", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("x1", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("y1", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("r", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("g", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("b", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("led.draw_rect", "Draw rectangle at (x,y) with size w*h", led_draw_rect_callback);
    prop = esp_mcp_property_create_with_range("x", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("y", 0, 7);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("w", 1, 8);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("h", 1, 8);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("r", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("g", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("b", 0, 255);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("fill", 0, 1);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

#if CONFIG_IDF_TARGET_ESP32P4
    // Audio Preset Tools
    tool = esp_mcp_tool_create("audio.play_sound", "Play preset sound (success/error/warning/notification/click/startup/shutdown/alert/confirm/cancel)", audio_play_sound_callback);
    prop = esp_mcp_property_create("name", ESP_MCP_PROPERTY_TYPE_STRING);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    // Audio Fine-grained Tools
    tool = esp_mcp_tool_create("audio.play_freq", "Play frequency (20-20000 Hz) for duration_ms", audio_play_freq_callback);
    prop = esp_mcp_property_create_with_range("freq", 20, 20000);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("duration_ms", 50, 5000);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("audio.play_note", "Play musical note (C3-C6) for duration_ms", audio_play_note_callback);
    prop = esp_mcp_property_create("note", ESP_MCP_PROPERTY_TYPE_STRING);
    esp_mcp_tool_add_property(tool, prop);
    prop = esp_mcp_property_create_with_range("duration_ms", 50, 5000);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("audio.play_sequence", "Play sequence of notes [{note,duration_ms},...]", audio_play_sequence_callback);
    prop = esp_mcp_property_create("notes", ESP_MCP_PROPERTY_TYPE_OBJECT);
    esp_mcp_tool_add_property(tool, prop);
    esp_mcp_add_tool(s_mcp, tool);

    tool = esp_mcp_tool_create("audio.stop", "Stop audio playback", audio_stop_callback);
    esp_mcp_add_tool(s_mcp, tool);
#endif

    return ESP_OK;
}

static esp_err_t init_mdns(void)
{
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    mdns_hostname_set("esp32-mcp");
    mdns_instance_name_set("ESP32 MCP Touch Device");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS initialized: esp32-mcp.local");
    return ESP_OK;
}

esp_err_t mcp_server_init(void)
{
    esp_err_t ret;

    ret = esp_mcp_create(&s_mcp);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create MCP instance");

    ret = register_mcp_tools();
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to register MCP tools");

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_open_sockets = 7;
    http_config.lru_purge_enable = true;

    esp_mcp_mgr_config_t mcp_mgr_config = {
        .transport = esp_mcp_transport_http_server,
        .config = &http_config,
        .instance = s_mcp,
    };

    ret = esp_mcp_mgr_init(mcp_mgr_config, &s_mcp_mgr_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to init MCP manager");

    ret = esp_mcp_mgr_start(s_mcp_mgr_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to start MCP manager");

    ret = esp_mcp_mgr_register_endpoint(s_mcp_mgr_handle, "mcp", NULL);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to register MCP endpoint");

    init_mdns();

    ESP_LOGI(TAG, "MCP Server started on http://<ip>:80/mcp");
    return ESP_OK;
}

esp_err_t mcp_server_deinit(void)
{
    if (s_mcp_mgr_handle) {
        esp_mcp_mgr_stop(s_mcp_mgr_handle);
        esp_mcp_mgr_deinit(s_mcp_mgr_handle);
        s_mcp_mgr_handle = 0;
    }

    if (s_mcp) {
        esp_mcp_destroy(s_mcp);
        s_mcp = NULL;
    }

    mdns_free();
    return ESP_OK;
}
