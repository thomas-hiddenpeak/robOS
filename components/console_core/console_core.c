/**
 * @file console_core.c
 * @brief Console Core Component Implementation
 * 
 * @version 1.0.0
 * @date 2025-09-28
 */

#include "console_core.h"
#include "hardware_hal.h"
#include "event_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "string.h"
#include "stdio.h"
#include "stdarg.h"
#include "ctype.h"

/* ============================================================================
 * Constants and Macros
 * ============================================================================ */

#define CONSOLE_TASK_STACK_SIZE         (4096)
#define CONSOLE_TASK_PRIORITY           (5)
#define CONSOLE_QUEUE_SIZE              (10)
#define CONSOLE_UART_TIMEOUT_MS         (100)
#define CONSOLE_COMMAND_DELIMITER       " \t\r\n"

static const char *TAG = "CONSOLE_CORE";

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

/**
 * @brief Console context structure
 */
typedef struct {
    bool initialized;                                   ///< Initialization status
    bool running;                                       ///< Task running status
    SemaphoreHandle_t mutex;                           ///< Mutex for thread safety
    TaskHandle_t task_handle;                          ///< Console task handle
    QueueHandle_t input_queue;                         ///< Input character queue
    
    console_config_t config;                           ///< Console configuration
    char prompt[CONSOLE_PROMPT_MAX_LENGTH];            ///< Current prompt string
    
    // Command registry
    console_cmd_t commands[CONSOLE_MAX_COMMANDS];      ///< Registered commands
    uint32_t command_count;                            ///< Number of registered commands
    
    // Command history
    char history[CONSOLE_HISTORY_SIZE][CONSOLE_MAX_COMMAND_LENGTH]; ///< Command history buffer
    uint32_t history_head;                             ///< History buffer head
    uint32_t history_count;                            ///< Number of commands in history
    
    // Statistics
    uint32_t total_commands;                           ///< Total commands executed
    
    // Current input line
    char input_buffer[CONSOLE_MAX_COMMAND_LENGTH];     ///< Current input buffer
    uint32_t input_pos;                                ///< Current input position
    uint32_t input_length;                             ///< Current input length
} console_context_t;

/* ============================================================================
 * Global Variables
 * ============================================================================ */

static console_context_t s_console_ctx = {0};

/* Test temperature for debugging */
static int s_test_temperature = 25;  // Default to 25°C

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void console_task(void *pvParameters);
static esp_err_t console_process_char(char ch);
static esp_err_t console_process_command(const char *command_line);
static esp_err_t console_parse_command(const char *command_line, char **argv, int *argc);
static esp_err_t console_execute_parsed_command(int argc, char **argv);
static void console_add_to_history(const char *command);
static void console_print_prompt(void);
static esp_err_t console_setup_uart(const console_config_t *config);
static esp_err_t console_register_builtin_commands(void);

/* ============================================================================
 * Public Functions
 * ============================================================================ */

esp_err_t console_core_init(const console_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_console_ctx.initialized) {
        ESP_LOGW(TAG, "Console core already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing Console Core...");
    
    // Create mutex for thread safety
    s_console_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_console_ctx.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Create input queue for character buffering
    s_console_ctx.input_queue = xQueueCreate(CONSOLE_QUEUE_SIZE, sizeof(char));
    if (!s_console_ctx.input_queue) {
        ESP_LOGE(TAG, "Failed to create input queue");
        vSemaphoreDelete(s_console_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    memcpy(&s_console_ctx.config, config, sizeof(console_config_t));
    
    // Set prompt
    if (config->prompt) {
        strncpy(s_console_ctx.prompt, config->prompt, CONSOLE_PROMPT_MAX_LENGTH - 1);
        s_console_ctx.prompt[CONSOLE_PROMPT_MAX_LENGTH - 1] = '\0';
    } else {
        strcpy(s_console_ctx.prompt, "robOS> ");
    }
    
    // Initialize context first
    s_console_ctx.initialized = true;
    s_console_ctx.running = false;
    s_console_ctx.command_count = 0;
    s_console_ctx.history_head = 0;
    s_console_ctx.history_count = 0;
    s_console_ctx.total_commands = 0;
    s_console_ctx.input_pos = 0;
    s_console_ctx.input_length = 0;
    
    // Setup UART
    esp_err_t ret = console_setup_uart(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup UART: %s", esp_err_to_name(ret));
        s_console_ctx.initialized = false;
        vQueueDelete(s_console_ctx.input_queue);
        vSemaphoreDelete(s_console_ctx.mutex);
        return ret;
    }
    
    // Register built-in commands (after initialization is marked complete)
    ret = console_register_builtin_commands();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register built-in commands: %s", esp_err_to_name(ret));
        s_console_ctx.initialized = false;
        uart_driver_delete(s_console_ctx.config.uart_port);
        vQueueDelete(s_console_ctx.input_queue);
        vSemaphoreDelete(s_console_ctx.mutex);
        return ret;
    }
    
    ESP_LOGI(TAG, "Console Core initialized successfully");
    return ESP_OK;
}

esp_err_t console_core_deinit(void)
{
    if (!s_console_ctx.initialized) {
        ESP_LOGW(TAG, "Console core not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing Console Core...");
    
    // Stop console task if running
    if (s_console_ctx.running) {
        console_core_stop();
    }
    
    // Delete UART driver
    uart_driver_delete(s_console_ctx.config.uart_port);
    
    // Cleanup resources
    if (s_console_ctx.input_queue) {
        vQueueDelete(s_console_ctx.input_queue);
        s_console_ctx.input_queue = NULL;
    }
    
    if (s_console_ctx.mutex) {
        vSemaphoreDelete(s_console_ctx.mutex);
        s_console_ctx.mutex = NULL;
    }
    
    // Reset context
    memset(&s_console_ctx, 0, sizeof(console_context_t));
    
    ESP_LOGI(TAG, "Console Core deinitialized");
    return ESP_OK;
}

bool console_core_is_initialized(void)
{
    return s_console_ctx.initialized;
}

esp_err_t console_core_get_status(console_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        status->initialized = s_console_ctx.initialized;
        status->commands_count = s_console_ctx.command_count;
        status->history_count = s_console_ctx.history_count;
        status->total_commands = s_console_ctx.total_commands;
        status->uart_port = s_console_ctx.config.uart_port;
        status->baud_rate = s_console_ctx.config.baud_rate;
        xSemaphoreGive(s_console_ctx.mutex);
    }
    
    return ESP_OK;
}

esp_err_t console_core_start(void)
{
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_console_ctx.running) {
        ESP_LOGW(TAG, "Console task already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting console task...");
    
    BaseType_t result = xTaskCreate(
        console_task,
        "console_task",
        CONSOLE_TASK_STACK_SIZE,
        NULL,
        CONSOLE_TASK_PRIORITY,
        &s_console_ctx.task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create console task");
        return ESP_ERR_NO_MEM;
    }
    
    s_console_ctx.running = true;
    ESP_LOGI(TAG, "Console task started successfully");
    
    return ESP_OK;
}

esp_err_t console_core_stop(void)
{
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_console_ctx.running) {
        ESP_LOGW(TAG, "Console task not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping console task...");
    
    s_console_ctx.running = false;
    
    if (s_console_ctx.task_handle) {
        vTaskDelete(s_console_ctx.task_handle);
        s_console_ctx.task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Console task stopped");
    return ESP_OK;
}

esp_err_t console_register_command(const console_cmd_t *cmd)
{
    if (!cmd || !cmd->command || !cmd->func) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_console_ctx.command_count >= CONSOLE_MAX_COMMANDS) {
            xSemaphoreGive(s_console_ctx.mutex);
            ESP_LOGE(TAG, "Maximum number of commands reached");
            return ESP_ERR_NO_MEM;
        }
        
        // Check for duplicate command names
        for (uint32_t i = 0; i < s_console_ctx.command_count; i++) {
            if (strcmp(s_console_ctx.commands[i].command, cmd->command) == 0) {
                xSemaphoreGive(s_console_ctx.mutex);
                ESP_LOGE(TAG, "Command '%s' already registered", cmd->command);
                return ESP_ERR_INVALID_ARG;
            }
        }
        
        // Add command to registry
        memcpy(&s_console_ctx.commands[s_console_ctx.command_count], cmd, sizeof(console_cmd_t));
        s_console_ctx.command_count++;
        
        xSemaphoreGive(s_console_ctx.mutex);
        
        ESP_LOGD(TAG, "Command '%s' registered successfully", cmd->command);
    }
    
    return ESP_OK;
}

esp_err_t console_unregister_command(const char *command)
{
    if (!command) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (uint32_t i = 0; i < s_console_ctx.command_count; i++) {
            if (strcmp(s_console_ctx.commands[i].command, command) == 0) {
                // Shift remaining commands down
                for (uint32_t j = i; j < s_console_ctx.command_count - 1; j++) {
                    memcpy(&s_console_ctx.commands[j], &s_console_ctx.commands[j + 1], sizeof(console_cmd_t));
                }
                s_console_ctx.command_count--;
                xSemaphoreGive(s_console_ctx.mutex);
                
                ESP_LOGD(TAG, "Command '%s' unregistered successfully", command);
                return ESP_OK;
            }
        }
        xSemaphoreGive(s_console_ctx.mutex);
        
        ESP_LOGW(TAG, "Command '%s' not found", command);
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_ERR_TIMEOUT;
}

int console_printf(const char *format, ...)
{
    if (!s_console_ctx.initialized) {
        return 0;
    }
    
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0) {
        uart_write_bytes(s_console_ctx.config.uart_port, buffer, len);
    }
    
    return len;
}

esp_err_t console_print(const char *text)
{
    if (!text || !s_console_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int len = strlen(text);
    if (len > 0) {
        uart_write_bytes(s_console_ctx.config.uart_port, text, len);
    }
    
    return ESP_OK;
}

esp_err_t console_println(const char *text)
{
    esp_err_t ret = console_print(text);
    if (ret == ESP_OK) {
        ret = console_print("\r\n");
    }
    return ret;
}

esp_err_t console_execute_command(const char *command_line)
{
    if (!command_line) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return console_process_command(command_line);
}

esp_err_t console_set_prompt(const char *prompt)
{
    if (!prompt) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        strncpy(s_console_ctx.prompt, prompt, CONSOLE_PROMPT_MAX_LENGTH - 1);
        s_console_ctx.prompt[CONSOLE_PROMPT_MAX_LENGTH - 1] = '\0';
        xSemaphoreGive(s_console_ctx.mutex);
    }
    
    return ESP_OK;
}

const char* console_get_prompt(void)
{
    if (!s_console_ctx.initialized) {
        return NULL;
    }
    
    return s_console_ctx.prompt;
}

esp_err_t console_clear(void)
{
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Send ANSI escape sequence to clear screen and move cursor to top
    return console_print("\033[2J\033[H");
}

console_config_t console_get_default_config(void)
{
    console_config_t config = {
        .uart_port = CONSOLE_UART_DEFAULT_PORT,
        .baud_rate = CONSOLE_UART_DEFAULT_BAUDRATE,
        .tx_pin = UART_PIN_NO_CHANGE,
        .rx_pin = UART_PIN_NO_CHANGE,
        .prompt = "robOS> ",
        .echo_enabled = true,
        .history_enabled = true,
        .completion_enabled = true
    };
    
    return config;
}

/* ============================================================================
 * Private Functions
 * ============================================================================ */

static void console_task(void *pvParameters)
{
    char ch;
    uint8_t data[1];
    int len;
    
    ESP_LOGI(TAG, "Console task started");
    
    // Print initial prompt
    console_print_prompt();
    
    while (s_console_ctx.running) {
        // Read characters from UART with timeout
        len = uart_read_bytes(s_console_ctx.config.uart_port, data, 1, pdMS_TO_TICKS(CONSOLE_UART_TIMEOUT_MS));
        
        if (len > 0) {
            ch = data[0];
            console_process_char(ch);
        }
        
        // Allow other tasks to run
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGI(TAG, "Console task ended");
    vTaskDelete(NULL);
}

static esp_err_t console_process_char(char ch)
{
    // Handle special characters
    switch (ch) {
        case '\r':  // Carriage return
        case '\n':  // Line feed
            if (s_console_ctx.config.echo_enabled) {
                console_print("\r\n");
            }
            
            // Null terminate the input
            s_console_ctx.input_buffer[s_console_ctx.input_length] = '\0';
            
            // Process the command if not empty
            if (s_console_ctx.input_length > 0) {
                console_process_command(s_console_ctx.input_buffer);
                
                // Add to history
                if (s_console_ctx.config.history_enabled) {
                    console_add_to_history(s_console_ctx.input_buffer);
                }
            }
            
            // Reset input buffer
            s_console_ctx.input_pos = 0;
            s_console_ctx.input_length = 0;
            
            // Print new prompt
            console_print_prompt();
            break;
            
        case '\b':  // Backspace
        case 0x7F:  // DEL
            if (s_console_ctx.input_length > 0) {
                s_console_ctx.input_length--;
                s_console_ctx.input_pos = s_console_ctx.input_length;
                
                if (s_console_ctx.config.echo_enabled) {
                    console_print("\b \b");  // Backspace, space, backspace
                }
            }
            break;
            
        default:
            // Regular character
            if (isprint(ch) && s_console_ctx.input_length < CONSOLE_MAX_COMMAND_LENGTH - 1) {
                s_console_ctx.input_buffer[s_console_ctx.input_length] = ch;
                s_console_ctx.input_length++;
                s_console_ctx.input_pos = s_console_ctx.input_length;
                
                if (s_console_ctx.config.echo_enabled) {
                    uart_write_bytes(s_console_ctx.config.uart_port, &ch, 1);
                }
            }
            break;
    }
    
    return ESP_OK;
}

static esp_err_t console_process_command(const char *command_line)
{
    char *argv[CONSOLE_MAX_ARGS];
    int argc;
    
    // Parse command line
    esp_err_t ret = console_parse_command(command_line, argv, &argc);
    if (ret != ESP_OK) {
        console_println("Error: Failed to parse command");
        return ret;
    }
    
    if (argc == 0) {
        return ESP_OK;  // Empty command
    }
    
    // Execute parsed command
    ret = console_execute_parsed_command(argc, argv);
    
    // Update statistics
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_console_ctx.total_commands++;
        xSemaphoreGive(s_console_ctx.mutex);
    }
    
    return ret;
}

static esp_err_t console_parse_command(const char *command_line, char **argv, int *argc)
{
    static char parse_buffer[CONSOLE_MAX_COMMAND_LENGTH];
    char *token;
    
    *argc = 0;
    
    // Copy command line to parse buffer
    strncpy(parse_buffer, command_line, CONSOLE_MAX_COMMAND_LENGTH - 1);
    parse_buffer[CONSOLE_MAX_COMMAND_LENGTH - 1] = '\0';
    
    // Tokenize the command line
    token = strtok(parse_buffer, CONSOLE_COMMAND_DELIMITER);
    while (token != NULL && *argc < CONSOLE_MAX_ARGS) {
        argv[*argc] = token;
        (*argc)++;
        token = strtok(NULL, CONSOLE_COMMAND_DELIMITER);
    }
    
    return ESP_OK;
}

static esp_err_t console_execute_parsed_command(int argc, char **argv)
{
    if (argc == 0) {
        return ESP_OK;
    }
    
    const char *command = argv[0];
    
    // Find and execute command
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (uint32_t i = 0; i < s_console_ctx.command_count; i++) {
            if (strcmp(s_console_ctx.commands[i].command, command) == 0) {
                // Check argument count
                int arg_count = argc - 1;  // Exclude command name
                if (arg_count < s_console_ctx.commands[i].min_args ||
                    (s_console_ctx.commands[i].max_args > 0 && arg_count > s_console_ctx.commands[i].max_args)) {
                    xSemaphoreGive(s_console_ctx.mutex);
                    console_printf("Error: Invalid number of arguments for '%s'\r\n", command);
                    if (s_console_ctx.commands[i].help) {
                        console_printf("Usage: %s\r\n", s_console_ctx.commands[i].help);
                    }
                    return ESP_ERR_INVALID_ARG;
                }
                
                // Execute command
                console_cmd_func_t cmd_func = s_console_ctx.commands[i].func;
                xSemaphoreGive(s_console_ctx.mutex);
                
                esp_err_t ret = cmd_func(argc, argv);
                if (ret != ESP_OK) {
                    console_printf("Command '%s' failed: %s\r\n", command, esp_err_to_name(ret));
                }
                return ret;
            }
        }
        xSemaphoreGive(s_console_ctx.mutex);
    }
    
    console_printf("Unknown command: '%s'. Type 'help' for available commands.\r\n", command);
    return ESP_ERR_NOT_FOUND;
}

static void console_add_to_history(const char *command)
{
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Copy command to history buffer
        strncpy(s_console_ctx.history[s_console_ctx.history_head], command, CONSOLE_MAX_COMMAND_LENGTH - 1);
        s_console_ctx.history[s_console_ctx.history_head][CONSOLE_MAX_COMMAND_LENGTH - 1] = '\0';
        
        // Update head and count
        s_console_ctx.history_head = (s_console_ctx.history_head + 1) % CONSOLE_HISTORY_SIZE;
        if (s_console_ctx.history_count < CONSOLE_HISTORY_SIZE) {
            s_console_ctx.history_count++;
        }
        
        xSemaphoreGive(s_console_ctx.mutex);
    }
}

static void console_print_prompt(void)
{
    console_print(s_console_ctx.prompt);
}

static esp_err_t console_setup_uart(const console_config_t *config)
{
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // Check if UART driver is already installed
    esp_err_t ret = uart_driver_install(config->uart_port, CONSOLE_UART_BUFFER_SIZE, 0, 0, NULL, 0);
    if (ret == ESP_FAIL) {
        // Driver already installed, delete it first
        ESP_LOGW(TAG, "UART driver already installed, deleting and reinstalling");
        uart_driver_delete(config->uart_port);
        ret = uart_driver_install(config->uart_port, CONSOLE_UART_BUFFER_SIZE, 0, 0, NULL, 0);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_param_config(config->uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(config->uart_port);
        return ret;
    }
    
    if (config->tx_pin != UART_PIN_NO_CHANGE || config->rx_pin != UART_PIN_NO_CHANGE) {
        ret = uart_set_pin(config->uart_port, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
            uart_driver_delete(config->uart_port);
            return ret;
        }
    }
    
    ESP_LOGD(TAG, "UART%d configured: baud=%d, tx=%d, rx=%d", 
             config->uart_port, config->baud_rate, config->tx_pin, config->rx_pin);
    
    return ESP_OK;
}

static esp_err_t console_register_builtin_commands(void)
{
    // Register built-in commands
    console_cmd_t commands[] = {
        {
            .command = "help",
            .help = "help [command] - Show available commands or help for specific command",
            .hint = NULL,
            .func = console_cmd_help,
            .min_args = 0,
            .max_args = 1
        },
        {
            .command = "version",
            .help = "version - Show system version information",
            .hint = NULL,
            .func = console_cmd_version,
            .min_args = 0,
            .max_args = 0
        },
        {
            .command = "clear",
            .help = "clear - Clear the console screen",
            .hint = NULL,
            .func = console_cmd_clear,
            .min_args = 0,
            .max_args = 0
        },
        {
            .command = "history",
            .help = "history - Show command history",
            .hint = NULL,
            .func = console_cmd_history,
            .min_args = 0,
            .max_args = 0
        },
        {
            .command = "status",
            .help = "status - Show system status information",
            .hint = NULL,
            .func = console_cmd_status,
            .min_args = 0,
            .max_args = 0
        },
        {
            .command = "test",
            .help = "test <subcommand> [args...] - Test commands for debugging",
            .hint = "<temp|...> [args...]",
            .func = console_cmd_test,
            .min_args = 1,
            .max_args = 10
        }
    };
    
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        esp_err_t ret = console_register_command(&commands[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register built-in command '%s': %s", 
                     commands[i].command, esp_err_to_name(ret));
            return ret;
        }
    }
    
    ESP_LOGD(TAG, "Built-in commands registered successfully");
    return ESP_OK;
}

/* ============================================================================
 * Built-in Command Implementations
 * ============================================================================ */

esp_err_t console_cmd_help(int argc, char **argv)
{
    if (argc == 1) {
        // Show all commands
        console_println("Available commands:");
        
        if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (uint32_t i = 0; i < s_console_ctx.command_count; i++) {
                if (s_console_ctx.commands[i].help) {
                    console_printf("  %s\r\n", s_console_ctx.commands[i].help);
                } else {
                    console_printf("  %s\r\n", s_console_ctx.commands[i].command);
                }
            }
            xSemaphoreGive(s_console_ctx.mutex);
        }
    } else {
        // Show help for specific command
        const char *command = argv[1];
        bool found = false;
        
        if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (uint32_t i = 0; i < s_console_ctx.command_count; i++) {
                if (strcmp(s_console_ctx.commands[i].command, command) == 0) {
                    if (s_console_ctx.commands[i].help) {
                        console_printf("%s\r\n", s_console_ctx.commands[i].help);
                    } else {
                        console_printf("No help available for '%s'\r\n", command);
                    }
                    found = true;
                    break;
                }
            }
            xSemaphoreGive(s_console_ctx.mutex);
        }
        
        if (!found) {
            console_printf("Unknown command: '%s'\r\n", command);
            return ESP_ERR_NOT_FOUND;
        }
    }
    
    return ESP_OK;
}

esp_err_t console_cmd_version(int argc, char **argv)
{
    console_println("robOS Console Core v1.0.0");
    console_printf("ESP-IDF Version: %s\r\n", esp_get_idf_version());
    console_printf("Compile Time: %s %s\r\n", __DATE__, __TIME__);
    
    return ESP_OK;
}

esp_err_t console_cmd_clear(int argc, char **argv)
{
    return console_clear();
}

esp_err_t console_cmd_history(int argc, char **argv)
{
    console_println("Command history:");
    
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_console_ctx.history_count == 0) {
            console_println("  (empty)");
        } else {
            // Print history in reverse order (most recent first)
            for (int i = s_console_ctx.history_count - 1; i >= 0; i--) {
                int index = (s_console_ctx.history_head - 1 - i + CONSOLE_HISTORY_SIZE) % CONSOLE_HISTORY_SIZE;
                console_printf("  %lu: %s\r\n", (unsigned long)(s_console_ctx.history_count - i), s_console_ctx.history[index]);
            }
        }
        xSemaphoreGive(s_console_ctx.mutex);
    }
    
    return ESP_OK;
}

esp_err_t console_cmd_test(int argc, char **argv)
{
    if (argc < 2) {
        console_println("Usage: test <subcommand> [args...]");
        console_println("Available subcommands:");
        console_println("  temp <value> - Set test temperature value for debugging");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *subcommand = argv[1];
    
    if (strcmp(subcommand, "temp") == 0) {
        if (argc < 3) {
            console_println("Usage: test temp <value>");
            return ESP_ERR_INVALID_ARG;
        }
        
        int temp_value = atoi(argv[2]);
        console_printf("Setting test temperature to: %d°C\r\n", temp_value);
        
        esp_err_t ret = console_set_test_temperature(temp_value);
        if (ret != ESP_OK) {
            console_println("Invalid temperature value (range: -50°C to 150°C)");
            return ret;
        }
        
        return ESP_OK;
    } else {
        console_printf("Unknown test subcommand: '%s'\r\n", subcommand);
        console_println("Available subcommands:");
        console_println("  temp <value> - Set test temperature value for debugging");
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t console_cmd_status(int argc, char **argv)
{
    console_status_t status;
    esp_err_t ret = console_core_get_status(&status);
    if (ret != ESP_OK) {
        console_println("Failed to get console status");
        return ret;
    }
    
    console_println("Console Status:");
    console_printf("  Initialized: %s\r\n", status.initialized ? "Yes" : "No");
    console_printf("  UART Port: %d\r\n", status.uart_port);
    console_printf("  Baud Rate: %d\r\n", status.baud_rate);
    console_printf("  Registered Commands: %lu\r\n", status.commands_count);
    console_printf("  History Entries: %lu\r\n", status.history_count);
    console_printf("  Total Commands Executed: %lu\r\n", status.total_commands);
    
    return ESP_OK;
}

const char* console_get_history(uint32_t index)
{
    if (!s_console_ctx.initialized || index >= s_console_ctx.history_count) {
        return NULL;
    }
    
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int history_index = (s_console_ctx.history_head - 1 - index + CONSOLE_HISTORY_SIZE) % CONSOLE_HISTORY_SIZE;
        const char* command = s_console_ctx.history[history_index];
        xSemaphoreGive(s_console_ctx.mutex);
        return command;
    }
    
    return NULL;
}

esp_err_t console_clear_history(void)
{
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_console_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_console_ctx.history_head = 0;
        s_console_ctx.history_count = 0;
        memset(s_console_ctx.history, 0, sizeof(s_console_ctx.history));
        xSemaphoreGive(s_console_ctx.mutex);
    }
    
    return ESP_OK;
}

esp_err_t console_readline(char *buffer, size_t buffer_size, uint32_t timeout_ms)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_console_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t pos = 0;
    uint8_t data[1];
    int len;
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY;
    
    while (pos < buffer_size - 1) {
        // Check timeout
        if (timeout_ms > 0 && (xTaskGetTickCount() - start_time) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        
        len = uart_read_bytes(s_console_ctx.config.uart_port, data, 1, pdMS_TO_TICKS(100));
        
        if (len > 0) {
            char ch = data[0];
            
            if (ch == '\r' || ch == '\n') {
                buffer[pos] = '\0';
                return ESP_OK;
            } else if (ch == '\b' || ch == 0x7F) {  // Backspace
                if (pos > 0) {
                    pos--;
                    if (s_console_ctx.config.echo_enabled) {
                        console_print("\b \b");
                    }
                }
            } else if (isprint(ch)) {
                buffer[pos] = ch;
                pos++;
                if (s_console_ctx.config.echo_enabled) {
                    uart_write_bytes(s_console_ctx.config.uart_port, &ch, 1);
                }
            }
        }
    }
    
    buffer[pos] = '\0';
    return ESP_OK;
}

esp_err_t console_set_test_temperature(int temperature)
{
    if (temperature < -50 || temperature > 150) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_test_temperature = temperature;
    return ESP_OK;
}

esp_err_t console_get_test_temperature(int *temperature)
{
    if (!temperature) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *temperature = s_test_temperature;
    return ESP_OK;
}