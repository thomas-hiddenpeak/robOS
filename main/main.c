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

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

// Component includes
#include "agx_monitor.h"
#include "board_led.h"
#include "config_manager.h"
#include "console_core.h"
#include "device_controller.h"
#include "ethernet_manager.h"
#include "event_manager.h"
#include "fan_controller.h"
#include "gpio_controller.h"
#include "hardware_commands.h"
#include "hardware_hal.h"
#include "matrix_led.h"
#include "power_monitor.h"
#include "storage_manager.h"
#include "touch_led.h"
#include "usb_mux_controller.h"
#include "web_server.h"

static const char *TAG = "ROBOS_MAIN";

// Storage mount synchronization
static SemaphoreHandle_t storage_mount_semaphore = NULL;
static esp_err_t storage_mount_result = ESP_FAIL;

// Web server status tracking - simplified approach

/**
 * @brief Storage mount callback for startup auto-mount with synchronization
 * @param operation Storage operation type
 * @param result Operation result
 * @param data User data (unused)
 */
static void storage_mount_callback(storage_operation_type_t operation,
                                   esp_err_t result, void *data,
                                   void *user_data) {
  if (operation == STORAGE_OP_MOUNT) {
    // Store the mount result for synchronous waiting
    storage_mount_result = result;

    if (result == ESP_OK) {
      ESP_LOGI(TAG, "SD card auto-mount successful - storage ready at /sdcard");
      ESP_LOGI(TAG, "Use 'sdcard' command to enter interactive storage shell");
    } else {
      // Provide friendly error messages
      switch (result) {
      case ESP_ERR_TIMEOUT:
        ESP_LOGW(TAG, "No SD card detected - slot is empty");
        ESP_LOGI(TAG, "Insert an SD card and use 'storage mount' to try again");
        break;
      case ESP_ERR_NOT_FOUND:
        ESP_LOGW(TAG,
                 "SD card not responding - may be damaged or incompatible");
        break;
      case ESP_ERR_NOT_SUPPORTED:
        ESP_LOGW(TAG, "SD card format not supported - may need formatting");
        break;
      case ESP_ERR_INVALID_STATE:
        ESP_LOGW(TAG, "SD card slot in use or hardware conflict");
        break;
      default:
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(result));
        break;
      }
      ESP_LOGI(TAG, "robOS will continue without storage - insert card and use "
                    "'storage mount' when ready");
    }

    // Signal that mount operation is complete
    if (storage_mount_semaphore != NULL) {
      xSemaphoreGive(storage_mount_semaphore);
    }
  }
}

/**
 * @brief Touch event handler for LED interaction
 * @param event Touch event type
 * @param duration Event duration in milliseconds
 */
static void touch_event_handler(touch_event_t event, uint32_t duration) {
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
    touch_led_start_animation(TOUCH_LED_ANIM_BREATHE, 30, TOUCH_LED_COLOR_BLUE,
                              TOUCH_LED_COLOR_OFF);
    break;

  case TOUCH_EVENT_LONG_PRESS:
    ESP_LOGI(TAG, "Long press detected (%lu ms) - starting rainbow", duration);
    touch_led_start_animation(TOUCH_LED_ANIM_RAINBOW, 100, TOUCH_LED_COLOR_RED,
                              TOUCH_LED_COLOR_BLUE);
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
      ESP_LOGI(TAG, "Brightness changed from %d to %d", brightness,
               new_brightness);
    }
    break;

  default:
    break;
  }
}

/**
 * @brief System reboot command handler
 */
static esp_err_t cmd_reboot(int argc, char **argv) {
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
static esp_err_t register_system_commands(void) {
  console_cmd_t reboot_cmd = {.command = "reboot",
                              .help = "Restart the system",
                              .hint = NULL,
                              .func = cmd_reboot,
                              .min_args = 0,
                              .max_args = 1};

  return console_register_command(&reboot_cmd);
}

/**
 * @brief Initialize system components in proper order
 * @return ESP_OK on success
 */
static esp_err_t system_init(void) {
  esp_err_t ret = ESP_OK;

  ESP_LOGI(TAG, "Initializing robOS system...");

  // Initialize NVS (required for WiFi and other components)
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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
    ESP_LOGE(TAG, "Failed to initialize event manager: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Event manager initialized");

  // 2. Hardware HAL (hardware abstraction)
  ret = hardware_hal_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize hardware HAL: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Hardware HAL initialized");

  // 2.1. GPIO Controller (general GPIO operations)
  ret = gpio_controller_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize GPIO controller: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "GPIO controller initialized");

  // 2.2. USB MUX Controller (USB-C interface switching)
  ret = usb_mux_controller_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize USB MUX controller: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "USB MUX controller initialized");

  // 2.3. Device Controller (AGX and LPMU device management)
  ret = device_controller_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize device controller: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Device controller initialized");

  // 3. Console Core (user interface)
  console_config_t console_config = console_get_default_config();
  ret = console_core_init(&console_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize console core: %s",
             esp_err_to_name(ret));
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

  // 3.1. Hardware Commands (GPIO and USB MUX console commands)
  ret = hardware_commands_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize hardware commands: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Hardware commands initialized and registered");

  // 4. Configuration Manager (unified config storage)
  config_manager_config_t config_mgr_config =
      config_manager_get_default_config();
  ret = config_manager_init(&config_mgr_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize config manager: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Configuration manager initialized");

  // Register configuration management commands
  ret = config_manager_register_commands();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register config commands: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "Configuration management commands registered");

  // 5. Fan Controller (cooling management)
  ret = fan_controller_init(NULL); // Use default configuration
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize fan controller: %s",
             esp_err_to_name(ret));
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
      .led_gpio = GPIO_NUM_45,   // WS2812 data line
      .touch_gpio = GPIO_NUM_NC, // Touch sensor pin (not configured yet)
      .led_count = 16,           // Number of LEDs in strip
      .max_brightness = 200,     // Maximum brightness (0-255)
      .touch_threshold = 1000,   // Touch detection threshold
      .touch_invert = false      // Touch logic (false = active low)
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
      ESP_LOGW(TAG, "Failed to register touch callback: %s",
               esp_err_to_name(ret));
    }

    // Note: Initial LED state and animation are handled by the touch_led_init()
    // function which loads saved configuration. We don't set default state here
    // to avoid overriding user's saved preferences.

    ESP_LOGI(TAG, "Touch LED ready - touch sensor to interact");

    // Register touch LED commands
    ret = touch_led_register_commands();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to register touch LED commands: %s",
               esp_err_to_name(ret));
    } else {
      ESP_LOGI(TAG, "Touch LED commands registered");
    }
  }

  // Register system management commands
  ret = register_system_commands();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register system commands: %s",
             esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG, "System commands registered");

  // Initialize Board LED System
  ESP_LOGI(TAG, "Initializing board LED system...");
  ret = board_led_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize board LED: %s", esp_err_to_name(ret));
    // Board LED is not critical, continue with warning
    ESP_LOGW(TAG, "Continuing without board LED functionality");
  } else {
    ESP_LOGI(TAG, "Board LED controller initialized (GPIO %d, %d LEDs)",
             BOARD_LED_GPIO_PIN, BOARD_LED_COUNT);
  }

  // 7. Ethernet Manager (W5500 network controller)
  ESP_LOGI(TAG, "Initializing ethernet manager...");
  ret = ethernet_manager_init(NULL); // Use default configuration
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize ethernet manager: %s",
             esp_err_to_name(ret));
    // Ethernet is not critical for system boot, continue with warning
    ESP_LOGW(TAG, "Continuing without ethernet functionality");
  } else {
    ESP_LOGI(TAG, "Ethernet manager initialized");

    // Register ethernet console commands
    ret = ethernet_manager_register_console_commands();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to register ethernet commands: %s",
               esp_err_to_name(ret));
    } else {
      ESP_LOGI(TAG, "Ethernet console commands registered");
    }

    // Start ethernet manager if auto-start is enabled
    ret = ethernet_manager_start();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to start ethernet manager: %s",
               esp_err_to_name(ret));
    } else {
      ESP_LOGI(TAG, "Ethernet manager started");
    }
  }

  // 8. Storage Manager (TF card file system) - Initialize first
  ESP_LOGI(TAG, "Initializing storage manager...");
  storage_config_t storage_config;
  storage_manager_get_default_config(&storage_config);
  ret = storage_manager_init(&storage_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize storage manager: %s",
             esp_err_to_name(ret));
    // Storage is not critical for system boot, continue with warning
    ESP_LOGW(TAG, "Continuing without storage functionality");
    storage_mount_result = ESP_FAIL; // Mark as failed for Matrix LED init
  } else {
    ESP_LOGI(TAG, "Storage manager initialized");

    // Register storage console commands
    ret = storage_manager_register_console_commands();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to register storage commands: %s",
               esp_err_to_name(ret));
    } else {
      ESP_LOGI(TAG, "Storage commands registered");
    }

    // Create synchronization semaphore for mount operation
    storage_mount_semaphore = xSemaphoreCreateBinary();
    if (storage_mount_semaphore == NULL) {
      ESP_LOGE(TAG, "Failed to create storage mount semaphore");
      storage_mount_result = ESP_FAIL;
    } else {
      // Auto-mount SD card at startup and wait for completion
      ESP_LOGI(TAG, "Attempting to auto-mount SD card...");
      ret = storage_manager_mount_async(storage_mount_callback, NULL);
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initiate SD card mount: %s",
                 esp_err_to_name(ret));
        ESP_LOGW(TAG, "SD card may not be inserted or may have issues");
        storage_mount_result = ret;
      } else {
        ESP_LOGI(TAG, "SD card mount initiated, waiting for completion...");

        // Wait for mount operation to complete (timeout: 10 seconds)
        if (xSemaphoreTake(storage_mount_semaphore, pdMS_TO_TICKS(10000)) ==
            pdTRUE) {
          ESP_LOGI(TAG, "Storage mount operation completed with result: %s",
                   esp_err_to_name(storage_mount_result));
        } else {
          ESP_LOGW(TAG, "Storage mount operation timed out");
          storage_mount_result = ESP_ERR_TIMEOUT;
        }
      }

      // Clean up semaphore
      vSemaphoreDelete(storage_mount_semaphore);
      storage_mount_semaphore = NULL;
    }
  }

  // 8. Matrix LED Controller (32x32 WS2812 Matrix) - Initialize after storage
  ESP_LOGI(TAG, "Initializing matrix LED controller (storage available: %s)...",
           storage_mount_result == ESP_OK ? "Yes" : "No");
  ret = matrix_led_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize matrix LED: %s", esp_err_to_name(ret));
    // Matrix LED is not critical for system boot, continue with warning
    ESP_LOGW(TAG, "Continuing without matrix LED functionality");
  } else {
    ESP_LOGI(
        TAG,
        "Matrix LED controller initialized (GPIO %d, 32x32 matrix, 1024 LEDs)",
        MATRIX_LED_GPIO);

    // Configuration is loaded automatically during matrix_led_init()
    // No need to show test pattern here as it will be restored from config
  }

  // 9. Power Monitor (voltage monitoring and power chip communication)
  ESP_LOGI(TAG, "Initializing power monitor...");
  power_monitor_config_t power_config;
  ret = power_monitor_get_default_config(&power_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get power monitor default config: %s",
             esp_err_to_name(ret));
    // Power monitor is not critical for system boot, continue with warning
    ESP_LOGW(TAG, "Continuing without power monitor functionality");
  } else {
    ret = power_monitor_init(&power_config);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize power monitor: %s",
               esp_err_to_name(ret));
      // Power monitor is not critical for system boot, continue with warning
      ESP_LOGW(TAG, "Continuing without power monitor functionality");
    } else {
      ESP_LOGI(TAG, "Power monitor initialized (GPIO %d ADC, GPIO %d UART)",
               power_config.voltage_config.gpio_pin,
               power_config.power_chip_config.rx_gpio_pin);

      // Register power monitor console commands
      ret = power_monitor_register_console_commands();
      if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register power monitor commands: %s",
                 esp_err_to_name(ret));
      } else {
        ESP_LOGI(TAG, "Power monitor console commands registered");
      }
    }
  }

  // 10. Web Server (HTTP server for web interface and API)
  // Start web server if storage is mounted (simple approach like reference
  // project)
  if (storage_mount_result == ESP_OK) {
    ESP_LOGI(TAG, "Storage is ready, starting web server...");
    ret = web_server_start();
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
      ESP_LOGW(TAG, "Continuing without web server functionality");
    }
  } else {
    ESP_LOGI(TAG, "Storage not ready, skipping web server start");
    ESP_LOGI(TAG, "Web server will be available after mounting SD card");
  }

  // 11. AGX Monitor (AGX system monitoring via WebSocket)
  ESP_LOGI(TAG, "Initializing AGX monitor...");
  agx_monitor_config_t agx_config;
  ret = agx_monitor_get_default_config(&agx_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AGX monitor default config: %s",
             esp_err_to_name(ret));
    // AGX monitor is not critical for system boot, continue with warning
    ESP_LOGW(TAG, "Continuing without AGX monitor functionality");
  } else {
    ret = agx_monitor_init(&agx_config);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize AGX monitor: %s",
               esp_err_to_name(ret));
      // AGX monitor is not critical for system boot, continue with warning
      ESP_LOGW(TAG, "Continuing without AGX monitor functionality");
    } else {
      ESP_LOGI(TAG, "AGX monitor initialized (WebSocket client for %s:%d)",
               agx_config.server_url, agx_config.server_port);

      // AGX monitor auto-starts if auto_start is enabled in config
      if (!agx_config.auto_start) {
        ret = agx_monitor_start();
        if (ret != ESP_OK) {
          ESP_LOGW(TAG, "Failed to start AGX monitor: %s",
                   esp_err_to_name(ret));
        } else {
          ESP_LOGI(TAG, "AGX monitor started - connecting to AGX server");
        }
      } else {
        ESP_LOGI(TAG, "AGX monitor auto-started during initialization");
      }
    }
  }

  ESP_LOGI(TAG, "robOS system initialization completed");
  return ESP_OK; // System initialization is complete, regardless of individual
                 // component issues
}

/**
 * @brief Main application task
 * @param pvParameters Task parameters (unused)
 */
static void main_task(void *pvParameters) {
  ESP_LOGI(TAG, "Starting robOS main task");

  // Main application loop
  while (1) {
    // Console core runs in its own task, no need to process here
    // System status can be checked using the 'status' command in console
    // Use 'matrix_test' command to start Matrix LED testing

    // Let other tasks run - longer delay since we're not doing anything
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

/**
 * @brief Application entry point
 */
void app_main(void) {
  ESP_LOGI(TAG, "==================================================");
  ESP_LOGI(TAG, "     robOS - RM-01 Board Operating System        ");
  ESP_LOGI(TAG, "     Version: %s                                  ",
           "1.0.0-dev");
  ESP_LOGI(TAG, "     Build: %s %s                                 ", __DATE__,
           __TIME__);
  ESP_LOGI(TAG, "==================================================");

  // Initialize system components
  ESP_ERROR_CHECK(system_init());

  // Create and start main application task
  TaskHandle_t main_task_handle = NULL;
  BaseType_t ret =
      xTaskCreate(main_task, "main_task", 4096, NULL, 5, &main_task_handle);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create main task");
    esp_restart();
  }

  ESP_LOGI(TAG, "robOS startup completed, entering main loop");
}