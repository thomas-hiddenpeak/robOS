/**
 * @file hardware_hal.c
 * @brief Hardware Abstraction Layer implementation
 * 
 * @author robOS Team
 * @date 2025-09-28
 */

#include "hardware_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <string.h>

static const char *TAG = "HARDWARE_HAL";

/**
 * @brief Hardware HAL context structure
 */
typedef struct {
    bool initialized;               /**< Initialization flag */
    SemaphoreHandle_t mutex;        /**< Mutex for thread safety */
    hal_status_t status;           /**< Current status */
    
    // GPIO context
    bool gpio_pins_configured[HAL_GPIO_MAX_PIN]; /**< GPIO pin configuration status */
    
    // UART context
    bool uart_ports_configured[HAL_UART_MAX_PORT]; /**< UART port configuration status */
    
    // SPI context
    bool spi_hosts_configured[HAL_SPI_MAX_HOST]; /**< SPI host configuration status */
    spi_device_handle_t spi_devices[HAL_SPI_MAX_HOST]; /**< SPI device handles */
    
    // PWM context
    bool pwm_channels_configured[HAL_PWM_MAX_CHANNEL]; /**< PWM channel configuration status */
    
    // ADC context
    adc_oneshot_unit_handle_t adc_handles[SOC_ADC_PERIPH_NUM]; /**< ADC unit handles */
    adc_cali_handle_t adc_cali_handles[SOC_ADC_PERIPH_NUM]; /**< ADC calibration handles */
    bool adc_units_configured[SOC_ADC_PERIPH_NUM]; /**< ADC unit configuration status */
} hardware_hal_context_t;

static hardware_hal_context_t s_hal_ctx = {0};

/**
 * @brief Get ADC unit index from unit enum
 */
static inline int get_adc_unit_index(adc_unit_t unit)
{
    return (unit == ADC_UNIT_1) ? 0 : 1;
}

esp_err_t hardware_hal_init(void)
{
    if (s_hal_ctx.initialized) {
        ESP_LOGW(TAG, "Hardware HAL already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing Hardware HAL...");
    
    // Initialize mutex
    s_hal_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_hal_ctx.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize context
    memset(&s_hal_ctx.status, 0, sizeof(s_hal_ctx.status));
    memset(s_hal_ctx.gpio_pins_configured, false, sizeof(s_hal_ctx.gpio_pins_configured));
    memset(s_hal_ctx.uart_ports_configured, false, sizeof(s_hal_ctx.uart_ports_configured));
    memset(s_hal_ctx.spi_hosts_configured, false, sizeof(s_hal_ctx.spi_hosts_configured));
    memset(s_hal_ctx.pwm_channels_configured, false, sizeof(s_hal_ctx.pwm_channels_configured));
    memset(s_hal_ctx.adc_units_configured, false, sizeof(s_hal_ctx.adc_units_configured));
    
    s_hal_ctx.initialized = true;
    s_hal_ctx.status.initialized = true;
    
    ESP_LOGI(TAG, "Hardware HAL initialized successfully");
    return ESP_OK;
}

esp_err_t hardware_hal_deinit(void)
{
    if (!s_hal_ctx.initialized) {
        ESP_LOGW(TAG, "Hardware HAL not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing Hardware HAL...");
    
    if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex for deinitialization");
        return ESP_ERR_TIMEOUT;
    }
    
    // Cleanup ADC calibration handles
    for (int i = 0; i < SOC_ADC_PERIPH_NUM; i++) {
        if (s_hal_ctx.adc_cali_handles[i]) {
            adc_cali_delete_scheme_curve_fitting(s_hal_ctx.adc_cali_handles[i]);
            s_hal_ctx.adc_cali_handles[i] = NULL;
        }
        if (s_hal_ctx.adc_handles[i]) {
            adc_oneshot_del_unit(s_hal_ctx.adc_handles[i]);
            s_hal_ctx.adc_handles[i] = NULL;
        }
    }
    
    // Cleanup SPI devices
    for (int i = 0; i < HAL_SPI_MAX_HOST; i++) {
        if (s_hal_ctx.spi_devices[i]) {
            spi_bus_remove_device(s_hal_ctx.spi_devices[i]);
            s_hal_ctx.spi_devices[i] = NULL;
        }
        if (s_hal_ctx.spi_hosts_configured[i]) {
            spi_bus_free((spi_host_device_t)i);
            s_hal_ctx.spi_hosts_configured[i] = false;
        }
    }
    
    // Reset context
    memset(&s_hal_ctx.status, 0, sizeof(s_hal_ctx.status));
    s_hal_ctx.initialized = false;
    
    xSemaphoreGive(s_hal_ctx.mutex);
    vSemaphoreDelete(s_hal_ctx.mutex);
    s_hal_ctx.mutex = NULL;
    
    ESP_LOGI(TAG, "Hardware HAL deinitialized");
    return ESP_OK;
}

bool hardware_hal_is_initialized(void)
{
    return s_hal_ctx.initialized;
}

esp_err_t hardware_hal_get_status(hal_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_hal_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *status = s_hal_ctx.status;
    
    xSemaphoreGive(s_hal_ctx.mutex);
    return ESP_OK;
}

/* ============================================================================
 * GPIO Functions
 * ============================================================================ */

esp_err_t hal_gpio_configure(const hal_gpio_config_t *config)
{
    if (!config || config->pin >= HAL_GPIO_MAX_PIN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_hal_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << config->pin),
        .mode = config->mode,
        .pull_up_en = (config->pull == GPIO_PULLUP_ONLY || config->pull == GPIO_PULLUP_PULLDOWN) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (config->pull == GPIO_PULLDOWN_ONLY || config->pull == GPIO_PULLUP_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = config->intr_type,
    };
    
    esp_err_t ret = gpio_config(&gpio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", config->pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Handle signal inversion
    if (config->invert) {
        ret = gpio_set_intr_type(config->pin, config->intr_type);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set GPIO %d inversion: %s", config->pin, esp_err_to_name(ret));
            return ret;
        }
    }
    
    if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_hal_ctx.gpio_pins_configured[config->pin]) {
            s_hal_ctx.gpio_pins_configured[config->pin] = true;
            s_hal_ctx.status.gpio_count++;
        }
        xSemaphoreGive(s_hal_ctx.mutex);
    }
    
    ESP_LOGD(TAG, "GPIO %d configured successfully", config->pin);
    return ESP_OK;
}

esp_err_t hal_gpio_set_level(gpio_num_t pin, uint32_t level)
{
    if (pin >= HAL_GPIO_MAX_PIN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return gpio_set_level(pin, level);
}

esp_err_t hal_gpio_get_level(gpio_num_t pin, uint32_t *level)
{
    if (pin >= HAL_GPIO_MAX_PIN || !level) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *level = gpio_get_level(pin);
    return ESP_OK;
}

esp_err_t hal_gpio_toggle(gpio_num_t pin)
{
    if (pin >= HAL_GPIO_MAX_PIN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t current_level = gpio_get_level(pin);
    return gpio_set_level(pin, !current_level);
}

/* ============================================================================
 * UART Functions
 * ============================================================================ */

esp_err_t hal_uart_configure(const hal_uart_config_t *config)
{
    if (!config || config->port >= HAL_UART_MAX_PORT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_hal_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure UART parameters
    uart_config_t uart_cfg = {
        .baud_rate = config->baud_rate,
        .data_bits = config->data_bits,
        .parity = config->parity,
        .stop_bits = config->stop_bits,
        .flow_ctrl = config->flow_ctrl,
        .rx_flow_ctrl_thresh = config->rx_flow_ctrl_thresh,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_param_config(config->port, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART %d parameters: %s", config->port, esp_err_to_name(ret));
        return ret;
    }
    
    // Set UART pins
    ret = uart_set_pin(config->port, config->tx_pin, config->rx_pin, config->rts_pin, config->cts_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART %d pins: %s", config->port, esp_err_to_name(ret));
        return ret;
    }
    
    // Install UART driver
    ret = uart_driver_install(config->port, config->rx_buffer_size, config->tx_buffer_size, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART %d driver: %s", config->port, esp_err_to_name(ret));
        return ret;
    }
    
    if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_hal_ctx.uart_ports_configured[config->port]) {
            s_hal_ctx.uart_ports_configured[config->port] = true;
            s_hal_ctx.status.uart_count++;
        }
        xSemaphoreGive(s_hal_ctx.mutex);
    }
    
    ESP_LOGD(TAG, "UART %d configured successfully", config->port);
    return ESP_OK;
}

int hal_uart_write(uart_port_t port, const void *data, size_t length, uint32_t timeout_ms)
{
    if (port >= HAL_UART_MAX_PORT || !data) {
        return -1;
    }
    
    return uart_write_bytes(port, data, length);
}

int hal_uart_read(uart_port_t port, void *data, size_t length, uint32_t timeout_ms)
{
    if (port >= HAL_UART_MAX_PORT || !data) {
        return -1;
    }
    
    return uart_read_bytes(port, data, length, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t hal_uart_available(uart_port_t port, size_t *available)
{
    if (port >= HAL_UART_MAX_PORT || !available) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return uart_get_buffered_data_len(port, available);
}

/* ============================================================================
 * SPI Functions
 * ============================================================================ */

esp_err_t hal_spi_configure(const hal_spi_config_t *config)
{
    if (!config || config->host >= HAL_SPI_MAX_HOST) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_hal_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    
    esp_err_t ret = spi_bus_initialize(config->host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus %d: %s", config->host, esp_err_to_name(ret));
        return ret;
    }
    
    // Configure SPI device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = config->clock_speed,
        .mode = config->mode,
        .spics_io_num = config->cs_pin,
        .queue_size = config->queue_size,
    };
    
    ret = spi_bus_add_device(config->host, &dev_cfg, &s_hal_ctx.spi_devices[config->host]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device %d: %s", config->host, esp_err_to_name(ret));
        spi_bus_free(config->host);
        return ret;
    }
    
    if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_hal_ctx.spi_hosts_configured[config->host]) {
            s_hal_ctx.spi_hosts_configured[config->host] = true;
            s_hal_ctx.status.spi_count++;
        }
        xSemaphoreGive(s_hal_ctx.mutex);
    }
    
    ESP_LOGD(TAG, "SPI %d configured successfully", config->host);
    return ESP_OK;
}

esp_err_t hal_spi_transfer(spi_host_device_t host, const void *tx_data, 
                          void *rx_data, size_t length, uint32_t timeout_ms)
{
    if (host >= HAL_SPI_MAX_HOST || !s_hal_ctx.spi_devices[host]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    spi_transaction_t trans = {
        .length = length * 8,  // Length in bits
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    
    return spi_device_transmit(s_hal_ctx.spi_devices[host], &trans);
}

/* ============================================================================
 * PWM Functions
 * ============================================================================ */

esp_err_t hal_pwm_configure(const hal_pwm_config_t *config)
{
    if (!config || config->channel >= HAL_PWM_MAX_CHANNEL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_hal_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure timer
    ledc_timer_config_t timer_cfg = {
        .timer_num = config->timer,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = config->resolution,
        .freq_hz = config->frequency,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PWM timer %d: %s", config->timer, esp_err_to_name(ret));
        return ret;
    }
    
    // Configure channel
    ledc_channel_config_t channel_cfg = {
        .channel = config->channel,
        .duty = config->duty_cycle,
        .gpio_num = config->pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = config->timer,
        .intr_type = LEDC_INTR_DISABLE,
    };
    
    ret = ledc_channel_config(&channel_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PWM channel %d: %s", config->channel, esp_err_to_name(ret));
        return ret;
    }
    
    if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_hal_ctx.pwm_channels_configured[config->channel]) {
            s_hal_ctx.pwm_channels_configured[config->channel] = true;
            s_hal_ctx.status.pwm_count++;
        }
        xSemaphoreGive(s_hal_ctx.mutex);
    }
    
    ESP_LOGD(TAG, "PWM channel %d configured successfully", config->channel);
    return ESP_OK;
}

esp_err_t hal_pwm_set_duty(ledc_channel_t channel, uint32_t duty_cycle)
{
    if (channel >= HAL_PWM_MAX_CHANNEL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty_cycle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

esp_err_t hal_pwm_set_frequency(ledc_timer_t timer, uint32_t frequency)
{
    return ledc_set_freq(LEDC_LOW_SPEED_MODE, timer, frequency);
}

esp_err_t hal_pwm_start(ledc_channel_t channel)
{
    // PWM starts automatically after configuration
    return ESP_OK;
}

esp_err_t hal_pwm_stop(ledc_channel_t channel)
{
    if (channel >= HAL_PWM_MAX_CHANNEL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0);
}

/* ============================================================================
 * ADC Functions
 * ============================================================================ */

esp_err_t hal_adc_configure(const hal_adc_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_hal_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int unit_idx = get_adc_unit_index(config->unit);
    
    // Configure ADC unit if not already configured
    if (!s_hal_ctx.adc_units_configured[unit_idx]) {
        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = config->unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        
        esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_hal_ctx.adc_handles[unit_idx]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize ADC unit %d: %s", config->unit, esp_err_to_name(ret));
            return ret;
        }
        
        // Initialize calibration
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = config->unit,
            .atten = config->attenuation,
            .bitwidth = config->bitwidth,
        };
        
        ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_hal_ctx.adc_cali_handles[unit_idx]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC calibration not available for unit %d", config->unit);
        }
        
        s_hal_ctx.adc_units_configured[unit_idx] = true;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = config->attenuation,
        .bitwidth = config->bitwidth,
    };
    
    esp_err_t ret = adc_oneshot_config_channel(s_hal_ctx.adc_handles[unit_idx], config->channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel %d: %s", config->channel, esp_err_to_name(ret));
        return ret;
    }
    
    if (xSemaphoreTake(s_hal_ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_hal_ctx.status.adc_count++;
        xSemaphoreGive(s_hal_ctx.mutex);
    }
    
    ESP_LOGD(TAG, "ADC unit %d channel %d configured successfully", config->unit, config->channel);
    return ESP_OK;
}

esp_err_t hal_adc_read_raw(adc_unit_t unit, adc_channel_t channel, int *raw_value)
{
    if (!raw_value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int unit_idx = get_adc_unit_index(unit);
    if (!s_hal_ctx.adc_units_configured[unit_idx]) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return adc_oneshot_read(s_hal_ctx.adc_handles[unit_idx], channel, raw_value);
}

esp_err_t hal_adc_raw_to_voltage(adc_unit_t unit, adc_channel_t channel, 
                                int raw_value, int *voltage_mv)
{
    if (!voltage_mv) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int unit_idx = get_adc_unit_index(unit);
    if (!s_hal_ctx.adc_cali_handles[unit_idx]) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return adc_cali_raw_to_voltage(s_hal_ctx.adc_cali_handles[unit_idx], raw_value, voltage_mv);
}

esp_err_t hal_adc_read_voltage(adc_unit_t unit, adc_channel_t channel, int *voltage_mv)
{
    if (!voltage_mv) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int raw_value;
    esp_err_t ret = hal_adc_read_raw(unit, channel, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return hal_adc_raw_to_voltage(unit, channel, raw_value, voltage_mv);
}