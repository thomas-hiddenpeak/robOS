/**
 * @file power_monitor.h
 * @brief Power Monitor Component API
 * 
 * This component provides comprehensive power monitoring capabilities for robOS,
 * including voltage monitoring through ADC and power chip communication via UART.
 * 
 * Features:
 * - Supply voltage monitoring via GPIO 18 (ADC2_CHANNEL_7) with 11.4:1 divider
 * - Real-time voltage monitoring with threshold alarms
 * - Background task for continuous monitoring
 * - Power chip data reception via GPIO 47 (UART1_RX)
 * - 9600 baud rate, 8N1 configuration
 * - Data format: [0xFF header][voltage][current][CRC] (4 bytes)
 * - Real-time voltage, current, power data reading
 * - Protocol analysis and debugging support
 * - 10 dedicated console commands
 * - NVS configuration persistence
 * 
 * @author robOS Team
 * @date 2025-10-02
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power Monitor version
 */
#define POWER_MONITOR_VERSION "1.0.0"

/**
 * @brief Maximum number of voltage thresholds
 */
#define POWER_MONITOR_MAX_THRESHOLDS 8

/**
 * @brief Power chip data packet size
 */
#define POWER_CHIP_PACKET_SIZE 8

/**
 * @brief Power chip header byte
 */
#define POWER_CHIP_HEADER 0xFF

/**
 * @brief Voltage divider ratio (11.4:1)
 */
#define VOLTAGE_DIVIDER_RATIO 11.4f

/**
 * @brief ADC reference voltage (mV)
 */
#define ADC_REF_VOLTAGE_MV 3300

/**
 * @brief ADC resolution bits
 */
#define ADC_RESOLUTION_BITS 12

/**
 * @brief Maximum ADC reading value
 */
#define ADC_MAX_VALUE ((1 << ADC_RESOLUTION_BITS) - 1)

/**
 * @brief Power monitoring error codes
 */
typedef enum {
    POWER_MONITOR_OK = 0,                    /**< No error */
    POWER_MONITOR_ERR_INVALID_ARG = -1,      /**< Invalid argument */
    POWER_MONITOR_ERR_NOT_INIT = -2,         /**< Not initialized */
    POWER_MONITOR_ERR_ALREADY_INIT = -3,     /**< Already initialized */
    POWER_MONITOR_ERR_HW_FAIL = -4,          /**< Hardware failure */
    POWER_MONITOR_ERR_TIMEOUT = -5,          /**< Timeout error */
    POWER_MONITOR_ERR_CRC_FAIL = -6,         /**< CRC check failed */
    POWER_MONITOR_ERR_NO_MEM = -7,           /**< Out of memory */
    POWER_MONITOR_ERR_TASK_FAIL = -8,        /**< Task creation failed */
} power_monitor_err_t;

/**
 * @brief Voltage monitoring configuration
 */
typedef struct {
    int gpio_pin;                            /**< ADC GPIO pin (GPIO 18) */
    float divider_ratio;                     /**< Voltage divider ratio */
    uint32_t sample_interval_ms;             /**< Sampling interval in ms */
    float voltage_min_threshold;             /**< Minimum voltage threshold */
    float voltage_max_threshold;             /**< Maximum voltage threshold */
    bool enable_threshold_alarm;             /**< Enable threshold alarm */
} voltage_monitor_config_t;

/**
 * @brief Power chip communication configuration
 */
typedef struct {
    int uart_num;                            /**< UART port number */
    int rx_gpio_pin;                         /**< RX GPIO pin (GPIO 47) */
    int baud_rate;                           /**< Baud rate (9600) */
    uint32_t timeout_ms;                     /**< Communication timeout */
    bool enable_protocol_debug;              /**< Enable protocol debugging */
} power_chip_config_t;

/**
 * @brief Power monitor configuration
 */
typedef struct {
    voltage_monitor_config_t voltage_config; /**< Voltage monitoring config */
    power_chip_config_t power_chip_config;   /**< Power chip config */
    bool auto_start_monitoring;              /**< Auto start monitoring on init */
    uint32_t task_stack_size;                /**< Monitoring task stack size */
    int task_priority;                       /**< Monitoring task priority */
} power_monitor_config_t;

/**
 * @brief Voltage monitoring data
 */
typedef struct {
    float supply_voltage;                    /**< Supply voltage in volts */
    uint32_t timestamp;                      /**< Timestamp in milliseconds */
} voltage_monitor_data_t;

/**
 * @brief Power chip data
 */
typedef struct {
    bool valid;                              /**< Data validity flag */
    float voltage;                           /**< Voltage in volts */
    float current;                           /**< Current in amperes */
    float power;                             /**< Power in watts */
    uint8_t raw_data[POWER_CHIP_PACKET_SIZE]; /**< Raw packet data */
    uint32_t timestamp;                      /**< Timestamp in milliseconds */
    bool crc_valid;                          /**< CRC validation result */
} power_chip_data_t;

/**
 * @brief Power monitor statistics
 */
typedef struct {
    uint32_t voltage_samples;                /**< Total voltage samples */
    uint32_t power_chip_packets;             /**< Total power chip packets */
    uint32_t crc_errors;                     /**< CRC error count */
    uint32_t timeout_errors;                 /**< Timeout error count */
    uint32_t threshold_violations;           /**< Threshold violation count */
    uint64_t uptime_ms;                      /**< Uptime in milliseconds */
    float avg_voltage;                       /**< Average voltage */
    float avg_current;                       /**< Average current */
    float avg_power;                         /**< Average power */
} power_monitor_stats_t;

/**
 * @brief Power monitor event types
 */
typedef enum {
    POWER_MONITOR_EVENT_VOLTAGE_THRESHOLD,   /**< Voltage threshold event */
    POWER_MONITOR_EVENT_POWER_DATA_RECEIVED, /**< Power data received event */
    POWER_MONITOR_EVENT_CRC_ERROR,           /**< CRC error event */
    POWER_MONITOR_EVENT_TIMEOUT_ERROR,       /**< Timeout error event */
    POWER_MONITOR_EVENT_MAX,                 /**< Maximum event type */
} power_monitor_event_type_t;

/**
 * @brief Power monitor event callback function
 */
typedef void (*power_monitor_event_callback_t)(power_monitor_event_type_t event_type, void *event_data, void *user_data);

/**
 * @brief Initialize power monitor component
 * 
 * @param config Power monitor configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_init(const power_monitor_config_t *config);

/**
 * @brief Deinitialize power monitor component
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_deinit(void);

/**
 * @brief Start power monitoring
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_start(void);

/**
 * @brief Stop power monitoring
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_stop(void);

/**
 * @brief Get current voltage data
 * 
 * @param data Pointer to store voltage data
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_get_voltage_data(voltage_monitor_data_t *data);

/**
 * @brief Get current power chip data
 * 
 * @param data Pointer to store power chip data
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_get_power_chip_data(power_chip_data_t *data);

/**
 * @brief Get power monitor statistics
 * 
 * @param stats Pointer to store statistics
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_get_stats(power_monitor_stats_t *stats);

/**
 * @brief Reset power monitor statistics
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_reset_stats(void);

/**
 * @brief Set voltage thresholds
 * 
 * @param min_voltage Minimum voltage threshold
 * @param max_voltage Maximum voltage threshold
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_set_voltage_thresholds(float min_voltage, float max_voltage);

/**
 * @brief Get voltage thresholds
 * 
 * @param min_voltage Pointer to store minimum voltage threshold
 * @param max_voltage Pointer to store maximum voltage threshold
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_get_voltage_thresholds(float *min_voltage, float *max_voltage);

/**
 * @brief Enable/disable threshold alarm
 * 
 * @param enable True to enable, false to disable
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_set_threshold_alarm(bool enable);

/**
 * @brief Set monitoring sample interval
 * 
 * @param interval_ms Sample interval in milliseconds
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_set_sample_interval(uint32_t interval_ms);

/**
 * @brief Get monitoring sample interval
 * 
 * @param interval_ms Pointer to store sample interval
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_get_sample_interval(uint32_t *interval_ms);

/**
 * @brief Register event callback
 * 
 * @param callback Callback function
 * @param user_data User data to pass to callback
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_register_callback(power_monitor_event_callback_t callback, void *user_data);

/**
 * @brief Unregister event callback
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_unregister_callback(void);

/**
 * @brief Enable/disable power chip communication debugging
 * 
 * @param enable True to enable, false to disable
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_set_debug_mode(bool enable);

/**
 * @brief Get component status
 * 
 * @return true if initialized and running, false otherwise
 */
bool power_monitor_is_running(void);

/**
 * @brief Load configuration from NVS
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_load_config(void);

/**
 * @brief Save configuration to NVS
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_save_config(void);

/**
 * @brief Get default configuration
 * 
 * @param config Pointer to store default configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_get_default_config(power_monitor_config_t *config);

/**
 * @brief Register console commands
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t power_monitor_register_console_commands(void);

#ifdef __cplusplus
}
#endif