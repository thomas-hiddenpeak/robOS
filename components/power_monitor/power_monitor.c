/**
 * @file power_monitor.c
 * @brief Power Monitor Component Implementation
 * 
 * This component provides comprehensive power monitoring capabilities for robOS.
 * 
 * @author robOS Team  
 * @date 2025-10-02
 */

#include "power_monitor.h"
#include "hardware_hal.h"
#include "console_core.h"
#include "config_manager.h"
#include "event_manager.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "power_monitor";

// Power chip protocol constants (based on reference implementation)
#define POWER_CHIP_START_BYTE      0xAA      // Packet start marker
#define POWER_CHIP_END_BYTE        0x55      // Packet end marker

/**
 * @brief Power monitor state structure
 */
typedef struct {
    bool initialized;                        /**< Initialization flag */
    bool running;                           /**< Running flag */
    power_monitor_config_t config;          /**< Configuration */
    
    // Voltage monitoring
    adc_oneshot_unit_handle_t adc_handle;   /**< ADC handle */
    adc_cali_handle_t adc_cali_handle;      /**< ADC calibration handle */
    voltage_monitor_data_t latest_voltage;  /**< Latest voltage data */
    float last_supply_voltage;              /**< Last recorded supply voltage */
    float voltage_threshold;                /**< Voltage change threshold */
    
    // Power chip communication
    power_chip_data_t latest_power_data;    /**< Latest power chip data */
    
    // Task handles
    TaskHandle_t monitor_task_handle;       /**< Monitor task handle */
    
    // Synchronization
    SemaphoreHandle_t data_mutex;           /**< Data access mutex */
    QueueHandle_t uart_queue;               /**< UART event queue */
    
    // Statistics
    power_monitor_stats_t stats;            /**< Statistics */
    uint64_t start_time_us;                 /**< Start time */
    
    // Callback
    power_monitor_event_callback_t callback; /**< Event callback */
    void *callback_user_data;               /**< Callback user data */
    
} power_monitor_state_t;

static power_monitor_state_t s_power_monitor = {0};

// Forward declarations
static void power_monitor_task(void *pvParameters);
static esp_err_t voltage_monitor_init(void);
static esp_err_t power_chip_init(void);
static esp_err_t read_voltage_sample(voltage_monitor_data_t *data);
static bool check_voltage_change(void);
static esp_err_t read_power_chip_packet(power_chip_data_t *data);
static uint16_t crc16_ccitt(const uint8_t *data, size_t len);

static void update_statistics(void);
static void trigger_event(power_monitor_event_type_t event_type, void *event_data);

// Console command handlers
static int cmd_power_status(int argc, char **argv);
static int cmd_power_start(int argc, char **argv);
static int cmd_power_stop(int argc, char **argv);
static int cmd_power_config(int argc, char **argv);
static int cmd_power_thresholds(int argc, char **argv);
static int cmd_power_debug(int argc, char **argv);
static int cmd_power_stats(int argc, char **argv);
static int cmd_power_reset(int argc, char **argv);
static int cmd_power_voltage(int argc, char **argv);
static int cmd_power_chip(int argc, char **argv);

esp_err_t power_monitor_get_default_config(power_monitor_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(power_monitor_config_t));
    
    // Voltage monitoring defaults
    config->voltage_config.gpio_pin = 18;                    // GPIO 18 (ADC2_CHANNEL_7)
    config->voltage_config.divider_ratio = VOLTAGE_DIVIDER_RATIO;
    config->voltage_config.sample_interval_ms = 5000;        // 5 seconds (like reference)
    config->voltage_config.voltage_min_threshold = 10.0f;    // 10V minimum
    config->voltage_config.voltage_max_threshold = 30.0f;    // 30V maximum
    config->voltage_config.enable_threshold_alarm = true;
    
    // Power chip communication defaults
    config->power_chip_config.uart_num = UART_NUM_1;         // UART1
    config->power_chip_config.rx_gpio_pin = 47;              // GPIO 47 (UART1_RX)
    config->power_chip_config.baud_rate = 9600;              // 9600 baud
    config->power_chip_config.timeout_ms = 1000;             // 1 second timeout
    config->power_chip_config.enable_protocol_debug = false;
    
    // Task configuration
    config->auto_start_monitoring = true;
    config->task_stack_size = 4096;                          // 4KB stack
    config->task_priority = 5;                               // Medium priority
    
    return ESP_OK;
}

esp_err_t power_monitor_init(const power_monitor_config_t *config)
{
    if (s_power_monitor.initialized) {
        ESP_LOGW(TAG, "Power monitor already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "Configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing power monitor v%s", POWER_MONITOR_VERSION);
    
    // Copy configuration
    memcpy(&s_power_monitor.config, config, sizeof(power_monitor_config_t));
    
    // Create mutex
    s_power_monitor.data_mutex = xSemaphoreCreateMutex();
    if (s_power_monitor.data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize voltage monitoring
    esp_err_t ret = voltage_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize voltage monitor: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Initialize power chip communication
    ret = power_chip_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize power chip communication: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Initialize statistics
    memset(&s_power_monitor.stats, 0, sizeof(power_monitor_stats_t));
    s_power_monitor.start_time_us = esp_timer_get_time();
    
    s_power_monitor.initialized = true;
    
    // Auto-start monitoring if configured
    if (config->auto_start_monitoring) {
        ret = power_monitor_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to auto-start monitoring: %s", esp_err_to_name(ret));
        }
    }
    
    ESP_LOGI(TAG, "Power monitor initialized successfully");
    return ESP_OK;
    
cleanup:
    if (s_power_monitor.data_mutex) {
        vSemaphoreDelete(s_power_monitor.data_mutex);
        s_power_monitor.data_mutex = NULL;
    }
    return ret;
}

esp_err_t power_monitor_deinit(void)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing power monitor");
    
    // Stop monitoring
    power_monitor_stop();
    
    // Clean up ADC
    if (s_power_monitor.adc_handle) {
        adc_oneshot_del_unit(s_power_monitor.adc_handle);
        s_power_monitor.adc_handle = NULL;
    }
    
    // Clean up UART
    uart_driver_delete(s_power_monitor.config.power_chip_config.uart_num);
    
    // Clean up mutex
    if (s_power_monitor.data_mutex) {
        vSemaphoreDelete(s_power_monitor.data_mutex);
        s_power_monitor.data_mutex = NULL;
    }
    
    // Clean up queue
    if (s_power_monitor.uart_queue) {
        vQueueDelete(s_power_monitor.uart_queue);
        s_power_monitor.uart_queue = NULL;
    }
    
    s_power_monitor.initialized = false;
    s_power_monitor.running = false;
    
    ESP_LOGI(TAG, "Power monitor deinitialized");
    return ESP_OK;
}

esp_err_t power_monitor_start(void)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_power_monitor.running) {
        ESP_LOGW(TAG, "Power monitor already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting power monitor");
    
    // Create monitoring task
    BaseType_t ret = xTaskCreate(
        power_monitor_task,
        "power_monitor",
        s_power_monitor.config.task_stack_size,
        NULL,
        s_power_monitor.config.task_priority,
        &s_power_monitor.monitor_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitoring task");
        return ESP_ERR_NO_MEM;
    }
    
    s_power_monitor.running = true;
    s_power_monitor.start_time_us = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Power monitor started");
    return ESP_OK;
}

esp_err_t power_monitor_stop(void)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_power_monitor.running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping power monitor");
    
    s_power_monitor.running = false;
    
    // Delete monitoring task
    if (s_power_monitor.monitor_task_handle) {
        vTaskDelete(s_power_monitor.monitor_task_handle);
        s_power_monitor.monitor_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Power monitor stopped");
    return ESP_OK;
}

static esp_err_t voltage_monitor_init(void)
{
    ESP_LOGI(TAG, "Initializing voltage monitor on GPIO %d", 
             s_power_monitor.config.voltage_config.gpio_pin);
    
    // Configure ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_2,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &s_power_monitor.adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,   // 0-3.3V range for higher voltage measurements
    };
    
    ret = adc_oneshot_config_channel(s_power_monitor.adc_handle, ADC_CHANNEL_7, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Voltage monitor initialized successfully");
    return ESP_OK;
}

static esp_err_t power_chip_init(void)
{
    ESP_LOGI(TAG, "Initializing power chip communication on GPIO %d", 
             s_power_monitor.config.power_chip_config.rx_gpio_pin);
    
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = s_power_monitor.config.power_chip_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    int uart_num = s_power_monitor.config.power_chip_config.uart_num;
    
    esp_err_t ret = uart_driver_install(uart_num, 1024, 1024, 10, &s_power_monitor.uart_queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_param_config(uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_set_pin(uart_num, UART_PIN_NO_CHANGE, s_power_monitor.config.power_chip_config.rx_gpio_pin, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Power chip communication initialized successfully");
    return ESP_OK;
}

static void power_monitor_task(void *pvParameters)
{
    voltage_monitor_data_t voltage_data;
    power_chip_data_t power_data;
    TickType_t last_voltage_time = 0;
    
    ESP_LOGI(TAG, "Power monitor task started");
    
    while (s_power_monitor.running) {
        TickType_t current_time = xTaskGetTickCount();
        
        // Read voltage sample at configured interval
        if (current_time - last_voltage_time >= pdMS_TO_TICKS(s_power_monitor.config.voltage_config.sample_interval_ms)) {
            if (read_voltage_sample(&voltage_data) == ESP_OK) {
                if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    memcpy(&s_power_monitor.latest_voltage, &voltage_data, sizeof(voltage_monitor_data_t));
                    s_power_monitor.stats.voltage_samples++;
                    
                    // Update average voltage
                    s_power_monitor.stats.avg_voltage = 
                        (s_power_monitor.stats.avg_voltage * (s_power_monitor.stats.voltage_samples - 1) + voltage_data.supply_voltage) 
                        / s_power_monitor.stats.voltage_samples;
                    
                    xSemaphoreGive(s_power_monitor.data_mutex);
                }
                
                // Check thresholds
                if (s_power_monitor.config.voltage_config.enable_threshold_alarm) {
                    if (voltage_data.supply_voltage < s_power_monitor.config.voltage_config.voltage_min_threshold ||
                        voltage_data.supply_voltage > s_power_monitor.config.voltage_config.voltage_max_threshold) {
                        // Mark as threshold violation (no threshold_alarm field in new struct)
                        s_power_monitor.stats.threshold_violations++;
                        trigger_event(POWER_MONITOR_EVENT_VOLTAGE_THRESHOLD, &voltage_data);
                    }
                }
            }
            last_voltage_time = current_time;
        }
        
        // Check for power chip data
        if (read_power_chip_packet(&power_data) == ESP_OK) {
            if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                memcpy(&s_power_monitor.latest_power_data, &power_data, sizeof(power_chip_data_t));
                s_power_monitor.stats.power_chip_packets++;
                
                if (power_data.crc_valid) {
                    // Update averages
                    s_power_monitor.stats.avg_current = 
                        (s_power_monitor.stats.avg_current * (s_power_monitor.stats.power_chip_packets - 1) + power_data.current) 
                        / s_power_monitor.stats.power_chip_packets;
                    
                    s_power_monitor.stats.avg_power = 
                        (s_power_monitor.stats.avg_power * (s_power_monitor.stats.power_chip_packets - 1) + power_data.power) 
                        / s_power_monitor.stats.power_chip_packets;
                } else {
                    s_power_monitor.stats.crc_errors++;
                    trigger_event(POWER_MONITOR_EVENT_CRC_ERROR, &power_data);
                }
                
                xSemaphoreGive(s_power_monitor.data_mutex);
            }
            
            trigger_event(POWER_MONITOR_EVENT_POWER_DATA_RECEIVED, &power_data);
        }
        
        // Check for significant voltage changes (every 5 seconds)
        static TickType_t last_voltage_check = 0;
        if (current_time - last_voltage_check >= pdMS_TO_TICKS(5000)) {
            if (check_voltage_change()) {
                ESP_LOGI(TAG, "Significant voltage change detected");
                trigger_event(POWER_MONITOR_EVENT_VOLTAGE_THRESHOLD, NULL);
            }
            last_voltage_check = current_time;
        }
        
        // Update uptime
        update_statistics();
        
        // Small delay to prevent task from hogging CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Power monitor task ended");
    vTaskDelete(NULL);
}

static esp_err_t read_voltage_sample(voltage_monitor_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_power_monitor.initialized || s_power_monitor.adc_handle == NULL) {
        ESP_LOGW(TAG, "Power monitor not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    int raw_adc;
    int voltage_mv;
    
    // Read ADC raw value (GPIO18 -> ADC2_CHANNEL_7)
    esp_err_t ret = adc_oneshot_read(s_power_monitor.adc_handle, ADC_CHANNEL_7, &raw_adc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read supply voltage ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Calibrate to voltage value
    if (s_power_monitor.adc_cali_handle != NULL) {
        ret = adc_cali_raw_to_voltage(s_power_monitor.adc_cali_handle, raw_adc, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to calibrate supply voltage: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        // If no calibration, use default linear conversion
        voltage_mv = (raw_adc * ADC_REF_VOLTAGE_MV) / ADC_MAX_VALUE;
    }
    
    // Calculate actual voltage based on voltage divider circuit
    // Measured: ADC 2.43V corresponds to actual 27.8V, divider ratio ~11.4
    float actual_voltage = (voltage_mv / 1000.0f) * s_power_monitor.config.voltage_config.divider_ratio;
    
    data->supply_voltage = actual_voltage;
    data->timestamp = esp_log_timestamp();
    
    ESP_LOGD(TAG, "Supply voltage: raw=%d, mv=%d, actual=%.2fV", raw_adc, voltage_mv, actual_voltage);
    return ESP_OK;
}

static bool check_voltage_change(void)
{
    voltage_monitor_data_t voltage_data;
    esp_err_t ret = read_voltage_sample(&voltage_data);
    if (ret != ESP_OK) {
        return false;
    }
    
    bool voltage_changed = false;
    
    // Check supply voltage change
    if (s_power_monitor.last_supply_voltage > 0 && 
        fabsf(voltage_data.supply_voltage - s_power_monitor.last_supply_voltage) > s_power_monitor.voltage_threshold) {
        ESP_LOGI(TAG, "Supply voltage changed: %.2fV -> %.2fV (threshold: %.2fV)", 
                 s_power_monitor.last_supply_voltage, voltage_data.supply_voltage, s_power_monitor.voltage_threshold);
        voltage_changed = true;
    }
    
    // Update stored values
    s_power_monitor.last_supply_voltage = voltage_data.supply_voltage;
    
    // Update global status
    if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_power_monitor.latest_voltage = voltage_data;
        xSemaphoreGive(s_power_monitor.data_mutex);
    }
    
    return voltage_changed;
}

static esp_err_t read_power_chip_packet(power_chip_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t buffer[POWER_CHIP_PACKET_SIZE];
    memset(buffer, 0, sizeof(buffer));
    int uart_num = s_power_monitor.config.power_chip_config.uart_num;
    
    // Try to read a complete packet
    int bytes_read = uart_read_bytes(uart_num, buffer, POWER_CHIP_PACKET_SIZE, 
                                   pdMS_TO_TICKS(s_power_monitor.config.power_chip_config.timeout_ms));
    
    if (bytes_read <= 0) {
        ESP_LOGD(TAG, "No power chip data available");
        return ESP_ERR_NOT_FOUND;
    }
    
    if (bytes_read != POWER_CHIP_PACKET_SIZE) {
        ESP_LOGW(TAG, "Incomplete power chip packet: got %d bytes, expected %d", 
                 bytes_read, POWER_CHIP_PACKET_SIZE);
        s_power_monitor.stats.timeout_errors++;
        trigger_event(POWER_MONITOR_EVENT_TIMEOUT_ERROR, NULL);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Validate packet structure
    if (buffer[0] != POWER_CHIP_START_BYTE || buffer[POWER_CHIP_PACKET_SIZE-1] != POWER_CHIP_END_BYTE) {
        ESP_LOGW(TAG, "Invalid power chip packet structure: start=0x%02X, end=0x%02X", 
                 buffer[0], buffer[POWER_CHIP_PACKET_SIZE-1]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Verify CRC16 checksum (last 2 bytes before end byte)
    uint16_t calculated_crc = crc16_ccitt(buffer, POWER_CHIP_PACKET_SIZE - 3); 
    uint16_t received_crc = (buffer[POWER_CHIP_PACKET_SIZE-3] << 8) | buffer[POWER_CHIP_PACKET_SIZE-2];
    
    bool crc_valid = (calculated_crc == received_crc);
    if (!crc_valid) {
        ESP_LOGW(TAG, "Power chip CRC mismatch: calc=0x%04X, recv=0x%04X", calculated_crc, received_crc);
        s_power_monitor.stats.crc_errors++;
        // Don't return error - just mark as invalid but continue parsing for debugging
    }
    
    // Parse power data (adjust based on actual chip protocol)
    // Assuming 16-bit values in big-endian format
    uint16_t voltage_raw = (buffer[1] << 8) | buffer[2];    // Voltage in 0.01V units
    uint16_t current_raw = (buffer[3] << 8) | buffer[4];    // Current in 0.1mA units
    
    float voltage = voltage_raw / 100.0f;      // Convert to volts
    float current = current_raw / 10000.0f;    // Convert to amps
    float power = voltage * current;
    
    // Fill data structure with improved format
    data->voltage = voltage;
    data->current = current;
    data->power = power;
    data->valid = crc_valid;
    data->timestamp = esp_log_timestamp();
    
    if (s_power_monitor.config.power_chip_config.enable_protocol_debug) {
        ESP_LOGI(TAG, "Power chip: V=%.2fV, I=%.3fA, P=%.2fW, CRC=%s [raw: V=%u, I=%u]", 
                 voltage, current, power, crc_valid ? "OK" : "FAIL", voltage_raw, current_raw);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, POWER_CHIP_PACKET_SIZE, ESP_LOG_DEBUG);
    }
    
    return ESP_OK;
}



static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}



static void update_statistics(void)
{
    if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_power_monitor.stats.uptime_ms = (esp_timer_get_time() - s_power_monitor.start_time_us) / 1000;
        xSemaphoreGive(s_power_monitor.data_mutex);
    }
}

static void trigger_event(power_monitor_event_type_t event_type, void *event_data)
{
    if (s_power_monitor.callback) {
        s_power_monitor.callback(event_type, event_data, s_power_monitor.callback_user_data);
    }
}

// Public API implementations
esp_err_t power_monitor_get_voltage_data(voltage_monitor_data_t *data)
{
    if (!s_power_monitor.initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, &s_power_monitor.latest_voltage, sizeof(voltage_monitor_data_t));
        xSemaphoreGive(s_power_monitor.data_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t power_monitor_get_power_chip_data(power_chip_data_t *data)
{
    if (!s_power_monitor.initialized || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, &s_power_monitor.latest_power_data, sizeof(power_chip_data_t));
        xSemaphoreGive(s_power_monitor.data_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t power_monitor_get_stats(power_monitor_stats_t *stats)
{
    if (!s_power_monitor.initialized || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &s_power_monitor.stats, sizeof(power_monitor_stats_t));
        xSemaphoreGive(s_power_monitor.data_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t power_monitor_reset_stats(void)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_power_monitor.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&s_power_monitor.stats, 0, sizeof(power_monitor_stats_t));
        s_power_monitor.start_time_us = esp_timer_get_time();
        xSemaphoreGive(s_power_monitor.data_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t power_monitor_set_voltage_thresholds(float min_voltage, float max_voltage)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (min_voltage >= max_voltage || min_voltage < 0 || max_voltage < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_power_monitor.config.voltage_config.voltage_min_threshold = min_voltage;
    s_power_monitor.config.voltage_config.voltage_max_threshold = max_voltage;
    
    ESP_LOGI(TAG, "Voltage thresholds set: %.2fV - %.2fV", min_voltage, max_voltage);
    return ESP_OK;
}

esp_err_t power_monitor_get_voltage_thresholds(float *min_voltage, float *max_voltage)
{
    if (!s_power_monitor.initialized || min_voltage == NULL || max_voltage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *min_voltage = s_power_monitor.config.voltage_config.voltage_min_threshold;
    *max_voltage = s_power_monitor.config.voltage_config.voltage_max_threshold;
    
    return ESP_OK;
}

esp_err_t power_monitor_set_threshold_alarm(bool enable)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_power_monitor.config.voltage_config.enable_threshold_alarm = enable;
    ESP_LOGI(TAG, "Threshold alarm %s", enable ? "enabled" : "disabled");
    
    return ESP_OK;
}

esp_err_t power_monitor_set_sample_interval(uint32_t interval_ms)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (interval_ms < 100 || interval_ms > 60000) {  // 100ms to 60s range
        return ESP_ERR_INVALID_ARG;
    }
    
    s_power_monitor.config.voltage_config.sample_interval_ms = interval_ms;
    ESP_LOGI(TAG, "Sample interval set to %ums", interval_ms);
    
    return ESP_OK;
}

esp_err_t power_monitor_get_sample_interval(uint32_t *interval_ms)
{
    if (!s_power_monitor.initialized || interval_ms == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *interval_ms = s_power_monitor.config.voltage_config.sample_interval_ms;
    return ESP_OK;
}

esp_err_t power_monitor_register_callback(power_monitor_event_callback_t callback, void *user_data)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_power_monitor.callback = callback;
    s_power_monitor.callback_user_data = user_data;
    
    ESP_LOGI(TAG, "Event callback registered");
    return ESP_OK;
}

esp_err_t power_monitor_unregister_callback(void)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_power_monitor.callback = NULL;
    s_power_monitor.callback_user_data = NULL;
    
    ESP_LOGI(TAG, "Event callback unregistered");
    return ESP_OK;
}

esp_err_t power_monitor_set_debug_mode(bool enable)
{
    if (!s_power_monitor.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_power_monitor.config.power_chip_config.enable_protocol_debug = enable;
    ESP_LOGI(TAG, "Protocol debug %s", enable ? "enabled" : "disabled");
    
    return ESP_OK;
}

bool power_monitor_is_running(void)
{
    return s_power_monitor.initialized && s_power_monitor.running;
}

esp_err_t power_monitor_load_config(void)
{
    // TODO: Implement NVS configuration loading
    ESP_LOGW(TAG, "Configuration loading not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t power_monitor_save_config(void)
{
    // TODO: Implement NVS configuration saving
    ESP_LOGW(TAG, "Configuration saving not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

// Console command implementations
static int cmd_power_status(int argc, char **argv)
{
    if (!s_power_monitor.initialized) {
        printf("Power monitor not initialized\n");
        return 1;
    }
    
    voltage_monitor_data_t voltage_data;
    power_chip_data_t power_data;
    power_monitor_stats_t stats;
    
    printf("Power Monitor Status:\n");
    printf("=====================\n");
    printf("Initialized: %s\n", s_power_monitor.initialized ? "Yes" : "No");
    printf("Running: %s\n", s_power_monitor.running ? "Yes" : "No");
    
    if (power_monitor_get_voltage_data(&voltage_data) == ESP_OK) {
        printf("\nVoltage Monitoring:\n");
        printf("  Current Voltage: %.2fV\n", voltage_data.supply_voltage);
        printf("  Timestamp: %lu ms\n", (unsigned long)voltage_data.timestamp);
        
        float min_thresh, max_thresh;
        if (power_monitor_get_voltage_thresholds(&min_thresh, &max_thresh) == ESP_OK) {
            printf("  Thresholds: %.2fV - %.2fV\n", min_thresh, max_thresh);
        }
        
        uint32_t interval;
        if (power_monitor_get_sample_interval(&interval) == ESP_OK) {
            printf("  Sample Interval: %lums\n", (unsigned long)interval);
        }
    }
    
    if (power_monitor_get_power_chip_data(&power_data) == ESP_OK) {
        printf("\nPower Chip Data:\n");
        printf("  Voltage: %.2fV\n", power_data.voltage);
        printf("  Current: %.3fA\n", power_data.current);
        printf("  Power: %.2fW\n", power_data.power);
        printf("  Valid: %s\n", power_data.valid ? "Yes" : "No");
        printf("  Raw Data: %02X %02X %02X %02X\n", 
               power_data.raw_data[0], power_data.raw_data[1], 
               power_data.raw_data[2], power_data.raw_data[3]);
    }
    
    if (power_monitor_get_stats(&stats) == ESP_OK) {
        printf("\nStatistics:\n");
        printf("  Uptime: %llums\n", stats.uptime_ms);
        printf("  Voltage Samples: %lu\n", (unsigned long)stats.voltage_samples);
        printf("  Power Chip Packets: %lu\n", (unsigned long)stats.power_chip_packets);
        printf("  CRC Errors: %lu\n", (unsigned long)stats.crc_errors);
        printf("  Timeout Errors: %lu\n", (unsigned long)stats.timeout_errors);
        printf("  Threshold Violations: %lu\n", (unsigned long)stats.threshold_violations);
        printf("  Average Voltage: %.2fV\n", stats.avg_voltage);
        printf("  Average Current: %.3fA\n", stats.avg_current);
        printf("  Average Power: %.2fW\n", stats.avg_power);
    }
    
    return 0;
}

static int cmd_power_start(int argc, char **argv)
{
    esp_err_t ret = power_monitor_start();
    if (ret == ESP_OK) {
        printf("Power monitor started\n");
        return 0;
    } else {
        printf("Failed to start power monitor: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_stop(int argc, char **argv)
{
    esp_err_t ret = power_monitor_stop();
    if (ret == ESP_OK) {
        printf("Power monitor stopped\n");
        return 0;
    } else {
        printf("Failed to stop power monitor: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_config(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: power config <save|load|show>\n");
        return 1;
    }
    
    if (strcmp(argv[1], "save") == 0) {
        esp_err_t ret = power_monitor_save_config();
        if (ret == ESP_OK) {
            printf("Configuration saved\n");
            return 0;
        } else {
            printf("Failed to save configuration: %s\n", esp_err_to_name(ret));
            return 1;
        }
    } else if (strcmp(argv[1], "load") == 0) {
        esp_err_t ret = power_monitor_load_config();
        if (ret == ESP_OK) {
            printf("Configuration loaded\n");
            return 0;
        } else {
            printf("Failed to load configuration: %s\n", esp_err_to_name(ret));
            return 1;
        }
    } else if (strcmp(argv[1], "show") == 0) {
        printf("Current Configuration:\n");
        printf("=====================\n");
        printf("Voltage Monitor:\n");
        printf("  GPIO Pin: %d\n", s_power_monitor.config.voltage_config.gpio_pin);
        printf("  Divider Ratio: %.1f:1\n", s_power_monitor.config.voltage_config.divider_ratio);
        printf("  Sample Interval: %lums\n", (unsigned long)s_power_monitor.config.voltage_config.sample_interval_ms);
        printf("  Min Threshold: %.2fV\n", s_power_monitor.config.voltage_config.voltage_min_threshold);
        printf("  Max Threshold: %.2fV\n", s_power_monitor.config.voltage_config.voltage_max_threshold);
        printf("  Threshold Alarm: %s\n", s_power_monitor.config.voltage_config.enable_threshold_alarm ? "Enabled" : "Disabled");
        
        printf("\nPower Chip:\n");
        printf("  UART Number: %d\n", s_power_monitor.config.power_chip_config.uart_num);
        printf("  RX GPIO Pin: %d\n", s_power_monitor.config.power_chip_config.rx_gpio_pin);
        printf("  Baud Rate: %d\n", s_power_monitor.config.power_chip_config.baud_rate);
        printf("  Timeout: %lums\n", (unsigned long)s_power_monitor.config.power_chip_config.timeout_ms);
        printf("  Protocol Debug: %s\n", s_power_monitor.config.power_chip_config.enable_protocol_debug ? "Enabled" : "Disabled");
        
        printf("\nTask Configuration:\n");
        printf("  Auto Start: %s\n", s_power_monitor.config.auto_start_monitoring ? "Yes" : "No");
        printf("  Stack Size: %lu bytes\n", (unsigned long)s_power_monitor.config.task_stack_size);
        printf("  Priority: %d\n", s_power_monitor.config.task_priority);
        
        return 0;
    } else {
        printf("Invalid command. Use: save, load, or show\n");
        return 1;
    }
}

static int cmd_power_thresholds(int argc, char **argv)
{
    if (argc < 2) {
        float min_thresh, max_thresh;
        if (power_monitor_get_voltage_thresholds(&min_thresh, &max_thresh) == ESP_OK) {
            printf("Current voltage thresholds: %.2fV - %.2fV\n", min_thresh, max_thresh);
        }
        printf("Usage: power thresholds <min_voltage> <max_voltage>\n");
        printf("       power thresholds enable|disable\n");
        return 1;
    }
    
    if (strcmp(argv[1], "enable") == 0) {
        esp_err_t ret = power_monitor_set_threshold_alarm(true);
        if (ret == ESP_OK) {
            printf("Threshold alarm enabled\n");
            return 0;
        } else {
            printf("Failed to enable threshold alarm: %s\n", esp_err_to_name(ret));
            return 1;
        }
    } else if (strcmp(argv[1], "disable") == 0) {
        esp_err_t ret = power_monitor_set_threshold_alarm(false);
        if (ret == ESP_OK) {
            printf("Threshold alarm disabled\n");
            return 0;
        } else {
            printf("Failed to disable threshold alarm: %s\n", esp_err_to_name(ret));
            return 1;
        }
    } else if (argc >= 3) {
        float min_voltage = atof(argv[1]);
        float max_voltage = atof(argv[2]);
        
        esp_err_t ret = power_monitor_set_voltage_thresholds(min_voltage, max_voltage);
        if (ret == ESP_OK) {
            printf("Voltage thresholds set: %.2fV - %.2fV\n", min_voltage, max_voltage);
            return 0;
        } else {
            printf("Failed to set voltage thresholds: %s\n", esp_err_to_name(ret));
            return 1;
        }
    }
    
    return 1;
}

static int cmd_power_debug(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: power debug enable|disable\n");
        return 1;
    }
    
    bool enable = (strcmp(argv[1], "enable") == 0);
    esp_err_t ret = power_monitor_set_debug_mode(enable);
    
    if (ret == ESP_OK) {
        printf("Protocol debug %s\n", enable ? "enabled" : "disabled");
        return 0;
    } else {
        printf("Failed to set debug mode: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_stats(int argc, char **argv)
{
    power_monitor_stats_t stats;
    
    if (power_monitor_get_stats(&stats) != ESP_OK) {
        printf("Failed to get statistics\n");
        return 1;
    }
    
    printf("Power Monitor Statistics:\n");
    printf("========================\n");
    printf("Uptime: %llu ms (%.1f hours)\n", stats.uptime_ms, stats.uptime_ms / 3600000.0);
    printf("Voltage Samples: %lu\n", (unsigned long)stats.voltage_samples);
    printf("Power Chip Packets: %lu\n", (unsigned long)stats.power_chip_packets);
    printf("CRC Errors: %lu (%.1f%%)\n", (unsigned long)stats.crc_errors, 
           stats.power_chip_packets > 0 ? (stats.crc_errors * 100.0f / stats.power_chip_packets) : 0.0f);
    printf("Timeout Errors: %lu\n", (unsigned long)stats.timeout_errors);
    printf("Threshold Violations: %lu\n", (unsigned long)stats.threshold_violations);
    printf("Average Voltage: %.2fV\n", stats.avg_voltage);
    printf("Average Current: %.3fA\n", stats.avg_current);
    printf("Average Power: %.2fW\n", stats.avg_power);
    
    return 0;
}

static int cmd_power_reset(int argc, char **argv)
{
    esp_err_t ret = power_monitor_reset_stats();
    if (ret == ESP_OK) {
        printf("Statistics reset\n");
        return 0;
    } else {
        printf("Failed to reset statistics: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_voltage(int argc, char **argv)
{
    voltage_monitor_data_t voltage_data;
    
    if (power_monitor_get_voltage_data(&voltage_data) != ESP_OK) {
        printf("Failed to get voltage data\n");
        return 1;
    }
    
    printf("Voltage Monitoring Data:\n");
    printf("=======================\n");
    printf("Voltage: %.2fV\n", voltage_data.supply_voltage);
    printf("Timestamp: %lu ms\n", (unsigned long)voltage_data.timestamp);
    
    if (argc > 1 && strcmp(argv[1], "interval") == 0) {
        if (argc > 2) {
            uint32_t interval = atoi(argv[2]);
            esp_err_t ret = power_monitor_set_sample_interval(interval);
            if (ret == ESP_OK) {
                printf("Sample interval set to %lums\n", (unsigned long)interval);
                return 0;
            } else {
                printf("Failed to set sample interval: %s\n", esp_err_to_name(ret));
                return 1;
            }
        } else {
            uint32_t interval;
            if (power_monitor_get_sample_interval(&interval) == ESP_OK) {
                printf("Current sample interval: %lums\n", (unsigned long)interval);
            }
            printf("Usage: power voltage interval <milliseconds>\n");
        }
    }
    
    return 0;
}

static int cmd_power_chip(int argc, char **argv)
{
    power_chip_data_t power_data;
    
    if (power_monitor_get_power_chip_data(&power_data) != ESP_OK) {
        printf("Failed to get power chip data\n");
        return 1;
    }
    
    printf("Power Chip Data:\n");
    printf("================\n");
    printf("Voltage: %.2fV\n", power_data.voltage);
    printf("Current: %.3fA\n", power_data.current);
    printf("Power: %.2fW\n", power_data.power);
    printf("Valid: %s\n", power_data.valid ? "Yes" : "No");
    printf("Timestamp: %lu ms\n", (unsigned long)power_data.timestamp);
    printf("Raw Data: ");
    for (int i = 0; i < POWER_CHIP_PACKET_SIZE; i++) {
        printf("%02X ", power_data.raw_data[i]);
    }
    printf("\n");
    
    return 0;
}

esp_err_t power_monitor_register_console_commands(void)
{
    static const console_cmd_t commands[] = {
        {
            .command = "power",
            .help = "Power monitor status and control",
            .hint = NULL,
            .func = cmd_power_status,
            .min_args = 0,
            .max_args = 0,
        },
        {
            .command = "power start",
            .help = "Start power monitoring",
            .hint = NULL,
            .func = cmd_power_start,
            .min_args = 0,
            .max_args = 0,
        },
        {
            .command = "power stop", 
            .help = "Stop power monitoring",
            .hint = NULL,
            .func = cmd_power_stop,
            .min_args = 0,
            .max_args = 0,
        },
        {
            .command = "power config",
            .help = "Configuration management",
            .hint = "<save|load|show>",
            .func = cmd_power_config,
            .min_args = 1,
            .max_args = 1,
        },
        {
            .command = "power thresholds",
            .help = "Voltage threshold management",
            .hint = "<min_voltage> <max_voltage> or enable|disable",
            .func = cmd_power_thresholds,
            .min_args = 1,
            .max_args = 2,
        },
        {
            .command = "power debug",
            .help = "Enable/disable protocol debugging",
            .hint = "enable|disable",
            .func = cmd_power_debug,
            .min_args = 1,
            .max_args = 1,
        },
        {
            .command = "power stats",
            .help = "Show detailed statistics",
            .hint = NULL,
            .func = cmd_power_stats,
            .min_args = 0,
            .max_args = 0,
        },
        {
            .command = "power reset",
            .help = "Reset statistics",
            .hint = NULL,
            .func = cmd_power_reset,
            .min_args = 0,
            .max_args = 0,
        },
        {
            .command = "power voltage",
            .help = "Voltage monitoring control",
            .hint = "[interval <ms>]",
            .func = cmd_power_voltage,
            .min_args = 0,
            .max_args = 2,
        },
        {
            .command = "power chip",
            .help = "Power chip data display",
            .hint = NULL,
            .func = cmd_power_chip,
            .min_args = 0,
            .max_args = 0,
        },
    };
    
    ESP_LOGI(TAG, "Registering %d console commands", sizeof(commands) / sizeof(commands[0]));
    
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        esp_err_t ret = console_register_command(&commands[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register command '%s': %s", 
                     commands[i].command, esp_err_to_name(ret));
            return ret;
        }
    }
    
    ESP_LOGI(TAG, "Console commands registered successfully");
    return ESP_OK;
}