#ifndef STORAGE_CONSOLE_H
#define STORAGE_CONSOLE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register storage console commands
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t storage_manager_register_console_commands(void);

/**
 * @brief Register storage console commands (internal)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t storage_console_register_commands(void);

/**
 * @brief Unregister storage console commands (internal)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t storage_console_unregister_commands(void);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_CONSOLE_H