/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
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
#include "driver/ledc.h"
#include "driver/gpio.h"
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

// MCP notification display timeout (5 minutes)
#define MCP_NOTIFICATION_TIMEOUT_MS (5 * 60 * 1000)
// MCP notification dim delay (5 seconds after show, reduce brightness)
#define MCP_NOTIFICATION_DIM_DELAY_MS (5 * 1000)
// Brightness levels for notification
#define MCP_NOTIFICATION_FULL_BRIGHTNESS 40
#define MCP_NOTIFICATION_DIM_BRIGHTNESS 8
// Timestamp when MCP notification should auto-clear (0 = no notification)
static volatile uint32_t mcp_notification_clear_time = 0;
// Timestamp when MCP notification should dim (0 = already dimmed or no notification)
static volatile uint32_t mcp_notification_dim_time = 0;
// Flag to request immediate notification clear (set by button press)
static volatile bool mcp_notification_clear_requested = false;
// Current MCP icon state for restoration after touch events
static char mcp_current_icon[32] = {0};
static uint32_t mcp_current_icon_color = 0;
// IO0 button GPIO
#define BUTTON_IO0_GPIO 0

// Heartbeat interval: 25 minutes
#define HEARTBEAT_INTERVAL_MS (25 * 60 * 1000)

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
#define AUDIO_VOLUME 70 // Volume level (0-100)

// Different beep frequencies for different channels (higher frequencies for better clarity)
static const uint32_t beep_frequencies[] = {880, 988, 1047, 1319}; // A5, B5, C6, E6

// Pre-generated audio buffers for each frequency
static int16_t *pregenerated_beeps[NUM_BEEP_TONES] = {NULL};

// Servo configuration
#define SERVO_GPIO              21
#define SERVO_LEDC_TIMER        LEDC_TIMER_1
#define SERVO_LEDC_CHANNEL      LEDC_CHANNEL_1
#define SERVO_LEDC_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ           50
#define SERVO_RESOLUTION        LEDC_TIMER_14_BIT
#define SERVO_MIN_PULSEWIDTH_US 500   // 0.5ms for 0 degrees
#define SERVO_MAX_PULSEWIDTH_US 2500  // 2.5ms for 180 degrees
#define SERVO_MAX_DEGREE        180

static bool servo_initialized = false;
static int current_servo_angle = 90;

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

// Set MCP notification timeout (called when LED icon is shown via MCP)
void mcp_notification_set_timeout(void)
{
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    mcp_notification_dim_time = now_ms + MCP_NOTIFICATION_DIM_DELAY_MS;
    mcp_notification_clear_time = now_ms + MCP_NOTIFICATION_TIMEOUT_MS;
    led_matrix_set_brightness(MCP_NOTIFICATION_FULL_BRIGHTNESS);
    ESP_LOGI(TAG, "MCP notification: full brightness for 5s, then dim, timeout 5min");
}

// Save current MCP icon state for restoration after touch events
void mcp_notification_set_icon(const char *icon, uint32_t color)
{
    if (icon) {
        strncpy(mcp_current_icon, icon, sizeof(mcp_current_icon) - 1);
        mcp_current_icon[sizeof(mcp_current_icon) - 1] = '\0';
    } else {
        mcp_current_icon[0] = '\0';
    }
    mcp_current_icon_color = color;
}

// Clear MCP notification (can be called via MCP tool)
void mcp_notification_clear(void)
{
    if (mcp_notification_clear_time != 0) {
        mcp_notification_clear_requested = true;
        mcp_notification_dim_time = 0;
        ESP_LOGI(TAG, "MCP notification clear requested");
    }
}

// Check if MCP notification is active
bool mcp_notification_is_active(void)
{
    return mcp_notification_clear_time != 0;
}

#if CONFIG_IDF_TARGET_ESP32P4
// IO0 button interrupt handler
static void IRAM_ATTR io0_button_isr_handler(void *arg)
{
    if (mcp_notification_clear_time != 0) {
        mcp_notification_clear_requested = true;  // Signal to clear in animation task
    }
}
#endif

static void touch_state_callback(touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *user_data)
{
    static const uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00};

    if (state == TOUCH_STATE_ACTIVE) {
        active_channel = channel;
        ESP_LOGI(TAG, "Channel %" PRIu32 " is Active", channel);

        // Clear MCP notification when touch button pressed
        if (mcp_notification_clear_time != 0) {
            mcp_notification_clear_requested = true;  // Will be cleared in animation task
        }

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

// Audio event types for the queue
typedef enum {
    AUDIO_EVENT_BEEP,
    AUDIO_EVENT_FREQ,
    AUDIO_EVENT_STOP,
} audio_event_type_t;

typedef struct {
    audio_event_type_t type;
    uint32_t param1;  // tone_index for BEEP, frequency for FREQ
    uint32_t param2;  // duration_ms for FREQ
} audio_event_msg_t;

static volatile bool s_audio_stop_flag = false;

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

void audio_play_frequency(uint32_t freq, uint32_t duration_ms)
{
    if (freq < 20 || freq > 20000 || duration_ms == 0) {
        return;
    }

    size_t samples = (AUDIO_SAMPLE_RATE * duration_ms / 1000) * AUDIO_CHANNELS;
    int16_t *buffer = (int16_t *)malloc(samples * sizeof(int16_t));
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for frequency playback");
        return;
    }

    for (size_t i = 0; i < samples / AUDIO_CHANNELS; i++) {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        float envelope = 1.0f;
        size_t mono_samples = samples / AUDIO_CHANNELS;
        if (i < mono_samples / 20) {
            envelope = (float)i / (mono_samples / 20);
        } else if (i > mono_samples * 19 / 20) {
            envelope = (float)(mono_samples - i) / (mono_samples / 20);
        }
        int16_t sample_value = (int16_t)(sin(2.0f * M_PI * freq * t) * 16000.0f * envelope);
        buffer[i * 2] = sample_value;
        buffer[i * 2 + 1] = sample_value;
    }

    if (spk_codec_dev) {
        esp_codec_dev_write(spk_codec_dev, buffer, samples * sizeof(int16_t));
    }
    free(buffer);
}

// Note name to frequency conversion
static uint32_t note_to_freq(const char *note)
{
    static const struct {
        const char *name;
        uint32_t freq;
    } notes[] = {
        {"C3", 131}, {"D3", 147}, {"E3", 165}, {"F3", 175}, {"G3", 196}, {"A3", 220}, {"B3", 247},
        {"C4", 262}, {"D4", 294}, {"E4", 330}, {"F4", 349}, {"G4", 392}, {"A4", 440}, {"B4", 494},
        {"C5", 523}, {"D5", 587}, {"E5", 659}, {"F5", 698}, {"G5", 784}, {"A5", 880}, {"B5", 988},
        {"C6", 1047}, {"D6", 1175}, {"E6", 1319},
    };
    for (int i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        if (strcmp(notes[i].name, note) == 0) {
            return notes[i].freq;
        }
    }
    return 0;
}

void audio_play_note(const char *note, uint32_t duration_ms)
{
    uint32_t freq = note_to_freq(note);
    if (freq > 0) {
        audio_play_frequency(freq, duration_ms);
    }
}

void audio_stop(void)
{
    s_audio_stop_flag = true;
}

// Preset sounds
typedef struct {
    const char *name;
    const uint16_t *freqs;
    const uint16_t *durations;
    int count;
} preset_sound_t;

static const uint16_t snd_success_f[] = {523, 659, 784};
static const uint16_t snd_success_d[] = {150, 150, 300};
static const uint16_t snd_error_f[] = {784, 392};
static const uint16_t snd_error_d[] = {200, 400};
static const uint16_t snd_warning_f[] = {880, 880, 880};
static const uint16_t snd_warning_d[] = {100, 100, 100};
static const uint16_t snd_notification_f[] = {1047, 784};
static const uint16_t snd_notification_d[] = {100, 200};
static const uint16_t snd_click_f[] = {1000};
static const uint16_t snd_click_d[] = {50};
static const uint16_t snd_startup_f[] = {262, 330, 392, 523};
static const uint16_t snd_startup_d[] = {150, 150, 150, 300};
static const uint16_t snd_shutdown_f[] = {523, 392, 330, 262};
static const uint16_t snd_shutdown_d[] = {150, 150, 150, 300};
static const uint16_t snd_alert_f[] = {880, 988, 880, 988};
static const uint16_t snd_alert_d[] = {150, 150, 150, 150};
static const uint16_t snd_confirm_f[] = {784, 784, 1047};
static const uint16_t snd_confirm_d[] = {100, 100, 300};
static const uint16_t snd_cancel_f[] = {523};
static const uint16_t snd_cancel_d[] = {400};

// Minion style sound - high-pitched, fast, chattery like "Ba-ba-ba banana!"
static const uint16_t snd_minion_f[] = {1200, 1400, 1200, 1500, 1300, 1600, 1400, 1100, 1500, 1200};
static const uint16_t snd_minion_d[] = {60, 60, 60, 80, 60, 100, 60, 60, 120, 80};

// Minion laugh - "hehehe"
static const uint16_t snd_minion_laugh_f[] = {1800, 1600, 1800, 1600, 1800, 1600, 2000};
static const uint16_t snd_minion_laugh_d[] = {50, 50, 50, 50, 50, 50, 150};

// Minion "Bello!" greeting
static const uint16_t snd_minion_bello_f[] = {800, 1200, 1000, 1400};
static const uint16_t snd_minion_bello_d[] = {120, 100, 80, 200};

static const preset_sound_t preset_sounds[] = {
    {"success", snd_success_f, snd_success_d, 3},
    {"error", snd_error_f, snd_error_d, 2},
    {"warning", snd_warning_f, snd_warning_d, 3},
    {"notification", snd_notification_f, snd_notification_d, 2},
    {"click", snd_click_f, snd_click_d, 1},
    {"startup", snd_startup_f, snd_startup_d, 4},
    {"shutdown", snd_shutdown_f, snd_shutdown_d, 4},
    {"alert", snd_alert_f, snd_alert_d, 4},
    {"confirm", snd_confirm_f, snd_confirm_d, 3},
    {"cancel", snd_cancel_f, snd_cancel_d, 1},
    {"minion", snd_minion_f, snd_minion_d, 10},
    {"minion_laugh", snd_minion_laugh_f, snd_minion_laugh_d, 7},
    {"minion_bello", snd_minion_bello_f, snd_minion_bello_d, 4},
};

void audio_play_sound(const char *name)
{
    s_audio_stop_flag = false;
    for (int i = 0; i < sizeof(preset_sounds) / sizeof(preset_sounds[0]); i++) {
        if (strcmp(preset_sounds[i].name, name) == 0) {
            for (int j = 0; j < preset_sounds[i].count && !s_audio_stop_flag; j++) {
                audio_play_frequency(preset_sounds[i].freqs[j], preset_sounds[i].durations[j]);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            break;
        }
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
                // Only clear if the hold period has already expired AND no MCP notification active
                if ((int32_t)(now_ms - number_hold_until_ms) >= 0 && mcp_notification_clear_time == 0) {
                    led_matrix_clear();
                    displayed_channel = 0;
                }
                // else: silently drop — the hold-expiry below will clear later
            }
        }

        // --- 1b. Auto-clear once hold period expires, restore MCP icon if active ---
        if (number_hold_until_ms != 0 && (int32_t)(now_ms - number_hold_until_ms) >= 0) {
            if (mcp_notification_clear_time == 0) {
                led_matrix_clear();
            } else if (mcp_current_icon[0] != '\0') {
                led_matrix_show_icon(mcp_current_icon, mcp_current_icon_color);
            }
            number_hold_until_ms = 0;
            displayed_channel = 0;
        }

        // --- 1c. MCP notification: button press clear request ---
        if (mcp_notification_clear_requested) {
            led_matrix_clear();
            led_matrix_set_brightness(MCP_NOTIFICATION_FULL_BRIGHTNESS);
            mcp_notification_clear_time = 0;
            mcp_notification_dim_time = 0;
            mcp_notification_clear_requested = false;
            mcp_current_icon[0] = '\0';
            ESP_LOGI(TAG, "MCP notification cleared (button press)");
        }

        // --- 1d. MCP notification: dim after 5 seconds ---
        if (mcp_notification_dim_time != 0) {
            if ((int32_t)(now_ms - mcp_notification_dim_time) >= 0) {
                led_matrix_set_brightness(MCP_NOTIFICATION_DIM_BRIGHTNESS);
                mcp_notification_dim_time = 0;
                ESP_LOGI(TAG, "MCP notification dimmed");
            }
        }

        // --- 1e. MCP notification timeout check (5 minutes) ---
        if (mcp_notification_clear_time != 0) {
            if ((int32_t)(now_ms - mcp_notification_clear_time) >= 0) {
                led_matrix_clear();
                led_matrix_set_brightness(MCP_NOTIFICATION_FULL_BRIGHTNESS);
                mcp_notification_clear_time = 0;
                mcp_notification_dim_time = 0;
                mcp_current_icon[0] = '\0';
                ESP_LOGI(TAG, "MCP notification cleared (timeout)");
            }
        }

        // --- 2. Natural energy decay and animation (only when no MCP notification) ---
        if (mcp_notification_clear_time == 0) {
            for (int i = 0; i < 4; i++) {
                if (channel_energy[i] > 2) {
                    channel_energy[i] -= 2;
                    led_matrix_set_energy(i + 1, channel_energy[i]);
                } else if (channel_energy[i] > 0) {
                    channel_energy[i] = 0;
                    led_matrix_set_energy(i + 1, 0);
                }
            }
            // Render one animation frame (includes fadeToBlack)
            led_matrix_update();
        }
        // MCP notification is static, no animation needed

        vTaskDelay(pdMS_TO_TICKS(20)); // 50 FPS
    }
}

// Convert angle to LEDC duty cycle
static uint32_t servo_angle_to_duty(int angle)
{
    if (angle < 0) {
        angle = 0;
    }
    if (angle > SERVO_MAX_DEGREE) {
        angle = SERVO_MAX_DEGREE;
    }

    uint32_t pulse_width_us = SERVO_MIN_PULSEWIDTH_US +
                              (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) * angle / SERVO_MAX_DEGREE;

    uint32_t max_duty = (1 << SERVO_RESOLUTION) - 1;
    uint32_t duty = pulse_width_us * max_duty * SERVO_FREQ_HZ / 1000000;

    return duty;
}

// Initialize servo
static esp_err_t servo_init(void)
{
    if (servo_initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_conf = {
        .speed_mode = SERVO_LEDC_SPEED_MODE,
        .duty_resolution = SERVO_RESOLUTION,
        .timer_num = SERVO_LEDC_TIMER,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t channel_conf = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = SERVO_LEDC_SPEED_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .duty = servo_angle_to_duty(90),
        .hpoint = 0,
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    servo_initialized = true;
    current_servo_angle = 90;
    ESP_LOGI(TAG, "Servo initialized on GPIO %d", SERVO_GPIO);
    return ESP_OK;
}

// Set servo angle (0-180 degrees)
void servo_set_angle(int angle)
{
    if (!servo_initialized) {
        servo_init();
    }

    if (angle < 0) {
        angle = 0;
    }
    if (angle > SERVO_MAX_DEGREE) {
        angle = SERVO_MAX_DEGREE;
    }

    uint32_t duty = servo_angle_to_duty(angle);
    ledc_set_duty(SERVO_LEDC_SPEED_MODE, SERVO_LEDC_CHANNEL, duty);
    ledc_update_duty(SERVO_LEDC_SPEED_MODE, SERVO_LEDC_CHANNEL);
    current_servo_angle = angle;
    ESP_LOGI(TAG, "Servo set to %d degrees (duty: %lu)", angle, (unsigned long)duty);
}

// Sweep servo from current position to target angle
void servo_sweep(int from_angle, int to_angle, int step_delay_ms)
{
    if (!servo_initialized) {
        servo_init();
    }

    if (from_angle < 0) {
        from_angle = 0;
    }
    if (from_angle > SERVO_MAX_DEGREE) {
        from_angle = SERVO_MAX_DEGREE;
    }
    if (to_angle < 0) {
        to_angle = 0;
    }
    if (to_angle > SERVO_MAX_DEGREE) {
        to_angle = SERVO_MAX_DEGREE;
    }
    if (step_delay_ms < 5) {
        step_delay_ms = 5;
    }

    int step = (from_angle < to_angle) ? 1 : -1;

    for (int angle = from_angle; angle != to_angle; angle += step) {
        servo_set_angle(angle);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }
    servo_set_angle(to_angle);
}

// Get current servo angle
int servo_get_angle(void)
{
    return current_servo_angle;
}

// Minion sounds rotation
static const char *minion_sounds[] = {"minion", "minion_laugh", "minion_bello"};
static int minion_sound_index = 0;

// Heartbeat LED icons (yellow minion style)
static const char *heartbeat_icons[] = {"heart", "star", "heart"};
static const uint32_t heartbeat_colors[] = {0xFFFF00, 0xFFCC00, 0xFF8800};

// Heartbeat volume (lower than normal)
#define HEARTBEAT_VOLUME 20
#define NORMAL_VOLUME 50

// Heartbeat timer callback - minion shakes and speaks to show we're alive
static void heartbeat_timer_callback(void *arg)
{
    const char *sound = minion_sounds[minion_sound_index];
    const char *icon = heartbeat_icons[minion_sound_index];
    uint32_t color = heartbeat_colors[minion_sound_index];

    ESP_LOGI(TAG, "Heartbeat: Minion plays '%s'", sound);

    // Save previous MCP icon state
    char prev_icon[32];
    uint32_t prev_color = mcp_current_icon_color;
    strncpy(prev_icon, mcp_current_icon, sizeof(prev_icon));
    prev_icon[sizeof(prev_icon) - 1] = '\0';

    // Show heartbeat LED icon
    led_matrix_show_icon(icon, color);

    // Lower volume for heartbeat
    audio_set_volume(HEARTBEAT_VOLUME);

    // Minion wiggle dance
    int center = servo_get_angle();
    servo_sweep(center, center - 20, 8);
    servo_sweep(center - 20, center + 20, 8);
    servo_sweep(center + 20, center - 15, 8);
    servo_sweep(center - 15, center + 15, 8);
    servo_sweep(center + 15, center, 8);

    // Play current sound
    audio_play_sound(sound);

    // Restore normal volume
    audio_set_volume(NORMAL_VOLUME);

    // Brief pause to show icon
    vTaskDelay(pdMS_TO_TICKS(500));

    // Restore previous MCP icon or clear
    if (prev_icon[0] != '\0') {
        led_matrix_show_icon(prev_icon, prev_color);
        ESP_LOGI(TAG, "Heartbeat: Restored icon '%s'", prev_icon);
    } else {
        led_matrix_clear();
    }

    // Rotate to next sound
    minion_sound_index = (minion_sound_index + 1) % 3;
}

static esp_timer_handle_t heartbeat_timer = NULL;

// Start heartbeat timer
static esp_err_t heartbeat_timer_start(void)
{
    esp_timer_create_args_t timer_args = {
        .callback = heartbeat_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "heartbeat",
    };

    esp_err_t ret = esp_timer_create(&timer_args, &heartbeat_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create heartbeat timer: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_timer_start_periodic(heartbeat_timer, HEARTBEAT_INTERVAL_MS * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start heartbeat timer: %s", esp_err_to_name(ret));
        esp_timer_delete(heartbeat_timer);
        heartbeat_timer = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Heartbeat timer started: every %d minutes", HEARTBEAT_INTERVAL_MS / 60000);
    return ESP_OK;
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

    // Initialize servo motor on GPIO21
    ret = servo_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize servo: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Servo motor initialized on GPIO %d", SERVO_GPIO);
    }

    // Initialize IO0 button for clearing MCP notifications
    gpio_config_t io0_conf = {
        .pin_bit_mask = (1ULL << BUTTON_IO0_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Trigger on falling edge (button press)
    };
    gpio_config(&io0_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_IO0_GPIO, io0_button_isr_handler, NULL);
    ESP_LOGI(TAG, "IO0 button initialized for clearing MCP notifications");
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

#if CONFIG_IDF_TARGET_ESP32P4
    // === Startup animation ===
    ESP_LOGI(TAG, "Playing startup animation...");

    // 1. Servo sweep: 90 -> 0 -> 180 -> 90
    ESP_LOGI(TAG, "Servo startup sweep");
    servo_sweep(90, 0, 8);
    vTaskDelay(pdMS_TO_TICKS(100));
    servo_sweep(0, 180, 8);
    vTaskDelay(pdMS_TO_TICKS(100));
    servo_sweep(180, 90, 8);

    // 2. Play startup sound
    audio_play_sound("startup");
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. Show WiFi icon briefly
    led_matrix_show_icon("wifi", 0x00FF00);  // Green WiFi icon
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 4. Scroll WiFi SSID and IP (2 cycles only)
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char startup_msg[64];
        snprintf(startup_msg, sizeof(startup_msg), "WIFI %s IP %d.%d.%d.%d",
                 CONFIG_EXAMPLE_WIFI_SSID,
                 IP2STR(&ip_info.ip));
        led_matrix_scroll_text(startup_msg, 0x00FFFF, 70);

        // Wait for 2 scroll cycles: (text_len * 9 + 8) pixels per cycle * 70ms * 2 cycles
        int text_len = strlen(startup_msg);
        int one_cycle_ms = (text_len * 9 + 8) * 70;
        int two_cycles_ms = one_cycle_ms * 2;
        vTaskDelay(pdMS_TO_TICKS(two_cycles_ms));

        // Stop scrolling
        led_matrix_stop_scroll();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 5. Show check icon to indicate ready
    led_matrix_show_icon("check", 0x00FF00);
    audio_play_sound("success");
    vTaskDelay(pdMS_TO_TICKS(1500));
    led_matrix_clear();

    ESP_LOGI(TAG, "Startup animation complete!");

    // Start heartbeat timer - shake servo and play sound every 25 minutes
    heartbeat_timer_start();
#endif

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
