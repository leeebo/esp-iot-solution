/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "protocol_examples_common.h"
#include "touch_button_sensor.h"
#include "led_matrix.h"
#include "mcp_server_init.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "bsp/esp32_p4_function_ev_board.h"
#include "esp_codec_dev.h"
#endif

uint32_t channel_list[] = {1, 2, 3, 4}; // gpio 9, 11, 13
uint32_t channel_threshold[] = {120, 120, 120, 120};
#define TOUCH_DEBOUNCE_ACTIVE 1
#define TOUCH_DEBOUNCE_INACTIVE 1

static uint8_t channel_energy[4] = {0, 0, 0, 0};
static uint32_t active_channel = 0;

// Minimum display duration for the number (ms)
#define NUMBER_HOLD_MS 1200
// Timestamp (xTaskGetTickCount in ms) after which number may be cleared
static uint32_t number_hold_until_ms = 0;
// Which channel is currently being displayed (0 = none)
static uint32_t displayed_channel = 0;

// LED event queue — callback sends events, anim task processes them
typedef enum {
    LED_EVENT_ACTIVE,
    LED_EVENT_INACTIVE
} led_event_type_t;
typedef struct {
    led_event_type_t type;
    uint32_t channel; // 1-based
    uint32_t color;   // only valid for LED_EVENT_ACTIVE
} led_event_t;
static QueueHandle_t led_event_queue = NULL;

static const char *TAG = "touch_button_sensor";

#if CONFIG_IDF_TARGET_ESP32P4
static esp_codec_dev_handle_t spk_codec_dev = NULL;
static QueueHandle_t audio_event_queue = NULL;

#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_CHANNELS 2
#define BEEP_DURATION_MS 200
#define AUDIO_BUFFER_SIZE (AUDIO_SAMPLE_RATE * BEEP_DURATION_MS / 1000 * AUDIO_CHANNELS)
#define NUM_BEEP_TONES 4
#define AUDIO_VOLUME 80 // Volume level (0-100)

// Different beep frequencies for different channels (higher frequencies for better clarity)
static const uint32_t beep_frequencies[] = {880, 988, 1047, 1319}; // A5, B5, C6, E6

// Pre-generated audio buffers for each frequency
static int16_t *pregenerated_beeps[NUM_BEEP_TONES] = {NULL};

// Forward declarations for MCP server to call
void audio_play_beep(int tone_index);
void audio_set_volume(int volume);

// Generate a beep tone with the specified frequency (stereo)
static void generate_beep_tone(int16_t *buffer, size_t samples, uint32_t frequency)
{
    for (size_t i = 0; i < samples / AUDIO_CHANNELS; i++) {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        // Generate sine wave with envelope (fade in/out)
        float envelope = 1.0f;
        size_t mono_samples = samples / AUDIO_CHANNELS;
        if (i < mono_samples / 10) {
            envelope = (float)i / (mono_samples / 10);
        } else if (i > mono_samples * 9 / 10) {
            envelope = (float)(mono_samples - i) / (mono_samples / 10);
        }
        int16_t sample_value = (int16_t)(sin(2.0f * M_PI * frequency * t) * 16000.0f * envelope);
        // Write same value to both left and right channels
        buffer[i * 2] = sample_value;     // Left channel
        buffer[i * 2 + 1] = sample_value; // Right channel
    }
}
#endif

static void touch_state_callback(touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *user_data)
{
    static const uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00};

    if (state == TOUCH_STATE_ACTIVE) {
        active_channel = channel;
        ESP_LOGI(TAG, "Channel %" PRIu32 " is Active", channel);

        // Update MCP server button state
        mcp_server_update_button_state((int)channel, true);

#if CONFIG_IDF_TARGET_ESP32P4
        if (audio_event_queue != NULL) {
            xQueueSend(audio_event_queue, &channel, 0);
        }
#endif
        // Notify LED task via queue — no direct LED calls here
        if (led_event_queue != NULL) {
            led_event_t evt = {
                .type = LED_EVENT_ACTIVE,
                .channel = channel,
                .color = colors[(channel - 1) % 4],
            };
            xQueueSend(led_event_queue, &evt, 0);
        }
    } else if (state == TOUCH_STATE_INACTIVE) {
        if (active_channel == channel) {
            active_channel = 0;
        }
        ESP_LOGI(TAG, "Channel %" PRIu32 " is Inactive", channel);

        // Update MCP server button state
        mcp_server_update_button_state((int)channel, false);

        if (led_event_queue != NULL) {
            led_event_t evt = {
                .type = LED_EVENT_INACTIVE,
                .channel = channel,
                .color = 0,
            };
            xQueueSend(led_event_queue, &evt, 0);
        }
    }
}

/* Task function to handle touch button events */
static void touch_button_task(void *pvParameters)
{
    touch_button_handle_t handle = (touch_button_handle_t)pvParameters;
    while (1) {
        touch_button_sensor_handle_events(handle);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

#if CONFIG_IDF_TARGET_ESP32P4
/* Task to play audio beeps when touch button is pressed */
static void audio_play_task(void *pvParameters)
{
    uint32_t channel;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .channel = AUDIO_CHANNELS,
        .bits_per_sample = 16,
    };

    // Open codec once — do not re-open on every beep
    esp_codec_dev_set_out_vol(spk_codec_dev, AUDIO_VOLUME);
    if (esp_codec_dev_open(spk_codec_dev, &fs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open audio codec");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Audio codec opened (vol=%d)", AUDIO_VOLUME);

    while (1) {
        if (xQueueReceive(audio_event_queue, &channel, portMAX_DELAY)) {
            uint32_t freq_index = (channel - 1) % NUM_BEEP_TONES;
            esp_codec_dev_write(spk_codec_dev, pregenerated_beeps[freq_index],
                                AUDIO_BUFFER_SIZE * sizeof(int16_t));
        }
    }
}

// Functions for MCP server to call
void audio_play_beep(int tone_index)
{
    if (tone_index >= 0 && tone_index < NUM_BEEP_TONES && audio_event_queue != NULL) {
        uint32_t channel = tone_index + 1;
        xQueueSend(audio_event_queue, &channel, 0);
    }
}

void audio_set_volume(int volume)
{
    if (spk_codec_dev != NULL && volume >= 0 && volume <= 100) {
        esp_codec_dev_set_out_vol(spk_codec_dev, volume);
        ESP_LOGI(TAG, "Volume set to %d", volume);
    }
}

static void led_animation_task(void *pvParameters)
{
    while (1) {
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        // --- 1. Drain LED event queue (non-blocking) ---
        led_event_t evt;
        while (xQueueReceive(led_event_queue, &evt, 0) == pdTRUE) {
            if (evt.type == LED_EVENT_ACTIVE && evt.channel >= 1 && evt.channel <= 4) {
                if (evt.channel != displayed_channel) {
                    // Different key: clear screen and reset ALL channel energies immediately
                    led_matrix_clear();
                    for (int i = 0; i < 4; i++) {
                        channel_energy[i] = 0;
                        led_matrix_set_energy(i + 1, 0);
                    }
                    displayed_channel = evt.channel;
                    number_hold_until_ms = 0; // will be set below
                }
                // Same key (or just switched): accumulate energy and refresh display
                uint16_t e = channel_energy[evt.channel - 1] + 80;
                channel_energy[evt.channel - 1] = (e > 255) ? 255 : (uint8_t)e;
                led_matrix_set_energy(evt.channel, channel_energy[evt.channel - 1]);
                led_matrix_show_number((int)evt.channel, evt.color);
                // Extend (or set) the hold window — only same-key repeats get full effect
                number_hold_until_ms = now_ms + NUMBER_HOLD_MS;
            } else if (evt.type == LED_EVENT_INACTIVE) {
                // Only clear if the hold period has already expired
                if ((int32_t)(now_ms - number_hold_until_ms) >= 0) {
                    led_matrix_clear();
                    displayed_channel = 0;
                }
                // else: silently drop — the hold-expiry below will clear later
            }
        }

        // --- 1b. Auto-clear once hold period expires ---
        if (number_hold_until_ms != 0 && (int32_t)(now_ms - number_hold_until_ms) >= 0) {
            led_matrix_clear();
            number_hold_until_ms = 0;
            displayed_channel = 0;
        }

        // --- 2. Natural energy decay each frame ---
        for (int i = 0; i < 4; i++) {
            if (channel_energy[i] > 2) {
                channel_energy[i] -= 2;
                led_matrix_set_energy(i + 1, channel_energy[i]);
            } else if (channel_energy[i] > 0) {
                channel_energy[i] = 0;
                led_matrix_set_energy(i + 1, 0);
            }
        }

        // --- 3. Render one animation frame ---
        led_matrix_update();
        vTaskDelay(pdMS_TO_TICKS(20)); // 50 FPS
    }
}
#endif

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Connect to WiFi (uses protocol_examples_common)
    ESP_LOGI(TAG, "Connecting to WiFi...");
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "WiFi connected!");

#if CONFIG_IDF_TARGET_ESP32P4
    // Initialize ESP32-P4 EV Board BSP
    ESP_LOGI(TAG, "Initializing ESP32-P4 Function EV Board BSP");

    // Initialize audio codec
    spk_codec_dev = bsp_audio_codec_speaker_init();
    if (spk_codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to initialize audio codec");
        return;
    }
    ESP_LOGI(TAG, "Audio codec initialized successfully");

    // Pre-generate audio buffers for all frequencies
    ESP_LOGI(TAG, "Pre-generating audio beep tones...");
    for (int i = 0; i < NUM_BEEP_TONES; i++) {
        pregenerated_beeps[i] = malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
        if (pregenerated_beeps[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate audio buffer for frequency %d", i);
            return;
        }
        generate_beep_tone(pregenerated_beeps[i], AUDIO_BUFFER_SIZE, beep_frequencies[i]);
        ESP_LOGI(TAG, "Generated beep tone %d: %" PRIu32 " Hz", i, beep_frequencies[i]);
    }
    ESP_LOGI(TAG, "All audio tones pre-generated successfully");

    // Create queue for audio events
    audio_event_queue = xQueueCreate(10, sizeof(uint32_t));
    if (audio_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create audio event queue");
        return;
    }
#endif

    // Create LED event queue (touch callback -> animation task)
    led_event_queue = xQueueCreate(8, sizeof(led_event_t));
    if (led_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create LED event queue");
        return;
    }

    // Initialize LED matrix
    led_matrix_init();
    ESP_LOGI(TAG, "LED matrix initialized on GPIO %d", LED_MATRIX_GPIO);

    touch_button_config_t config = {
        .channel_num = sizeof(channel_list) / sizeof(channel_list[0]),
        .channel_list = channel_list,
        .thresholds = channel_threshold,
        .debounce_active = TOUCH_DEBOUNCE_ACTIVE,
        .debounce_inactive = TOUCH_DEBOUNCE_INACTIVE,
        .skip_lowlevel_init = false
    };

    touch_button_handle_t handle;
    ESP_ERROR_CHECK(touch_button_sensor_create(&config, &handle, touch_state_callback, NULL));

    // Create a task to handle touch button events
    xTaskCreate(touch_button_task, "touch_button_sensor", 3072, handle, 4, NULL);

#if CONFIG_IDF_TARGET_ESP32P4
    // Create audio playback task
    xTaskCreate(audio_play_task, "audio_play", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Audio playback task created");
#endif

    // Create LED animation task
    xTaskCreate(led_animation_task, "led_anim", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "LED animation task created");

    // Initialize MCP Server
    ret = mcp_server_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MCP server: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "MCP Server initialized successfully");
    }

    // Main loop can do other things now
    uint32_t start_time = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20)); // Main task can do other work or just idle
        start_time += 20;
        // print touch sensor data in one line
        for (int i = 0; i < sizeof(channel_list) / sizeof(channel_list[0]); i++) {
            uint32_t data;
            touch_button_sensor_get_data(handle, channel_list[i], &data);
            // printf("Channel %" PRIu32 " data: %" PRIu32 ", threshold: %" PRIu32 "\t", channel_list[i], data, channel_threshold[i]);
            // printf format printf("P%d,%d,%f,%f,%f")  第一个 %d 是通道，第二个是 timestamp，第三个是 data，第四个是 threshold，第五个是 energy
            // printf("P%" PRIu32 ",%" PRIu32 ",%f,%f,%f\n", channel_list[i], start_time, (float)data, (float)channel_threshold[i], (float)data - (float)channel_threshold[i]);
        }
    }
}
