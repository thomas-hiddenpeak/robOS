/**
 * @file hardware_commands.c
 * @brief Hardware Commands Component Implementation
 *
 * @version 1.0.0
 * @date 2025-10-03
 */

#include "hardware_commands.h"
#include "console_core.h"
#include "device_controller.h"
#include "esp_log.h"
#include "gpio_controller.h"
#include "usb_mux_controller.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Private Constants and Macros
 * ============================================================================
 */

#define MAX_PIN_NUM_STR_LEN 4 ///< Maximum length for pin number string

/* ============================================================================
 * Private Type Definitions
 * ============================================================================
 */

/**
 * @brief Hardware commands internal state
 */
typedef struct {
  bool initialized;              ///< Initialization status
  uint32_t gpio_command_count;   ///< GPIO command execution counter
  uint32_t usbmux_command_count; ///< USB MUX command execution counter
  SemaphoreHandle_t mutex;       ///< Mutex for thread safety
} hardware_commands_state_t;

/* ============================================================================
 * Private Variables
 * ============================================================================
 */

static hardware_commands_state_t s_hw_cmd_state = {0};
static const char *TAG = HARDWARE_COMMANDS_TAG;

/* ============================================================================
 * Private Function Declarations
 * ============================================================================
 */

static esp_err_t register_hardware_commands(void);
static esp_err_t unregister_hardware_commands(void);
static esp_err_t parse_pin_number(const char *pin_str, uint8_t *pin);
static void print_gpio_usage(void);
static void print_usbmux_usage(void);
static void print_agx_usage(void);
static void print_lpmu_usage(void);

/* ============================================================================
 * Public Function Implementations
 * ============================================================================
 */

esp_err_t hardware_commands_init(void) {
  if (s_hw_cmd_state.initialized) {
    ESP_LOGW(TAG, "Hardware commands already initialized");
    return ESP_OK;
  }

  // Check dependencies
  if (!gpio_controller_is_initialized()) {
    ESP_LOGE(TAG, "GPIO controller is not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!usb_mux_controller_is_initialized()) {
    ESP_LOGE(TAG, "USB MUX controller is not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!device_controller_is_initialized()) {
    ESP_LOGE(TAG, "Device controller is not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Create mutex
  s_hw_cmd_state.mutex = xSemaphoreCreateMutex();
  if (s_hw_cmd_state.mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }

  // Register commands
  esp_err_t ret = register_hardware_commands();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register hardware commands");
    vSemaphoreDelete(s_hw_cmd_state.mutex);
    s_hw_cmd_state.mutex = NULL;
    return ret;
  }

  s_hw_cmd_state.initialized = true;
  s_hw_cmd_state.gpio_command_count = 0;
  s_hw_cmd_state.usbmux_command_count = 0;

  ESP_LOGI(TAG, "Hardware commands initialized successfully");
  return ESP_OK;
}

esp_err_t hardware_commands_deinit(void) {
  if (!s_hw_cmd_state.initialized) {
    ESP_LOGW(TAG, "Hardware commands not initialized");
    return ESP_OK;
  }

  // Unregister commands
  esp_err_t ret = unregister_hardware_commands();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to unregister hardware commands: %s",
             esp_err_to_name(ret));
  }

  // Cleanup
  if (s_hw_cmd_state.mutex != NULL) {
    vSemaphoreDelete(s_hw_cmd_state.mutex);
    s_hw_cmd_state.mutex = NULL;
  }

  s_hw_cmd_state.initialized = false;

  ESP_LOGI(TAG, "Hardware commands deinitialized");
  return ESP_OK;
}

bool hardware_commands_is_initialized(void) {
  return s_hw_cmd_state.initialized;
}

esp_err_t hardware_cmd_gpio(int argc, char **argv) {
  if (!s_hw_cmd_state.initialized) {
    printf("错误: 硬件命令组件未初始化\r\n");
    return ESP_ERR_INVALID_STATE;
  }

  if (argc < 3) {
    print_gpio_usage();
    return ESP_ERR_INVALID_ARG;
  }

  // Parse pin number
  uint8_t pin;
  esp_err_t ret = parse_pin_number(argv[1], &pin);
  if (ret != ESP_OK) {
    printf("错误: 无效的GPIO引脚号: %s\r\n", argv[1]);
    print_gpio_usage();
    return ESP_ERR_INVALID_ARG;
  }

  // Validate GPIO pin
  ret = gpio_controller_validate_pin(pin);
  if (ret != ESP_OK) {
    printf("错误: GPIO%d 不是有效的引脚\r\n", pin);
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_hw_cmd_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    printf("错误: 获取互斥锁失败\r\n");
    return ESP_FAIL;
  }

  // Execute command
  if (strcmp(argv[2], "high") == 0) {
    ret = gpio_controller_set_output(pin, GPIO_STATE_HIGH);
    if (ret == ESP_OK) {
      printf("GPIO%d 已设置为高电平\r\n", pin);
    } else {
      printf("错误: 设置GPIO%d为高电平失败: %s\r\n", pin, esp_err_to_name(ret));
    }
  } else if (strcmp(argv[2], "low") == 0) {
    ret = gpio_controller_set_output(pin, GPIO_STATE_LOW);
    if (ret == ESP_OK) {
      printf("GPIO%d 已设置为低电平\r\n", pin);
    } else {
      printf("错误: 设置GPIO%d为低电平失败: %s\r\n", pin, esp_err_to_name(ret));
    }
  } else if (strcmp(argv[2], "input") == 0) {
    gpio_state_t state;
    ret = gpio_controller_read_input(pin, &state);
    if (ret == ESP_OK) {
      printf("GPIO%d 输入电平: %s\r\n", pin,
             state == GPIO_STATE_HIGH ? "高" : "低");
    } else {
      printf("错误: 读取GPIO%d输入失败: %s\r\n", pin, esp_err_to_name(ret));
    }
  } else {
    printf("错误: 无效的操作: %s\r\n", argv[2]);
    print_gpio_usage();
    ret = ESP_ERR_INVALID_ARG;
  }

  if (ret == ESP_OK) {
    s_hw_cmd_state.gpio_command_count++;
  }

  xSemaphoreGive(s_hw_cmd_state.mutex);
  return ret;
}

esp_err_t hardware_cmd_usbmux(int argc, char **argv) {
  if (!s_hw_cmd_state.initialized) {
    printf("错误: 硬件命令组件未初始化\r\n");
    return ESP_ERR_INVALID_STATE;
  }

  if (argc < 2) {
    print_usbmux_usage();
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_hw_cmd_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    printf("错误: 获取互斥锁失败\r\n");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;

  if (strcmp(argv[1], "esp32s3") == 0) {
    ret = usb_mux_controller_set_target(USB_MUX_TARGET_ESP32S3);
    if (ret == ESP_OK) {
      printf("USB-C接口已切换到ESP32S3\r\n");
    } else {
      printf("错误: 切换到ESP32S3失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "agx") == 0) {
    ret = usb_mux_controller_set_target(USB_MUX_TARGET_AGX);
    if (ret == ESP_OK) {
      printf("USB-C接口已切换到AGX\r\n");
    } else {
      printf("错误: 切换到AGX失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "lpmu") == 0) {
    ret = usb_mux_controller_set_target(USB_MUX_TARGET_LPMU);
    if (ret == ESP_OK) {
      printf("USB-C接口已切换到LPMU\r\n");
    } else {
      printf("错误: 切换到LPMU失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "status") == 0) {
    usb_mux_target_t current_target;
    ret = usb_mux_controller_get_target(&current_target);
    if (ret == ESP_OK) {
      printf("当前USB-C接口连接到: %s\r\n",
             usb_mux_controller_get_target_name(current_target));
    } else {
      printf("错误: 获取USB MUX状态失败: %s\r\n", esp_err_to_name(ret));
    }
  } else {
    printf("错误: 无效的目标: %s\r\n", argv[1]);
    print_usbmux_usage();
    ret = ESP_ERR_INVALID_ARG;
  }

  if (ret == ESP_OK) {
    s_hw_cmd_state.usbmux_command_count++;
  }

  xSemaphoreGive(s_hw_cmd_state.mutex);
  return ret;
}

/* ============================================================================
 * Private Function Implementations
 * ============================================================================
 */

static esp_err_t register_hardware_commands(void) {
  console_cmd_t commands[] = {
      {.command = "gpio",
       .help = "gpio <pin> high|low|input - GPIO control commands",
       .hint = "<pin> high|low|input",
       .func = hardware_cmd_gpio,
       .min_args = 2,
       .max_args = 2},
      {.command = "usbmux",
       .help = "usbmux esp32s3|agx|lpmu|status - USB MUX control commands",
       .hint = "esp32s3|agx|lpmu|status",
       .func = hardware_cmd_usbmux,
       .min_args = 1,
       .max_args = 1},
      {.command = "agx",
       .help = "agx on|off|reset|recovery|status - AGX device control commands",
       .hint = "on|off|reset|recovery|status",
       .func = hardware_cmd_agx,
       .min_args = 1,
       .max_args = 1},
      {.command = "lpmu",
       .help = "lpmu toggle|reset|status|config - LPMU device control commands",
       .hint = "toggle|reset|status|config",
       .func = hardware_cmd_lpmu,
       .min_args = 1,
       .max_args = 3}};

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    esp_err_t ret = console_register_command(&commands[i]);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register command '%s': %s", commands[i].command,
               esp_err_to_name(ret));
      return ret;
    }
  }

  ESP_LOGD(TAG, "Hardware commands registered successfully");
  return ESP_OK;
}

static esp_err_t unregister_hardware_commands(void) {
  const char *commands[] = {"gpio", "usbmux", "agx", "lpmu"};

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    esp_err_t ret = console_unregister_command(commands[i]);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to unregister command '%s': %s", commands[i],
               esp_err_to_name(ret));
    }
  }

  ESP_LOGD(TAG, "Hardware commands unregistered");
  return ESP_OK;
}

static esp_err_t parse_pin_number(const char *pin_str, uint8_t *pin) {
  if (pin_str == NULL || pin == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  char *endptr;
  long pin_num = strtol(pin_str, &endptr, 10);

  // Check if conversion was successful and string was fully consumed
  if (*endptr != '\0' || pin_num < 0 || pin_num > 255) {
    return ESP_ERR_INVALID_ARG;
  }

  *pin = (uint8_t)pin_num;
  return ESP_OK;
}

static void print_gpio_usage(void) {
  printf("用法: gpio <pin> high|low|input\r\n");
  printf("  <pin>  - GPIO引脚号 (0-48)\r\n");
  printf("  high   - 设置GPIO为高电平输出\r\n");
  printf("  low    - 设置GPIO为低电平输出\r\n");
  printf("  input  - 设置GPIO为输入模式并读取电平\r\n");
  printf("注意: 避免在输出模式下读取状态以防止干扰\r\n");
}

static void print_usbmux_usage(void) {
  printf("用法: usbmux esp32s3|agx|lpmu|status\r\n");
  printf("  esp32s3 - 切换USB-C接口到ESP32S3\r\n");
  printf("  agx     - 切换USB-C接口到AGX\r\n");
  printf("  lpmu    - 切换USB-C接口到LPMU\r\n");
  printf("  status  - 显示当前USB-C接口连接状态\r\n");
}

esp_err_t hardware_cmd_agx(int argc, char **argv) {
  if (!s_hw_cmd_state.initialized) {
    printf("错误: 硬件命令组件未初始化\r\n");
    return ESP_ERR_INVALID_STATE;
  }

  if (argc < 2) {
    print_agx_usage();
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_hw_cmd_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    printf("错误: 获取互斥锁失败\r\n");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;

  if (strcmp(argv[1], "on") == 0) {
    ret = device_controller_agx_power_on();
    if (ret == ESP_OK) {
      printf("AGX 设备开机完成\r\n");
    } else {
      printf("错误: AGX 设备开机失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "off") == 0) {
    ret = device_controller_agx_power_off();
    if (ret == ESP_OK) {
      printf("AGX 设备关机完成\r\n");
    } else {
      printf("错误: AGX 设备关机失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "reset") == 0) {
    ret = device_controller_agx_reset();
    if (ret == ESP_OK) {
      printf("AGX 设备重启完成\r\n");
    } else {
      printf("错误: AGX 设备重启失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "recovery") == 0) {
    ret = device_controller_agx_enter_recovery_mode();
    if (ret == ESP_OK) {
      printf("AGX 设备强制恢复模式完成\r\n");
    } else {
      printf("错误: AGX 设备强制恢复模式失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "status") == 0) {
    power_state_t power_state;
    ret = device_controller_agx_get_power_state(&power_state);
    if (ret == ESP_OK) {
      printf("AGX 设备状态: %s\r\n",
             power_state == POWER_STATE_ON ? "开机" : "关机");
    } else {
      printf("错误: 获取 AGX 设备状态失败: %s\r\n", esp_err_to_name(ret));
    }
  } else {
    printf("错误: 无效的操作: %s\r\n", argv[1]);
    print_agx_usage();
    ret = ESP_ERR_INVALID_ARG;
  }

  xSemaphoreGive(s_hw_cmd_state.mutex);
  return ret;
}

esp_err_t hardware_cmd_lpmu(int argc, char **argv) {
  if (!s_hw_cmd_state.initialized) {
    printf("错误: 硬件命令组件未初始化\r\n");
    return ESP_ERR_INVALID_STATE;
  }

  if (argc < 2) {
    print_lpmu_usage();
    return ESP_ERR_INVALID_ARG;
  }

  // Take mutex for thread safety
  if (xSemaphoreTake(s_hw_cmd_state.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    printf("错误: 获取互斥锁失败\r\n");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;

  if (strcmp(argv[1], "toggle") == 0) {
    ret = device_controller_lpmu_power_toggle();
    if (ret == ESP_OK) {
      printf("LPMU 设备电源切换完成\r\n");
    } else {
      printf("错误: LPMU 设备电源切换失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "reset") == 0) {
    ret = device_controller_lpmu_reset();
    if (ret == ESP_OK) {
      printf("LPMU 设备重启完成\r\n");
    } else {
      printf("错误: LPMU 设备重启失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "status") == 0) {
    power_state_t power_state;
    ret = device_controller_lpmu_get_power_state(&power_state);
    if (ret == ESP_OK) {
      const char *state_str;
      switch (power_state) {
      case POWER_STATE_ON:
        state_str = "开机";
        break;
      case POWER_STATE_OFF:
        state_str = "关机";
        break;
      case POWER_STATE_UNKNOWN:
        state_str = "未知 (使用 toggle 命令启动)";
        break;
      default:
        state_str = "无效状态";
        break;
      }
      printf("LPMU 设备状态: %s\r\n", state_str);

      // Also show auto-start configuration
      bool auto_start;
      if (device_controller_get_lpmu_auto_start(&auto_start) == ESP_OK) {
        printf("开机自启动: %s\r\n", auto_start ? "开启" : "关闭");
      }
    } else {
      printf("错误: 获取 LPMU 设备状态失败: %s\r\n", esp_err_to_name(ret));
    }
  } else if (strcmp(argv[1], "config") == 0) {
    if (argc < 3) {
      printf("用法: lpmu config auto-start [on|off]\r\n");
      ret = ESP_ERR_INVALID_ARG;
    } else if (strcmp(argv[2], "auto-start") == 0) {
      if (argc == 3) {
        // Show current auto-start status
        bool auto_start;
        ret = device_controller_get_lpmu_auto_start(&auto_start);
        if (ret == ESP_OK) {
          printf("LPMU 开机自启动: %s\r\n", auto_start ? "开启" : "关闭");
        } else {
          printf("错误: 获取自启动状态失败: %s\r\n", esp_err_to_name(ret));
        }
      } else if (argc == 4) {
        // Set auto-start status
        bool auto_start;
        if (strcmp(argv[3], "on") == 0) {
          auto_start = true;
        } else if (strcmp(argv[3], "off") == 0) {
          auto_start = false;
        } else {
          printf("错误: 无效的参数，请使用 'on' 或 'off'\r\n");
          ret = ESP_ERR_INVALID_ARG;
        }

        if (ret == ESP_OK) {
          ret = device_controller_set_lpmu_auto_start(auto_start);
          if (ret == ESP_OK) {
            printf("LPMU 开机自启动已%s\r\n", auto_start ? "开启" : "关闭");
          } else {
            printf("错误: 设置自启动失败: %s\r\n", esp_err_to_name(ret));
          }
        }
      } else {
        printf("用法: lpmu config auto-start [on|off]\r\n");
        ret = ESP_ERR_INVALID_ARG;
      }
    } else {
      printf("错误: 无效的配置选项: %s\r\n", argv[2]);
      printf("可用选项: auto-start\r\n");
      ret = ESP_ERR_INVALID_ARG;
    }
  } else {
    printf("错误: 无效的操作: %s\r\n", argv[1]);
    print_lpmu_usage();
    ret = ESP_ERR_INVALID_ARG;
  }

  xSemaphoreGive(s_hw_cmd_state.mutex);
  return ret;
}

static void print_agx_usage(void) {
  printf("用法: agx on|off|reset|recovery|status\r\n");
  printf("  on       - 开启AGX设备电源\r\n");
  printf("  off      - 关闭AGX设备电源\r\n");
  printf("  reset    - 重启AGX设备\r\n");
  printf("  recovery - 强制AGX设备进入恢复模式\r\n");
  printf("  status   - 显示AGX设备电源状态\r\n");
}

static void print_lpmu_usage(void) {
  printf("用法: lpmu toggle|reset|status|config\r\n");
  printf("  toggle - 切换LPMU设备电源状态\r\n");
  printf("  reset  - 重启LPMU设备\r\n");
  printf("  status - 显示LPMU设备电源状态\r\n");
  printf("  config - 配置LPMU设备选项\r\n");
  printf("    config auto-start on|off  - 设置开机自动启动\r\n");
  printf("    config auto-start         - 查看自动启动状态\r\n");
}