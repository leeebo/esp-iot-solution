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
#include "touch_button_sensor.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "bsp/esp32_p4_function_ev_board.h"
#include "esp_codec_dev.h"
uint32_t channel_list[] = {1, 2, 3, 4};        // gpio 9, 11, 13
float channel_threshold[] = {0.006, 0.006, 0.006, 0.006}; // 1%
#define TOUCH_DEBOUNCE_TIMES 2
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
uint32_t channel_list[] = {8, 10, 12}; // gpio 8, 10, 12
float channel_threshold[] = {0.02, 0.02, 0.02}; // 2%
#define TOUCH_DEBOUNCE_TIMES 2
#elif CONFIG_IDF_TARGET_ESP32
uint32_t channel_list[] = {8, 6, 4}; // gpio 33, 14, 13
float channel_threshold[] = {0.01, 0.01, 0.01}; // 1%
#define TOUCH_DEBOUNCE_TIMES 2
#endif

static const char *TAG = "touch_button_sensor";

#if CONFIG_IDF_TARGET_ESP32P4
static esp_codec_dev_handle_t spk_codec_dev = NULL;
static QueueHandle_t audio_event_queue = NULL;

#define AUDIO_SAMPLE_RATE    16000
#define AUDIO_CHANNELS       2
#define BEEP_DURATION_MS     200
#define AUDIO_BUFFER_SIZE    (AUDIO_SAMPLE_RATE * BEEP_DURATION_MS / 1000 * AUDIO_CHANNELS)
#define NUM_BEEP_TONES       4
#define AUDIO_VOLUME         60  // Volume level (0-100)

// Different beep frequencies for different channels (higher frequencies for better clarity)
static const uint32_t beep_frequencies[] = {880, 988, 1047, 1319}; // A5, B5, C6, E6

// Pre-generated audio buffers for each frequency
static int16_t *pregenerated_beeps[NUM_BEEP_TONES] = {NULL};

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
    if (state == TOUCH_STATE_ACTIVE) {
        ESP_LOGI(TAG, "Channel %"PRIu32" is Active", channel);
#if CONFIG_IDF_TARGET_ESP32P4
        // Send channel number to audio task for playing beep
        if (audio_event_queue != NULL) {
            xQueueSend(audio_event_queue, &channel, 0);
        }
#endif
    } else if (state == TOUCH_STATE_INACTIVE) {
        ESP_LOGI(TAG, "Channel %"PRIu32" is Inactive", channel);
    }
}

/* Task function to handle touch button events */
static void touch_button_task(void *pvParameters)
{
    touch_button_handle_t handle = (touch_button_handle_t)pvParameters;
    while (1) {
        touch_button_sensor_handle_events(handle);
        vTaskDelay(pdMS_TO_TICKS(20));
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

    // Open codec once at the beginning with higher volume
    esp_codec_dev_set_out_vol(spk_codec_dev, AUDIO_VOLUME);
    if (esp_codec_dev_open(spk_codec_dev, &fs) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open codec");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Audio codec opened successfully with volume %d", AUDIO_VOLUME);

    while (1) {
        // Wait for touch event
        if (xQueueReceive(audio_event_queue, &channel, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Playing beep for channel %"PRIu32, channel);

            // Get the pre-generated buffer for this channel
            uint32_t freq_index = (channel - 1) % NUM_BEEP_TONES;

            // Play audio directly from pre-generated buffer (codec already opened)
            esp_codec_dev_write(spk_codec_dev, pregenerated_beeps[freq_index], AUDIO_BUFFER_SIZE * sizeof(int16_t));
        }
    }

    esp_codec_dev_close(spk_codec_dev);
    vTaskDelete(NULL);
}
#endif

void app_main(void)
{
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
        ESP_LOGI(TAG, "Generated beep tone %d: %"PRIu32" Hz", i, beep_frequencies[i]);
    }
    ESP_LOGI(TAG, "All audio tones pre-generated successfully");

    // Create queue for audio events
    audio_event_queue = xQueueCreate(10, sizeof(uint32_t));
    if (audio_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create audio event queue");
        return;
    }
#endif

    touch_button_config_t config = {
        .channel_num = sizeof(channel_list) / sizeof(channel_list[0]),
        .channel_list = channel_list,
        .channel_threshold = channel_threshold,
        .debounce_times = TOUCH_DEBOUNCE_TIMES,
        .skip_lowlevel_init = false
    };

    touch_button_handle_t handle;
    ESP_ERROR_CHECK(touch_button_sensor_create(&config, &handle, touch_state_callback, NULL));

    // Create a task to handle touch button events
    xTaskCreate(touch_button_task, "touch_button_sensor", 3072, handle, 2, NULL);

#if CONFIG_IDF_TARGET_ESP32P4
    // Create audio playback task
    xTaskCreate(audio_play_task, "audio_play", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Audio playback task created");
#endif

    // Main loop can do other things now
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Main task can do other work or just idle
    }
}
