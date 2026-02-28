/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdio.h>
#include "esp_err.h"
#include "touch_sensor_lowlevel.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Touch sensor state
 */
typedef enum {
    TOUCH_STATE_INACTIVE = 0,  /*!< Touch sensor is inactive */
    TOUCH_STATE_ACTIVE,        /*!< Touch sensor is active */
} touch_state_t;

typedef struct touch_button_sensor_t *touch_button_handle_t;

/**
 * @brief Touch sensor callback function type
 *
 * @param handle    Touch button sensor handle
 * @param channel   Channel number that triggered the event
 * @param state     Current state of the touch sensor
 * @param cb_arg    User data passed to the callback
 */
typedef void(*touch_cb_t)(touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *cb_arg);

/**
 * @brief Configuration structure for touch button sensor
 */
typedef struct {
    uint32_t channel_num;           /*!< Number of touch button sensor channels */
    uint32_t *channel_list;         /*!< Touch channel list */
    uint32_t *thresholds;           /*!< Thresholds for touch detection (unscaled absolute, per-channel) */
    uint32_t debounce_active;       /*!< Active debounce count (sequential triggers to active) */
    uint32_t debounce_inactive;     /*!< Inactive debounce count (sequential triggers to inactive) */
    bool skip_lowlevel_init;        /*!< Skip low level initialization when working with existing touch driver */
} touch_button_config_t;

/**
 * @brief Create a touch button sensor instance
 *
 * This function initializes the touch sensor hardware (unless skip_lowlevel_init is true),
 * sets up FSM instances for touch detection, and registers callbacks for touch events.
 *
 * @param config   Touch button sensor configuration
 * @param handle   Pointer to receive the created touch button sensor handle
 * @param cb       Callback function for touch state changes
 * @param cb_arg   User data to pass to callback function
 *
 * @return
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG if config, handle, or required config fields are NULL
 *     - ESP_ERR_NO_MEM if memory allocation fails
 *     - ESP_FAIL if touch sensor or FSM initialization fails
 */
esp_err_t touch_button_sensor_create(touch_button_config_t *config, touch_button_handle_t *handle, touch_cb_t cb, void *cb_arg);

/**
 * @brief Delete a touch button sensor instance
 *
 * Stops all FSMs, unregisters callbacks, frees resources, and optionally
 * deinitializes the touch sensor hardware.
 *
 * @param handle   Touch button sensor handle to delete
 *
 * @return
 *     - ESP_OK on success (or if handle is NULL)
 *     - ESP_ERR_INVALID_STATE if sensor state is invalid
 */
esp_err_t touch_button_sensor_delete(touch_button_handle_t handle);

/**
 * @brief Get touch sensor data for a specific channel
 *
 * Retrieves the fsm output data for a specific channel
 *
 * @param handle        Touch button sensor handle
 * @param channel       Touch channel number
 * @param data          Pointer to store the touch sensor data
 *
 * @return
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG if handle or data is NULL
 *     - ESP_ERR_INVALID_STATE if sensor not initialized
 *     - ESP_ERR_NOT_FOUND if channel not configured
 */
esp_err_t touch_button_sensor_get_data(touch_button_handle_t handle, uint32_t channel, uint32_t *data);

/**
 * @brief Get current state of a touch channel
 *
 * Returns whether the touch channel is currently considered active (touched)
 * based on the FSM state output.
 *
 * @param handle    Touch button sensor handle
 * @param channel   Touch channel number
 * @param state     Pointer to store the channel state
 *
 * @return
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG if handle or state is NULL
 *     - ESP_ERR_INVALID_STATE if sensor not initialized
 *     - ESP_ERR_NOT_FOUND if channel not configured
 */
esp_err_t touch_button_sensor_get_state(touch_button_handle_t handle, uint32_t channel, touch_state_t *state);

/**
 * @brief Handle pending touch sensor events
 *
 * Processes events from all FSM instances. This function should be called
 * periodically to update touch states and trigger callbacks.
 *
 * @param handle    Touch button sensor handle
 *
 * @return
 *     - ESP_OK on success
 *     - ESP_ERR_INVALID_ARG if handle is NULL
 *     - ESP_ERR_INVALID_STATE if sensor not initialized
 */
esp_err_t touch_button_sensor_handle_events(touch_button_handle_t handle);

#ifdef __cplusplus
}
#endif
