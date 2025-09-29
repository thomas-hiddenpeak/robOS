/**
 * @file color_correction.c
 * @brief Color Correction for WS2812 LED Matrix Implementation
 */

#include "color_correction.h"
#include "config_manager.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include "cJSON.h"

static const char *TAG = "color_correction";

// Global configuration
static color_correction_config_t g_config = {0};
static bool g_initialized = false;
static color_correction_change_callback_t g_change_callback = NULL;

// Configuration keys for NVS storage
#define COLOR_CORRECTION_NAMESPACE "color_corr"
#define CONFIG_KEY_ENABLED "enabled"
#define CONFIG_KEY_WHITE_POINT "white_point"
#define CONFIG_KEY_GAMMA "gamma"
#define CONFIG_KEY_BRIGHTNESS "brightness"
#define CONFIG_KEY_SATURATION "saturation"

// Lookup table for gamma correction (pre-calculated for performance)
static uint8_t gamma_lut[256];
static bool gamma_lut_initialized = false;
static float current_gamma = 0.0f;

// Function declarations  
static void notify_config_change(void);
static esp_err_t create_config_json(cJSON **json_out);
static esp_err_t parse_config_json(const cJSON *json);

/**
 * @brief Clamp a float value to [0.0, 1.0] range
 */
static inline float clamp_float(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

/**
 * @brief Clamp an integer value to [0, 255] range
 */
static inline uint8_t clamp_uint8(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

/**
 * @brief Initialize gamma lookup table
 */
static void init_gamma_lut(float gamma) {
    if (gamma_lut_initialized && fabsf(current_gamma - gamma) < 0.001f) {
        return; // LUT already initialized with the same gamma
    }
    
    current_gamma = gamma;
    for (int i = 0; i < 256; i++) {
        float normalized = i / 255.0f;
        float corrected = powf(normalized, 1.0f / gamma);
        gamma_lut[i] = (uint8_t)(corrected * 255.0f + 0.5f);
    }
    gamma_lut_initialized = true;
    
    ESP_LOGD(TAG, "Gamma LUT initialized with gamma=%.2f", gamma);
}

/**
 * @brief Apply gamma correction using lookup table
 */
static inline uint8_t apply_gamma_lut(uint8_t value) {
    return gamma_lut[value];
}

/**
 * @brief Get default configuration
 */
esp_err_t color_correction_get_default_config(color_correction_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(color_correction_config_t));
    
    // Default settings
    config->enabled = false;
    
    // White point correction (neutral by default)
    config->white_point.enabled = false;
    config->white_point.red_scale = 1.0f;
    config->white_point.green_scale = 1.0f;
    config->white_point.blue_scale = 1.0f;
    
    // Gamma correction (standard sRGB gamma)
    config->gamma.enabled = false;
    config->gamma.gamma = 2.2f;
    
    // Brightness enhancement (no change by default)
    config->brightness.enabled = false;
    config->brightness.factor = 1.0f;
    
    // Saturation enhancement (no change by default)
    config->saturation.enabled = false;
    config->saturation.factor = 1.0f;
    
    return ESP_OK;
}

/**
 * @brief Load configuration from NVS
 */
static esp_err_t load_config_from_nvs(void) {
    esp_err_t ret;
    
    // Load main enabled flag
    bool enabled;
    size_t size = sizeof(enabled);
    ret = config_manager_get(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_ENABLED, CONFIG_TYPE_BOOL, &enabled, &size);
    if (ret == ESP_OK) {
        g_config.enabled = enabled;
    }
    
    // Load white point configuration
    size = sizeof(g_config.white_point);
    ret = config_manager_get(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_WHITE_POINT, CONFIG_TYPE_BLOB, 
                             &g_config.white_point, &size);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "White point config not found, using defaults");
        g_config.white_point.enabled = false;
        g_config.white_point.red_scale = 1.0f;
        g_config.white_point.green_scale = 1.0f;
        g_config.white_point.blue_scale = 1.0f;
    }
    
    // Load gamma configuration
    size = sizeof(g_config.gamma);
    ret = config_manager_get(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_GAMMA, CONFIG_TYPE_BLOB,
                             &g_config.gamma, &size);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Gamma config not found, using defaults");
        g_config.gamma.enabled = false;
        g_config.gamma.gamma = 2.2f;
    }
    
    // Load brightness configuration
    size = sizeof(g_config.brightness);
    ret = config_manager_get(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_BRIGHTNESS, CONFIG_TYPE_BLOB,
                             &g_config.brightness, &size);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Brightness config not found, using defaults");
        g_config.brightness.enabled = false;
        g_config.brightness.factor = 1.0f;
    }
    
    // Load saturation configuration
    size = sizeof(g_config.saturation);
    ret = config_manager_get(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_SATURATION, CONFIG_TYPE_BLOB,
                             &g_config.saturation, &size);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Saturation config not found, using defaults");
        g_config.saturation.enabled = false;
        g_config.saturation.factor = 1.0f;
    }
    
    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return ESP_OK;
}

/**
 * @brief Save configuration to NVS
 */
static esp_err_t save_config_to_nvs(void) {
    esp_err_t ret;
    
    // Save main enabled flag
    ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_ENABLED, CONFIG_TYPE_BOOL, 
                             &g_config.enabled, sizeof(g_config.enabled));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save enabled flag: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save white point configuration
    ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_WHITE_POINT, CONFIG_TYPE_BLOB,
                             &g_config.white_point, sizeof(g_config.white_point));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save white point config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save gamma configuration
    ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_GAMMA, CONFIG_TYPE_BLOB,
                             &g_config.gamma, sizeof(g_config.gamma));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save gamma config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save brightness configuration
    ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_BRIGHTNESS, CONFIG_TYPE_BLOB,
                             &g_config.brightness, sizeof(g_config.brightness));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save brightness config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save saturation configuration
    ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_SATURATION, CONFIG_TYPE_BLOB,
                             &g_config.saturation, sizeof(g_config.saturation));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save saturation config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Commit changes
    ret = config_manager_commit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit configuration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Configuration saved to NVS");
    return ESP_OK;
}

esp_err_t color_correction_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Color correction already initialized");
        return ESP_OK;
    }
    
    // Initialize with default configuration
    esp_err_t ret = color_correction_get_default_config(&g_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get default configuration");
        return ret;
    }
    
    // Load configuration from NVS
    load_config_from_nvs();
    
    // Initialize gamma LUT if gamma correction is enabled
    if (g_config.gamma.enabled) {
        init_gamma_lut(g_config.gamma.gamma);
    }
    
    g_initialized = true;
    
    ESP_LOGI(TAG, "Color correction initialized (enabled: %s)", 
             g_config.enabled ? "true" : "false");
    
    return ESP_OK;
}

esp_err_t color_correction_deinit(void) {
    if (!g_initialized) {
        ESP_LOGW(TAG, "Color correction not initialized");
        return ESP_OK;
    }
    
    g_initialized = false;
    gamma_lut_initialized = false;
    
    ESP_LOGI(TAG, "Color correction deinitialized");
    return ESP_OK;
}

esp_err_t color_correction_set_config(const color_correction_config_t *config) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate configuration
    if (config->white_point.red_scale < 0.0f || config->white_point.red_scale > 2.0f ||
        config->white_point.green_scale < 0.0f || config->white_point.green_scale > 2.0f ||
        config->white_point.blue_scale < 0.0f || config->white_point.blue_scale > 2.0f) {
        ESP_LOGE(TAG, "Invalid white point scale factors");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->gamma.gamma < 0.1f || config->gamma.gamma > 4.0f) {
        ESP_LOGE(TAG, "Invalid gamma value: %.2f", config->gamma.gamma);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->brightness.factor < 0.0f || config->brightness.factor > 2.0f) {
        ESP_LOGE(TAG, "Invalid brightness factor: %.2f", config->brightness.factor);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->saturation.factor < 0.0f || config->saturation.factor > 2.0f) {
        ESP_LOGE(TAG, "Invalid saturation factor: %.2f", config->saturation.factor);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&g_config, config, sizeof(color_correction_config_t));
    
    // Reinitialize gamma LUT if gamma settings changed
    if (g_config.gamma.enabled) {
        init_gamma_lut(g_config.gamma.gamma);
    }
    
    // Save to NVS
    esp_err_t ret = save_config_to_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save configuration to NVS");
    }
    
    ESP_LOGI(TAG, "Configuration updated");
    return ESP_OK;
}

esp_err_t color_correction_get_config(color_correction_config_t *config) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(config, &g_config, sizeof(color_correction_config_t));
    return ESP_OK;
}

void color_rgb_to_hsl(const color_rgb_t *rgb, color_hsl_t *hsl) {
    float r = rgb->r / 255.0f;
    float g = rgb->g / 255.0f;
    float b = rgb->b / 255.0f;
    
    float max_val = fmaxf(r, fmaxf(g, b));
    float min_val = fminf(r, fminf(g, b));
    float delta = max_val - min_val;
    
    // Lightness
    hsl->l = (max_val + min_val) / 2.0f;
    
    if (delta < 0.0001f) {
        // Achromatic
        hsl->h = 0.0f;
        hsl->s = 0.0f;
    } else {
        // Saturation
        if (hsl->l < 0.5f) {
            hsl->s = delta / (max_val + min_val);
        } else {
            hsl->s = delta / (2.0f - max_val - min_val);
        }
        
        // Hue
        if (max_val == r) {
            hsl->h = ((g - b) / delta) * 60.0f;
            if (g < b) {
                hsl->h += 360.0f;
            }
        } else if (max_val == g) {
            hsl->h = ((b - r) / delta + 2.0f) * 60.0f;
        } else {
            hsl->h = ((r - g) / delta + 4.0f) * 60.0f;
        }
    }
}

static float hue_to_rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

void color_hsl_to_rgb(const color_hsl_t *hsl, color_rgb_t *rgb) {
    float h = hsl->h / 360.0f;
    float s = clamp_float(hsl->s);
    float l = clamp_float(hsl->l);
    
    if (s < 0.0001f) {
        // Achromatic
        rgb->r = rgb->g = rgb->b = (uint8_t)(l * 255.0f + 0.5f);
    } else {
        float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
        float p = 2.0f * l - q;
        
        float r = hue_to_rgb(p, q, h + 1.0f/3.0f);
        float g = hue_to_rgb(p, q, h);
        float b = hue_to_rgb(p, q, h - 1.0f/3.0f);
        
        rgb->r = (uint8_t)(r * 255.0f + 0.5f);
        rgb->g = (uint8_t)(g * 255.0f + 0.5f);
        rgb->b = (uint8_t)(b * 255.0f + 0.5f);
    }
}

esp_err_t color_correction_apply_pixel(const color_rgb_t *input, color_rgb_t *output) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (input == NULL || output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // If color correction is disabled, just copy input to output
    if (!g_config.enabled) {
        *output = *input;
        return ESP_OK;
    }
    
    color_rgb_t working = *input;
    
    // Debug: Log first few pixels to verify color correction is working
    static int debug_count = 0;
    if (debug_count < 3 && (input->r > 0 || input->g > 0 || input->b > 0)) {
        ESP_LOGI(TAG, "Color correction input: R%d G%d B%d", input->r, input->g, input->b);
        debug_count++;
    }
    
    // Apply white point correction
    if (g_config.white_point.enabled) {
        float r = working.r * g_config.white_point.red_scale;
        float g = working.g * g_config.white_point.green_scale;
        float b = working.b * g_config.white_point.blue_scale;
        
        working.r = clamp_uint8((int)(r + 0.5f));
        working.g = clamp_uint8((int)(g + 0.5f));
        working.b = clamp_uint8((int)(b + 0.5f));
    }
    
    // Apply gamma correction
    if (g_config.gamma.enabled) {
        if (!gamma_lut_initialized || fabsf(current_gamma - g_config.gamma.gamma) > 0.001f) {
            init_gamma_lut(g_config.gamma.gamma);
        }
        
        working.r = apply_gamma_lut(working.r);
        working.g = apply_gamma_lut(working.g);
        working.b = apply_gamma_lut(working.b);
    }
    
    // Apply brightness and saturation enhancement
    if (g_config.brightness.enabled || g_config.saturation.enabled) {
        color_hsl_t hsl;
        color_rgb_to_hsl(&working, &hsl);
        
        // Apply brightness enhancement
        if (g_config.brightness.enabled) {
            hsl.l = clamp_float(hsl.l * g_config.brightness.factor);
        }
        
        // Apply saturation enhancement
        if (g_config.saturation.enabled) {
            hsl.s = clamp_float(hsl.s * g_config.saturation.factor);
        }
        
        color_hsl_to_rgb(&hsl, &working);
    }
    
    *output = working;
    
    // Debug: Log output for first few pixels
    static int debug_out_count = 0;
    if (debug_out_count < 3 && (working.r > 0 || working.g > 0 || working.b > 0)) {
        ESP_LOGI(TAG, "Color correction output: R%d G%d B%d", working.r, working.g, working.b);
        debug_out_count++;
    }
    
    return ESP_OK;
}

esp_err_t color_correction_apply_array(const color_rgb_t *input, color_rgb_t *output, size_t count) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (input == NULL || output == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // If color correction is disabled, just copy input to output
    if (!g_config.enabled) {
        memcpy(output, input, count * sizeof(color_rgb_t));
        return ESP_OK;
    }
    
    // Apply correction to each pixel
    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = color_correction_apply_pixel(&input[i], &output[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return ESP_OK;
}

esp_err_t color_correction_set_enabled(bool enabled) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    g_config.enabled = enabled;
    
    // Save to NVS
    esp_err_t ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_ENABLED, CONFIG_TYPE_BOOL, 
                                       &enabled, sizeof(enabled));
    if (ret == ESP_OK) {
        ret = config_manager_commit();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save enabled state to NVS");
    }
    
    ESP_LOGI(TAG, "Color correction %s", enabled ? "enabled" : "disabled");
    
    // Notify about the change
    notify_config_change();
    
    return ESP_OK;
}

bool color_correction_is_enabled(void) {
    if (!g_initialized) {
        return false;
    }
    
    return g_config.enabled;
}

esp_err_t color_correction_set_white_point(bool enabled, float red_scale, float green_scale, float blue_scale) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (red_scale < 0.0f || red_scale > 2.0f ||
        green_scale < 0.0f || green_scale > 2.0f ||
        blue_scale < 0.0f || blue_scale > 2.0f) {
        ESP_LOGE(TAG, "Invalid white point scale factors");
        return ESP_ERR_INVALID_ARG;
    }
    
    g_config.white_point.enabled = enabled;
    g_config.white_point.red_scale = red_scale;
    g_config.white_point.green_scale = green_scale;
    g_config.white_point.blue_scale = blue_scale;
    
    // Save to NVS
    esp_err_t ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_WHITE_POINT, CONFIG_TYPE_BLOB,
                                       &g_config.white_point, sizeof(g_config.white_point));
    if (ret == ESP_OK) {
        ret = config_manager_commit();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save white point config to NVS");
    }
    
    ESP_LOGI(TAG, "White point correction %s (R:%.2f G:%.2f B:%.2f)", 
             enabled ? "enabled" : "disabled", red_scale, green_scale, blue_scale);
    
    // Notify about the change
    notify_config_change();
    
    return ESP_OK;
}

esp_err_t color_correction_set_gamma(bool enabled, float gamma) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gamma < 0.1f || gamma > 4.0f) {
        ESP_LOGE(TAG, "Invalid gamma value: %.2f", gamma);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_config.gamma.enabled = enabled;
    g_config.gamma.gamma = gamma;
    
    // Reinitialize gamma LUT
    if (enabled) {
        init_gamma_lut(gamma);
    }
    
    // Save to NVS
    esp_err_t ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_GAMMA, CONFIG_TYPE_BLOB,
                                       &g_config.gamma, sizeof(g_config.gamma));
    if (ret == ESP_OK) {
        ret = config_manager_commit();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save gamma config to NVS");
    }
    
    ESP_LOGI(TAG, "Gamma correction %s (gamma: %.2f)", 
             enabled ? "enabled" : "disabled", gamma);
    
    // Notify about the change
    notify_config_change();
    
    return ESP_OK;
}

esp_err_t color_correction_set_brightness(bool enabled, float factor) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (factor < 0.0f || factor > 2.0f) {
        ESP_LOGE(TAG, "Invalid brightness factor: %.2f", factor);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_config.brightness.enabled = enabled;
    g_config.brightness.factor = factor;
    
    // Save to NVS
    esp_err_t ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_BRIGHTNESS, CONFIG_TYPE_BLOB,
                                       &g_config.brightness, sizeof(g_config.brightness));
    if (ret == ESP_OK) {
        ret = config_manager_commit();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save brightness config to NVS");
    }
    
    ESP_LOGI(TAG, "Brightness enhancement %s (factor: %.2f)", 
             enabled ? "enabled" : "disabled", factor);
    
    // Notify about the change
    notify_config_change();
    
    return ESP_OK;
}

esp_err_t color_correction_set_saturation(bool enabled, float factor) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (factor < 0.0f || factor > 2.0f) {
        ESP_LOGE(TAG, "Invalid saturation factor: %.2f", factor);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_config.saturation.enabled = enabled;
    g_config.saturation.factor = factor;
    
    // Save to NVS
    esp_err_t ret = config_manager_set(COLOR_CORRECTION_NAMESPACE, CONFIG_KEY_SATURATION, CONFIG_TYPE_BLOB,
                                       &g_config.saturation, sizeof(g_config.saturation));
    if (ret == ESP_OK) {
        ret = config_manager_commit();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save saturation config to NVS");
    }
    
    ESP_LOGI(TAG, "Saturation enhancement %s (factor: %.2f)", 
             enabled ? "enabled" : "disabled", factor);
    
    // Notify about the change
    notify_config_change();
    
    return ESP_OK;
}

/**
 * @brief Notify about configuration changes
 */
static void notify_config_change(void) {
    if (g_change_callback) {
        ESP_LOGI(TAG, "Notifying configuration change");
        g_change_callback();
    }
}

esp_err_t color_correction_register_change_callback(color_correction_change_callback_t callback) {
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    g_change_callback = callback;
    ESP_LOGI(TAG, "Change callback registered");
    return ESP_OK;
}

/**
 * @brief Create JSON object from current configuration
 */
static esp_err_t create_config_json(cJSON **json_out) {
    if (json_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }
    
    // Add version info
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON_AddStringToObject(root, "type", "color_correction_config");
    
    // Add main enabled flag
    cJSON_AddBoolToObject(root, "enabled", g_config.enabled);
    
    // Add white point correction
    cJSON *white_point = cJSON_CreateObject();
    if (white_point != NULL) {
        cJSON_AddBoolToObject(white_point, "enabled", g_config.white_point.enabled);
        cJSON_AddNumberToObject(white_point, "red_scale", g_config.white_point.red_scale);
        cJSON_AddNumberToObject(white_point, "green_scale", g_config.white_point.green_scale);
        cJSON_AddNumberToObject(white_point, "blue_scale", g_config.white_point.blue_scale);
        cJSON_AddItemToObject(root, "white_point", white_point);
    }
    
    // Add gamma correction
    cJSON *gamma = cJSON_CreateObject();
    if (gamma != NULL) {
        cJSON_AddBoolToObject(gamma, "enabled", g_config.gamma.enabled);
        cJSON_AddNumberToObject(gamma, "gamma", g_config.gamma.gamma);
        cJSON_AddItemToObject(root, "gamma", gamma);
    }
    
    // Add brightness enhancement
    cJSON *brightness = cJSON_CreateObject();
    if (brightness != NULL) {
        cJSON_AddBoolToObject(brightness, "enabled", g_config.brightness.enabled);
        cJSON_AddNumberToObject(brightness, "factor", g_config.brightness.factor);
        cJSON_AddItemToObject(root, "brightness", brightness);
    }
    
    // Add saturation enhancement
    cJSON *saturation = cJSON_CreateObject();
    if (saturation != NULL) {
        cJSON_AddBoolToObject(saturation, "enabled", g_config.saturation.enabled);
        cJSON_AddNumberToObject(saturation, "factor", g_config.saturation.factor);
        cJSON_AddItemToObject(root, "saturation", saturation);
    }
    
    *json_out = root;
    return ESP_OK;
}

/**
 * @brief Parse JSON configuration and apply to current config
 */
static esp_err_t parse_config_json(const cJSON *json) {
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    color_correction_config_t temp_config;
    memcpy(&temp_config, &g_config, sizeof(color_correction_config_t));
    
    // Parse main enabled flag
    const cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled)) {
        temp_config.enabled = cJSON_IsTrue(enabled);
    }
    
    // Parse white point correction
    const cJSON *white_point = cJSON_GetObjectItem(json, "white_point");
    if (cJSON_IsObject(white_point)) {
        const cJSON *wp_enabled = cJSON_GetObjectItem(white_point, "enabled");
        const cJSON *red_scale = cJSON_GetObjectItem(white_point, "red_scale");
        const cJSON *green_scale = cJSON_GetObjectItem(white_point, "green_scale");
        const cJSON *blue_scale = cJSON_GetObjectItem(white_point, "blue_scale");
        
        if (cJSON_IsBool(wp_enabled)) {
            temp_config.white_point.enabled = cJSON_IsTrue(wp_enabled);
        }
        if (cJSON_IsNumber(red_scale) && red_scale->valuedouble >= 0.0 && red_scale->valuedouble <= 2.0) {
            temp_config.white_point.red_scale = (float)red_scale->valuedouble;
        }
        if (cJSON_IsNumber(green_scale) && green_scale->valuedouble >= 0.0 && green_scale->valuedouble <= 2.0) {
            temp_config.white_point.green_scale = (float)green_scale->valuedouble;
        }
        if (cJSON_IsNumber(blue_scale) && blue_scale->valuedouble >= 0.0 && blue_scale->valuedouble <= 2.0) {
            temp_config.white_point.blue_scale = (float)blue_scale->valuedouble;
        }
    }
    
    // Parse gamma correction
    const cJSON *gamma = cJSON_GetObjectItem(json, "gamma");
    if (cJSON_IsObject(gamma)) {
        const cJSON *gamma_enabled = cJSON_GetObjectItem(gamma, "enabled");
        const cJSON *gamma_value = cJSON_GetObjectItem(gamma, "gamma");
        
        if (cJSON_IsBool(gamma_enabled)) {
            temp_config.gamma.enabled = cJSON_IsTrue(gamma_enabled);
        }
        if (cJSON_IsNumber(gamma_value) && gamma_value->valuedouble >= 0.1 && gamma_value->valuedouble <= 4.0) {
            temp_config.gamma.gamma = (float)gamma_value->valuedouble;
        }
    }
    
    // Parse brightness enhancement
    const cJSON *brightness = cJSON_GetObjectItem(json, "brightness");
    if (cJSON_IsObject(brightness)) {
        const cJSON *brightness_enabled = cJSON_GetObjectItem(brightness, "enabled");
        const cJSON *brightness_factor = cJSON_GetObjectItem(brightness, "factor");
        
        if (cJSON_IsBool(brightness_enabled)) {
            temp_config.brightness.enabled = cJSON_IsTrue(brightness_enabled);
        }
        if (cJSON_IsNumber(brightness_factor) && brightness_factor->valuedouble >= 0.0 && brightness_factor->valuedouble <= 2.0) {
            temp_config.brightness.factor = (float)brightness_factor->valuedouble;
        }
    }
    
    // Parse saturation enhancement
    const cJSON *saturation = cJSON_GetObjectItem(json, "saturation");
    if (cJSON_IsObject(saturation)) {
        const cJSON *saturation_enabled = cJSON_GetObjectItem(saturation, "enabled");
        const cJSON *saturation_factor = cJSON_GetObjectItem(saturation, "factor");
        
        if (cJSON_IsBool(saturation_enabled)) {
            temp_config.saturation.enabled = cJSON_IsTrue(saturation_enabled);
        }
        if (cJSON_IsNumber(saturation_factor) && saturation_factor->valuedouble >= 0.0 && saturation_factor->valuedouble <= 2.0) {
            temp_config.saturation.factor = (float)saturation_factor->valuedouble;
        }
    }
    
    // Apply the temporary configuration
    esp_err_t ret = color_correction_set_config(&temp_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply imported configuration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t color_correction_export_config(const char *file_path) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (file_path == NULL) {
        ESP_LOGE(TAG, "Invalid file path");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create JSON object from current configuration
    cJSON *json = NULL;
    esp_err_t ret = create_config_json(&json);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Convert JSON to string
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to convert JSON to string");
        return ESP_ERR_NO_MEM;
    }
    
    // Write to file
    FILE *file = fopen(file_path, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", file_path);
        free(json_string);
        return ESP_FAIL;
    }
    
    size_t written = fwrite(json_string, 1, strlen(json_string), file);
    fclose(file);
    free(json_string);
    
    if (written == 0) {
        ESP_LOGE(TAG, "Failed to write configuration to file");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Color correction configuration exported to: %s", file_path);
    return ESP_OK;
}

esp_err_t color_correction_import_config(const char *file_path) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Color correction not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (file_path == NULL) {
        ESP_LOGE(TAG, "Invalid file path");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if file exists
    struct stat st;
    if (stat(file_path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", file_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check file size (limit to 4KB for safety)
    if (st.st_size > 4096) {
        ESP_LOGE(TAG, "File too large: %ld bytes (max 4096)", st.st_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Read file content
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", file_path);
        return ESP_FAIL;
    }
    
    char *buffer = malloc(st.st_size + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file content");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    size_t bytes_read = fread(buffer, 1, st.st_size, file);
    fclose(file);
    
    if (bytes_read != st.st_size) {
        ESP_LOGE(TAG, "Failed to read complete file");
        free(buffer);
        return ESP_FAIL;
    }
    
    buffer[st.st_size] = '\0';
    
    // Parse JSON
    cJSON *json = cJSON_Parse(buffer);
    free(buffer);
    
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON from file: %s", file_path);
        return ESP_FAIL;
    }
    
    // Validate JSON type
    const cJSON *type = cJSON_GetObjectItem(json, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "color_correction_config") != 0) {
        ESP_LOGE(TAG, "Invalid configuration file type");
        cJSON_Delete(json);
        return ESP_FAIL;
    }
    
    // Parse and apply configuration
    esp_err_t ret = parse_config_json(json);
    cJSON_Delete(json);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Color correction configuration imported from: %s", file_path);
    }
    
    return ret;
}