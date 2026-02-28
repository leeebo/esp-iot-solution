/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include "soc/soc_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "touch_sensor_lowlevel.h"
#include "touch_sensor_fsm.h"
#include "touch_button_sensor.h"

static const char *TAG = "touch_button_sensor";

#if CONFIG_TOUCH_BUTTON_SENSOR_DEBUG
static uint64_t start_time = 0;
static uint64_t get_time_in_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t current_time = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (start_time == 0) {
        start_time = current_time;
    }
    return current_time - start_time;
}
#endif

#define TOUCH_BUTTON_CHECK_GOTO(a, str, label) \
    if (!(a)) { \
        FSM_LOGE(TAG, "%s(%d): %s", __FUNCTION__, __LINE__, str); \
        goto label; \
    }

typedef struct touch_button_sensor_t {
    uint32_t channel_num;
    uint32_t *channel_list;
    fsm_handle_t fsm_handle;
    touch_cb_t state_change_cb;
    void *user_data;
    bool is_initialized;
    touch_lowlevel_handle_t *lowlevel_handles;
    bool skip_lowlevel_init;
} touch_button_sensor_t;

static void fsm_state_cb(fsm_handle_t handle, uint32_t channel, fsm_state_t state, uint32_t data, void *user_data)
{
    touch_button_sensor_t *sensor = (touch_button_sensor_t *)user_data;
    if (sensor->state_change_cb) {
        touch_state_t t_state = (state == FSM_STATE_ACTIVE) ? TOUCH_STATE_ACTIVE : TOUCH_STATE_INACTIVE;
        sensor->state_change_cb(sensor, channel, t_state, sensor->user_data);
    }
}

static void touch_sensor_callback(uint32_t channel, touch_lowlevel_state_t state, void *state_data, void *arg)
{
    touch_button_sensor_t *sensor = (touch_button_sensor_t *)arg;
    if (state == TOUCH_LOWLEVEL_STATE_NEW_DATA && sensor->fsm_handle) {
        uint32_t *raw_data = (uint32_t *)state_data;
        touch_sensor_fsm_update_data(sensor->fsm_handle, channel, raw_data, SOC_TOUCH_SAMPLE_CFG_NUM, true);
    }
}

esp_err_t touch_button_sensor_create(touch_button_config_t *config, touch_button_handle_t *handle, touch_cb_t cb, void *cb_arg)
{
    ESP_RETURN_ON_FALSE(config && handle, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    ESP_RETURN_ON_FALSE(config->channel_num > 0 && config->channel_list, ESP_ERR_INVALID_ARG, TAG, "Invalid channel config");

    touch_button_sensor_t *sensor = calloc(1, sizeof(touch_button_sensor_t));
    ESP_RETURN_ON_FALSE(sensor, ESP_ERR_NO_MEM, TAG, "Failed to allocate memory");

    sensor->channel_num = config->channel_num;
    sensor->channel_list = malloc(config->channel_num * sizeof(uint32_t));
    sensor->lowlevel_handles = calloc(config->channel_num, sizeof(touch_lowlevel_handle_t));
    if (!sensor->channel_list || !sensor->lowlevel_handles) {
        free(sensor->channel_list);
        free(sensor->lowlevel_handles);
        free(sensor);
        return ESP_ERR_NO_MEM;
    }
    memcpy(sensor->channel_list, config->channel_list, config->channel_num * sizeof(uint32_t));
    sensor->state_change_cb = cb;
    sensor->user_data = cb_arg;
    sensor->skip_lowlevel_init = config->skip_lowlevel_init;

    // Initialize touch sensor low level if needed
    if (!config->skip_lowlevel_init) {
        touch_lowlevel_config_t ll_config = {
            .channel_num = config->channel_num,
            .channel_list = config->channel_list,
            .channel_type = NULL,  // Using default channel type
            .sample_period_ms = config->channel_num,
        };
        TOUCH_BUTTON_CHECK_GOTO(
            touch_sensor_lowlevel_create(&ll_config) == ESP_OK,
            "Failed to create touch sensor low level", cleanup
        );
    }

    // Initialize FSM
    fsm_config_t fsm_cfg = DEFAULTS_TOUCH_SENSOR_FSM_CONFIG();
    fsm_cfg.channel_num = config->channel_num;
    fsm_cfg.channel_list = config->channel_list;
    fsm_cfg.freq_num = SOC_TOUCH_SAMPLE_CFG_NUM;
    fsm_cfg.threshold = config->thresholds;
    fsm_cfg.hysteresis = 10; // Default unscaled hysteresis
    fsm_cfg.debounce_active = config->debounce_active;
    fsm_cfg.debounce_inactive = config->debounce_inactive;
    fsm_cfg.state_cb = fsm_state_cb;
    fsm_cfg.user_data = sensor;
    fsm_cfg.mode = FSM_MODE_USER_PUSH;
    // for different target, using different LPF params
#if SOC_TOUCH_SAMPLE_CFG_NUM > 1
    fsm_cfg.smooth_coef = 500;
    fsm_cfg.baseline_coef = 5;
#else
    fsm_cfg.smooth_coef = 200;
    fsm_cfg.baseline_coef = 10;
#endif

    TOUCH_BUTTON_CHECK_GOTO(touch_sensor_fsm_create(&fsm_cfg, &sensor->fsm_handle) == ESP_OK, "Failed to create FSM", cleanup);
    TOUCH_BUTTON_CHECK_GOTO(touch_sensor_fsm_control(sensor->fsm_handle, FSM_CTRL_START, NULL) == ESP_OK, "Failed to start FSM", cleanup);

    for (uint32_t i = 0; i < config->channel_num; i++) {
        TOUCH_BUTTON_CHECK_GOTO(
            touch_sensor_lowlevel_register(config->channel_list[i],
                                           touch_sensor_callback,
                                           sensor,
                                           &sensor->lowlevel_handles[i]) == ESP_OK,
            "Failed to register touch sensor callback", cleanup
        );
    }

    if (!config->skip_lowlevel_init) {
        TOUCH_BUTTON_CHECK_GOTO(touch_sensor_lowlevel_start() == ESP_OK, "Failed to start touch sensor low level", cleanup);
    }

    sensor->is_initialized = true;
    *handle = sensor;
    return ESP_OK;

cleanup:
    touch_button_sensor_delete(sensor);
    return ESP_FAIL;
}

esp_err_t touch_button_sensor_delete(touch_button_handle_t handle)
{
    if (!handle) {
        return ESP_OK;
    }

    touch_button_sensor_t *sensor = (touch_button_sensor_t *)handle;
    if (!sensor->is_initialized) {
        return ESP_OK;
    }

    if (!sensor->skip_lowlevel_init) {
        touch_sensor_lowlevel_stop();
    }

    if (sensor->fsm_handle) {
        touch_sensor_fsm_control(sensor->fsm_handle, FSM_CTRL_STOP, NULL);
        touch_sensor_fsm_delete(sensor->fsm_handle);
    }

    if (sensor->lowlevel_handles) {
        for (int i = 0; i < sensor->channel_num; i++) {
            if (sensor->lowlevel_handles[i]) {
                touch_sensor_lowlevel_unregister(sensor->lowlevel_handles[i]);
            }
        }
    }

    if (!sensor->skip_lowlevel_init) {
        touch_sensor_lowlevel_delete();
    }

    free(sensor->channel_list);
    free(sensor->lowlevel_handles);
    free(sensor);
    return ESP_OK;
}

esp_err_t touch_button_sensor_get_data(touch_button_handle_t handle, uint32_t channel, uint32_t *data)
{
    ESP_RETURN_ON_FALSE(handle && data, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    touch_button_sensor_t *sensor = (touch_button_sensor_t *)handle;
    ESP_RETURN_ON_FALSE(sensor->is_initialized, ESP_ERR_INVALID_STATE, TAG, "Sensor not initialized");

    touch_fsm_info_t info;
    ESP_RETURN_ON_ERROR(touch_sensor_fsm_get_info(sensor->fsm_handle, channel, &info), TAG, "Failed to get FSM info");

    *data = info.fusion_value;
    return ESP_OK;
}

esp_err_t touch_button_sensor_get_state(touch_button_handle_t handle, uint32_t channel, touch_state_t *state)
{
    ESP_RETURN_ON_FALSE(handle && state, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    touch_button_sensor_t *sensor = (touch_button_sensor_t *)handle;
    ESP_RETURN_ON_FALSE(sensor->is_initialized, ESP_ERR_INVALID_STATE, TAG, "Sensor not initialized");

    fsm_state_t f_state;
    ESP_RETURN_ON_ERROR(touch_sensor_fsm_get_state(sensor->fsm_handle, channel, &f_state), TAG, "Failed to get FSM state");

    *state = (f_state == FSM_STATE_ACTIVE) ? TOUCH_STATE_ACTIVE : TOUCH_STATE_INACTIVE;
    return ESP_OK;
}

esp_err_t touch_button_sensor_handle_events(touch_button_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "Invalid argument");

    touch_button_sensor_t *sensor = (touch_button_sensor_t *)handle;
    ESP_RETURN_ON_FALSE(sensor->is_initialized, ESP_ERR_INVALID_STATE, TAG, "Sensor not initialized");

    if (sensor->fsm_handle) {
        ESP_RETURN_ON_ERROR(
            touch_sensor_fsm_handle_events(sensor->fsm_handle),
            TAG, "Failed to handle FSM events"
        );

#if CONFIG_TOUCH_BUTTON_SENSOR_DEBUG
        for (uint32_t i = 0; i < sensor->channel_num; i++) {
            touch_fsm_info_t info;
            if (touch_sensor_fsm_get_info(sensor->fsm_handle, sensor->channel_list[i], &info) == ESP_OK) {
                for (uint32_t f = 0; f < SOC_TOUCH_SAMPLE_CFG_NUM; f++) {
                    printf("vl,%"PRIu64"," TOUCH_FSM_INFO_PRINT_FORMAT "\n", get_time_in_ms(), TOUCH_FSM_INFO_PRINT_ARGS(sensor->channel_list[i], f, &info));
                }
            }
        }
#endif
    }
    return ESP_OK;
}
