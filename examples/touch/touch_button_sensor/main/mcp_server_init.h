/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MCP_SERVER_INIT_H
#define MCP_SERVER_INIT_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the MCP Server
 *
 * This function creates an MCP server with HTTP transport and registers
 * all hardware control tools (LED, audio, button).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_init(void);

/**
 * @brief Stop and deinitialize the MCP Server
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_server_deinit(void);

/**
 * @brief Update touch button state for MCP queries
 *
 * @param channel Channel number (1-4)
 * @param active true if button is active, false otherwise
 */
void mcp_server_update_button_state(int channel, bool active);

#ifdef __cplusplus
}
#endif

#endif // MCP_SERVER_INIT_H
