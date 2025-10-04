/**
 * @file web_server.h
 * @brief Simple Web Server for robOS
 *
 * Based on successful implementation from rm01-esp32s3-bsp project.
 * Serves static files from /sdcard/web and provides basic API endpoints.
 *
 * @author robOS Team
 * @date 2025
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the web server
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the web server
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H