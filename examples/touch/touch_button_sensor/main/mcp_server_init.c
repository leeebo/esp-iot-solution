/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
