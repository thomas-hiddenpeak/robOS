/**
 * @file console_core.h
 * @brief Console Core Component for robOS
 * 
 * This component provides a unified command-line interface for the robOS system,
 * including UART interface, command parser, help system, and command registration.
 * 
 * Features:
 * - UART-based console interface
 * - Command registration and dispatching
 * - Built-in help system
 * - Command auto-completion
 * - Command history
 * - Multi-level command support
 * - Parameter parsing and validation
 * 
 * @version 1.0.0
 * @date 2025-09-28
 */

#ifndef CONSOLE_CORE_H
#define CONSOLE_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Macros
 * ============================================================================ */

#define CONSOLE_MAX_COMMAND_LENGTH      (256)   ///< Maximum command line length
#define CONSOLE_MAX_ARGS                (16)    ///< Maximum number of arguments
#define CONSOLE_MAX_ARG_LENGTH          (64)    ///< Maximum argument length
#define CONSOLE_MAX_COMMANDS            (32)    ///< Maximum number of registered commands
#define CONSOLE_HISTORY_SIZE            (10)    ///< Command history buffer size
#define CONSOLE_PROMPT_MAX_LENGTH       (32)    ///< Maximum prompt string length

#define CONSOLE_UART_DEFAULT_PORT       UART_NUM_0    ///< Default UART port
#define CONSOLE_UART_DEFAULT_BAUDRATE   (115200)      ///< Default baud rate
#define CONSOLE_UART_BUFFER_SIZE        (1024)        ///< UART buffer size

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Console command function prototype
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
typedef esp_err_t (*console_cmd_func_t)(int argc, char **argv);

/**
 * @brief Console command structure
 */
typedef struct {
    const char *command;        ///< Command name
    const char *help;          ///< Help text for the command
    const char *hint;          ///< Command hint for auto-completion
    console_cmd_func_t func;   ///< Command function pointer
    uint8_t min_args;          ///< Minimum number of arguments
    uint8_t max_args;          ///< Maximum number of arguments (0 = unlimited)
} console_cmd_t;

/**
 * @brief Console configuration structure
 */
typedef struct {
    uart_port_t uart_port;     ///< UART port number
    int baud_rate;             ///< UART baud rate
    int tx_pin;                ///< UART TX pin
    int rx_pin;                ///< UART RX pin
    const char *prompt;        ///< Console prompt string
    bool echo_enabled;         ///< Enable character echo
    bool history_enabled;      ///< Enable command history
    bool completion_enabled;   ///< Enable command auto-completion
} console_config_t;

/**
 * @brief Console status structure
 */
typedef struct {
    bool initialized;          ///< Initialization status
    uint32_t commands_count;   ///< Number of registered commands
    uint32_t history_count;    ///< Number of commands in history
    uint32_t total_commands;   ///< Total commands executed
    uart_port_t uart_port;     ///< Current UART port
    int baud_rate;             ///< Current baud rate
} console_status_t;

/* ============================================================================
 * Public Functions
 * ============================================================================ */

/**
 * @brief Initialize the console core component
 * 
 * @param config Console configuration structure
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_core_init(const console_config_t *config);

/**
 * @brief Deinitialize the console core component
 * 
 * Cleans up all resources including UART driver, command registry,
 * and history buffer.
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_core_deinit(void);

/**
 * @brief Check if console core is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool console_core_is_initialized(void);

/**
 * @brief Get console status information
 * 
 * @param status Pointer to status structure to fill
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_core_get_status(console_status_t *status);

/**
 * @brief Start the console task
 * 
 * Starts the main console task that handles UART input/output,
 * command parsing, and execution.
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_core_start(void);

/**
 * @brief Stop the console task
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_core_stop(void);

/**
 * @brief Register a command with the console
 * 
 * @param cmd Command structure containing command information
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_register_command(const console_cmd_t *cmd);

/**
 * @brief Unregister a command from the console
 * 
 * @param command Command name to unregister
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_unregister_command(const char *command);

/**
 * @brief Print formatted text to console
 * 
 * @param format Printf-style format string
 * @param ... Variable arguments
 * @return int Number of characters printed
 */
int console_printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Print text to console
 * 
 * @param text Text string to print
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_print(const char *text);

/**
 * @brief Print text to console with newline
 * 
 * @param text Text string to print
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_println(const char *text);

/**
 * @brief Read a line from console with timeout
 * 
 * @param buffer Buffer to store the input line
 * @param buffer_size Size of the buffer
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t console_readline(char *buffer, size_t buffer_size, uint32_t timeout_ms);

/**
 * @brief Set console prompt string
 * 
 * @param prompt New prompt string
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_set_prompt(const char *prompt);

/**
 * @brief Get current console prompt string
 * 
 * @return const char* Current prompt string
 */
const char* console_get_prompt(void);

/**
 * @brief Clear the console screen
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_clear(void);

/**
 * @brief Execute a command string directly
 * 
 * Parses and executes the given command string without
 * going through the interactive console.
 * 
 * @param command_line Command line string to execute
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_execute_command(const char *command_line);

/**
 * @brief Get command history
 * 
 * @param index History index (0 = most recent)
 * @return const char* Command string or NULL if index out of range
 */
const char* console_get_history(uint32_t index);

/**
 * @brief Clear command history
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_clear_history(void);

/* ============================================================================
 * Built-in Command Functions
 * ============================================================================ */

/**
 * @brief Built-in help command
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_cmd_help(int argc, char **argv);

/**
 * @brief Built-in version command
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_cmd_version(int argc, char **argv);

/**
 * @brief Built-in clear command
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_cmd_clear(int argc, char **argv);

/**
 * @brief Built-in history command
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_cmd_history(int argc, char **argv);

/**
 * @brief Built-in system status command
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_cmd_status(int argc, char **argv);

/**
 * @brief Built-in test command for debugging
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_cmd_test(int argc, char **argv);

/**
 * @brief Set test temperature value for debugging
 * 
 * @param temperature Temperature value in Celsius
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_set_test_temperature(int temperature);

/**
 * @brief Get current test temperature value
 * 
 * @param temperature Pointer to store temperature value
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t console_get_test_temperature(int *temperature);

/* ============================================================================
 * Default Configuration
 * ============================================================================ */

/**
 * @brief Get default console configuration
 * 
 * @return console_config_t Default configuration structure
 */
console_config_t console_get_default_config(void);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_CORE_H