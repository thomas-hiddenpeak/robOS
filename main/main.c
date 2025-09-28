/**
 * @file main.c
 * @brief robOS Main Application Entry Point
 * 
 * This is the main entry point for the robOS (RM-01 Board Operating System).
 * It initializes all system components and starts the main application loop.
 * 
 * @author robOS Team
 * @date 2025
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"

// Component includes
#include "event_manager.h"
#include "hardware_hal.h"
#include "console_core.h"
#include "config_manager.h"
#include "fan_controller.h"
#include "touch_led.h"

static const char *TAG = "ROBOS_MAIN";

/**
 * @brief Touch event handler for LED interaction
 * @param event Touch event type
 * @param duration Event duration in milliseconds
 */
static void touch_event_handler(touch_event_t event, uint32_t duration)
{
    switch (event) {
        case TOUCH_EVENT_PRESS:
            ESP_LOGI(TAG, "Touch pressed - switching to green");
            touch_led_stop_animation();
            touch_led_set_all_color(TOUCH_LED_COLOR_GREEN);
            touch_led_set_brightness(150);
            touch_led_update();
            break;
            
        case TOUCH_EVENT_RELEASE:
            ESP_LOGI(TAG, "Touch released after %lu ms - returning to blue", duration);
            touch_led_set_all_color(TOUCH_LED_COLOR_BLUE);
            touch_led_set_brightness(50);
            touch_led_update();
            
            // Start breathing animation immediately (no blocking delay in callback)
            touch_led_start_animation(TOUCH_LED_ANIM_BREATHE, 30, 
                                     TOUCH_LED_COLOR_BLUE, TOUCH_LED_COLOR_OFF);
            break;
            
        case TOUCH_EVENT_LONG_PRESS:
            ESP_LOGI(TAG, "Long press detected (%lu ms) - starting rainbow", duration);
            touch_led_start_animation(TOUCH_LED_ANIM_RAINBOW, 100,
                                     TOUCH_LED_COLOR_RED, TOUCH_LED_COLOR_BLUE);
            break;
            
        case TOUCH_EVENT_DOUBLE_TAP:
            ESP_LOGI(TAG, "Double tap detected - brightness toggle");
            // Toggle between low and high brightness
            uint16_t led_count;
            uint8_t brightness;
            touch_led_animation_t anim;
            
            if (touch_led_get_status(&led_count, &brightness, &anim) == ESP_OK) {
                uint8_t new_brightness = (brightness < 100) ? 200 : 30;
                touch_led_set_brightness(new_brightness);
                touch_led_update();
                ESP_LOGI(TAG, "Brightness changed from %d to %d", brightness, new_brightness);
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief System reboot command handler
 */
static esp_err_t cmd_reboot(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "-f") == 0 || strcmp(argv[1], "--force") == 0) {
            printf("Force rebooting system now...\n");
            esp_restart();
        } else {
            printf("Usage: reboot [-f|--force]\n");
            printf("  -f, --force    Force immediate reboot without cleanup\n");
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        printf("Rebooting system in 3 seconds...\n");
        printf("Press Ctrl+C to cancel\n");
        
        for (int i = 3; i > 0; i--) {
            printf("Rebooting in %d...\n", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        printf("Rebooting now!\n");
        esp_restart();
    }
    
    return ESP_OK;
}

/**
 * @brief Register system management commands
 */
static esp_err_t register_system_commands(void)
{
    console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Restart the system",
        .hint = NULL,
        .func = cmd_reboot,
        .min_args = 0,
        .max_args = 1
    };
    
    return console_register_command(&reboot_cmd);
}

/**
 * @brief Initialize system components in proper order
 * @return ESP_OK on success
 */
static esp_err_t system_init(void)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Initializing robOS system...");
    
    // Initialize NVS (required for WiFi and other components)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS flash initialized");
    
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Default event loop created");
    
    // Initialize components in dependency order
    // 1. Event Manager (core communication)
    ret = event_manager_init(NULL); // Use default configuration
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize event manager: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Event manager initialized");
    
    // 2. Hardware HAL (hardware abstraction)
    ret = hardware_hal_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize hardware HAL: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Hardware HAL initialized");
    
    // 3. Console Core (user interface)
    console_config_t console_config = console_get_default_config();
    ret = console_core_init(&console_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize console core: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Console core initialized");
    
    // Start console core task
    ret = console_core_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start console core: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Console core started");
    
    // 4. Configuration Manager (unified config storage)
    config_manager_config_t config_mgr_config = config_manager_get_default_config();
    ret = config_manager_init(&config_mgr_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize config manager: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Configuration manager initialized");
    
    // Register configuration management commands
    ret = config_manager_register_commands();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register config commands: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Configuration management commands registered");
    
    // 5. Fan Controller (cooling management)
    ret = fan_controller_init(NULL); // Use default configuration
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize fan controller: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Fan controller initialized");
    
    // Register fan commands with console
    ret = fan_controller_register_commands();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register fan commands: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Fan commands registered");
    
    // 6. Touch LED Controller (visual feedback and interaction)
    touch_led_config_t touch_led_config = {
        .led_gpio = GPIO_NUM_45,        // WS2812 data line
        .touch_gpio = GPIO_NUM_NC,      // Touch sensor pin (not configured yet)
        .led_count = 16,                // Number of LEDs in strip
        .max_brightness = 200,          // Maximum brightness (0-255)
        .touch_threshold = 1000,        // Touch detection threshold
        .touch_invert = false           // Touch logic (false = active low)
    };
    
    ret = touch_led_init(&touch_led_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch LED: %s", esp_err_to_name(ret));
        // Touch LED is not critical, continue with warning
        ESP_LOGW(TAG, "Continuing without touch LED functionality");
    } else {
        ESP_LOGI(TAG, "Touch LED controller initialized");
        
        // Register touch event callback
        ret = touch_led_register_callback(touch_event_handler);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register touch callback: %s", esp_err_to_name(ret));
        }
        
        // Note: Initial LED state and animation are handled by the touch_led_init() function
        // which loads saved configuration. We don't set default state here to avoid
        // overriding user's saved preferences.
        
        ESP_LOGI(TAG, "Touch LED ready - touch sensor to interact");
        
        // Register touch LED commands
        ret = touch_led_register_commands();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to register touch LED commands: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Touch LED commands registered");
        }
    }
    
    // Register system management commands
    ret = register_system_commands();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register system commands: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "System commands registered");
    
    // TODO: Initialize other components (Ethernet, Storage, etc.)
    
    ESP_LOGI(TAG, "robOS system initialization completed");
    return ret;
}

/**
 * @brief Main application task
 * @param pvParameters Task parameters (unused)
 */
static void main_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting robOS main task");
    
    // Main application loop
    while (1) {
        // Console core runs in its own task, no need to process here
        // System status can be checked using the 'status' command in console
        
        // Let other tasks run - longer delay since we're not doing anything
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief Application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "     robOS - RM-01 Board Operating System        ");
    ESP_LOGI(TAG, "     Version: %s                                  ", "1.0.0-dev");
    ESP_LOGI(TAG, "     Build: %s %s                                 ", __DATE__, __TIME__);
    ESP_LOGI(TAG, "==================================================");
    
    // Initialize system components
    ESP_ERROR_CHECK(system_init());
    
    // Create and start main application task
    TaskHandle_t main_task_handle = NULL;
    BaseType_t ret = xTaskCreate(
        main_task,
        "main_task",
        4096,
        NULL,
        5,
        &main_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task");
        esp_restart();
    }
    
    ESP_LOGI(TAG, "robOS startup completed, entering main loop");
}