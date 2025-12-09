#include "voltage_protection.h"
#include "agx_monitor.h"
#include "board_led.h"
#include "console_core.h"
#include "device_controller.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fan_controller.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gpio_controller.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "matrix_led.h"
#include "ping/ping_sock.h"
#include "power_monitor.h"
#include "touch_led.h"
#include <string.h>

static const char *TAG = "volt_prot";

typedef struct {
  bool initialized;
  bool running;
  voltage_protection_config_t config;
  voltage_protection_state_t state;
  float current_voltage;
  float last_voltage;
  uint32_t countdown_remaining_sec;
  uint32_t recovery_timer_sec;
  uint64_t last_websocket_check_us;
  uint64_t agx_last_connected_us;
  uint64_t lpmu_last_connected_us;
  bool agx_powered;
  bool lpmu_powered;
  bool agx_websocket_connected;
  uint32_t protection_count;
  uint64_t start_time_us;
  TaskHandle_t monitor_task_handle;
  SemaphoreHandle_t state_mutex;
  bool test_mode;
  float test_voltage;
  bool fans_stopped;
  uint32_t shutdown_timer_sec;
} voltage_protection_internal_state_t;

static voltage_protection_internal_state_t s_vp_state = {0};

static void voltage_protection_task(void *pvParameters);
static void voltage_protection_check_voltage(float voltage);
static void voltage_protection_check_device_status(void);
static void voltage_protection_execute_shutdown(void);
static void voltage_protection_execute_recovery(void);
static void voltage_protection_update_led_status(void);

// Ping callback data
typedef struct {
  uint32_t transmitted;
  uint32_t received;
  uint32_t total_time_ms;
  bool completed;
} ping_result_t;

static void ping_success_cb(esp_ping_handle_t hdl, void *args) {
  uint8_t ttl;
  uint16_t seqno;
  uint32_t elapsed_time;
  ip_addr_t target_addr;

  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr,
                       sizeof(target_addr));
  esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time,
                       sizeof(elapsed_time));

  ESP_LOGI(TAG, "Ping success: %s icmp_seq=%d ttl=%d time=%lu ms",
           inet_ntoa(target_addr.u_addr.ip4), seqno, ttl,
           (unsigned long)elapsed_time);
}

static void ping_timeout_cb(esp_ping_handle_t hdl, void *args) {
  uint16_t seqno;
  ip_addr_t target_addr;

  esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
  esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr,
                       sizeof(target_addr));

  ESP_LOGW(TAG, "Ping timeout: %s icmp_seq=%d",
           inet_ntoa(target_addr.u_addr.ip4), seqno);
}

static void ping_end_cb(esp_ping_handle_t hdl, void *args) {
  ping_result_t *result = (ping_result_t *)args;
  if (result) {
    // Get final statistics from profile
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &result->transmitted,
                         sizeof(result->transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &result->received,
                         sizeof(result->received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &result->total_time_ms,
                         sizeof(result->total_time_ms));
    result->completed = true;

    ESP_LOGI(
        TAG,
        "Ping completed: %lu packets transmitted, %lu received, time %lu ms",
        (unsigned long)result->transmitted, (unsigned long)result->received,
        (unsigned long)result->total_time_ms);
  }
}

/**
 * @brief Check if a device is online by pinging its IP address
 * @param ip_addr IP address string (e.g., "10.10.99.99")
 * @return true if device responds to ping, false otherwise
 */
static bool is_device_online(const char *ip_addr) {
  ESP_LOGI(TAG, "Checking if device %s is online via ping...", ip_addr);

  esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();

  // Parse IP address
  ip_addr_t target_addr;
  struct addrinfo hint;
  struct addrinfo *res = NULL;
  memset(&hint, 0, sizeof(hint));

  int ret = getaddrinfo(ip_addr, NULL, &hint, &res);
  if (ret != 0 || res == NULL) {
    ESP_LOGW(TAG, "Failed to resolve IP address: %s", ip_addr);
    return false;
  }

  struct in_addr addr4 = ((struct sockaddr_in *)(res->ai_addr))->sin_addr;
  inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
  freeaddrinfo(res);

  ping_config.target_addr = target_addr;
  ping_config.count = 3;         // Send 3 pings for reliability
  ping_config.timeout_ms = 1000; // 1 second timeout per ping
  ping_config.interval_ms = 100; // 100ms between pings

  // Setup result tracking
  ping_result_t result = {0};

  // Setup callbacks (matching official ESP-IDF examples)
  esp_ping_callbacks_t cbs = {.cb_args = &result,
                              .on_ping_success = ping_success_cb,
                              .on_ping_timeout = ping_timeout_cb,
                              .on_ping_end = ping_end_cb};

  esp_ping_handle_t ping_handle;
  ret = esp_ping_new_session(&ping_config, &cbs, &ping_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create ping session for %s: %s", ip_addr,
             esp_err_to_name(ret));
    return false;
  }

  // Start ping
  ret = esp_ping_start(ping_handle);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to start ping to %s: %s", ip_addr,
             esp_err_to_name(ret));
    esp_ping_delete_session(ping_handle);
    return false;
  }

  ESP_LOGI(TAG, "Ping started, waiting for response from %s...", ip_addr);

  // Wait for ping to complete (3 pings * 1s + intervals + buffer)
  for (int i = 0; i < 40 && !result.completed; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Stop and cleanup
  esp_ping_stop(ping_handle);
  esp_ping_delete_session(ping_handle);

  if (!result.completed) {
    ESP_LOGW(TAG, "Ping did not complete within timeout for %s", ip_addr);
    return false;
  }

  if (result.received > 0) {
    ESP_LOGI(TAG, "Device %s is ONLINE (received %lu/%lu pings)", ip_addr,
             (unsigned long)result.received, (unsigned long)result.transmitted);
    return true;
  } else {
    ESP_LOGW(TAG, "Device %s is OFFLINE (no ping response, sent %lu)", ip_addr,
             (unsigned long)result.transmitted);
    return false;
  }
}

esp_err_t
voltage_protection_get_default_config(voltage_protection_config_t *config) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(config, 0, sizeof(voltage_protection_config_t));
  config->low_voltage_threshold = VOLTAGE_PROTECTION_LOW_THRESHOLD;
  config->recovery_voltage_threshold = VOLTAGE_PROTECTION_RECOVERY_THRESHOLD;
  config->shutdown_delay_sec = VOLTAGE_PROTECTION_SHUTDOWN_DELAY_SEC;
  config->recovery_hold_sec = VOLTAGE_PROTECTION_RECOVERY_HOLD_SEC;
  config->websocket_timeout_sec = VOLTAGE_PROTECTION_WEBSOCKET_TIMEOUT_SEC;
  config->auto_recovery_enabled = true;
  config->enable_touch_led_feedback = true;
  config->enable_matrix_led_control = true;
  return ESP_OK;
}

esp_err_t voltage_protection_init(const voltage_protection_config_t *config) {
  if (s_vp_state.initialized)
    return ESP_ERR_INVALID_STATE;
  ESP_LOGI(TAG, "Initializing voltage protection v%s",
           VOLTAGE_PROTECTION_VERSION);

  if (config == NULL) {
    voltage_protection_get_default_config(&s_vp_state.config);
  } else {
    memcpy(&s_vp_state.config, config, sizeof(voltage_protection_config_t));
  }
  s_vp_state.state_mutex = xSemaphoreCreateMutex();
  if (s_vp_state.state_mutex == NULL)
    return ESP_ERR_NO_MEM;
  s_vp_state.state = VOLTAGE_PROTECTION_STATE_NORMAL;
  s_vp_state.start_time_us = esp_timer_get_time();
  s_vp_state.test_mode = false;
  s_vp_state.test_voltage = 0.0f;
  s_vp_state.initialized = true;
  ESP_LOGI(TAG, "Voltage protection initialized (Low: %.1fV, Recovery: %.1fV)",
           s_vp_state.config.low_voltage_threshold,
           s_vp_state.config.recovery_voltage_threshold);
  return ESP_OK;
}

esp_err_t voltage_protection_deinit(void) {
  if (!s_vp_state.initialized)
    return ESP_ERR_INVALID_STATE;
  if (s_vp_state.running)
    voltage_protection_stop();
  if (s_vp_state.state_mutex) {
    vSemaphoreDelete(s_vp_state.state_mutex);
    s_vp_state.state_mutex = NULL;
  }
  memset(&s_vp_state, 0, sizeof(voltage_protection_state_t));
  return ESP_OK;
}

esp_err_t voltage_protection_start(void) {
  if (!s_vp_state.initialized)
    return ESP_ERR_INVALID_STATE;
  if (s_vp_state.running)
    return ESP_OK;

  // Set running flag BEFORE creating task to avoid race condition
  s_vp_state.running = true;

  BaseType_t ret = xTaskCreate(voltage_protection_task, "vp_monitor", 8192,
                               NULL, 5, &s_vp_state.monitor_task_handle);
  if (ret != pdPASS) {
    s_vp_state.running = false; // Rollback on failure
    ESP_LOGE(TAG, "Failed to create voltage protection task");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Voltage protection monitoring started");
  return ESP_OK;
}

esp_err_t voltage_protection_stop(void) {
  if (!s_vp_state.initialized || !s_vp_state.running)
    return ESP_OK;
  s_vp_state.running = false;
  if (s_vp_state.monitor_task_handle) {
    vTaskDelete(s_vp_state.monitor_task_handle);
    s_vp_state.monitor_task_handle = NULL;
  }
  return ESP_OK;
}

esp_err_t voltage_protection_get_status(voltage_protection_status_t *status) {
  if (!s_vp_state.initialized || status == NULL)
    return ESP_ERR_INVALID_ARG;
  if (xSemaphoreTake(s_vp_state.state_mutex, pdMS_TO_TICKS(1000))) {
    status->initialized = s_vp_state.initialized;
    status->running = s_vp_state.running;
    status->state = s_vp_state.state;
    status->current_voltage = s_vp_state.current_voltage;
    status->countdown_remaining_sec = s_vp_state.countdown_remaining_sec;
    status->recovery_timer_sec = s_vp_state.recovery_timer_sec;
    status->protection_count = s_vp_state.protection_count;
    status->uptime_ms =
        (esp_timer_get_time() - s_vp_state.start_time_us) / 1000;
    status->device_status.agx_powered = s_vp_state.agx_powered;
    status->device_status.lpmu_powered = s_vp_state.lpmu_powered;
    status->device_status.agx_connected = s_vp_state.agx_websocket_connected;
    uint64_t now_us = esp_timer_get_time();
    status->device_status.agx_disconnect_time_sec =
        s_vp_state.agx_last_connected_us > 0
            ? (now_us - s_vp_state.agx_last_connected_us) / 1000000
            : 0;
    xSemaphoreGive(s_vp_state.state_mutex);
    return ESP_OK;
  }
  return ESP_FAIL;
}

bool voltage_protection_is_initialized(void) { return s_vp_state.initialized; }

bool voltage_protection_is_running(void) { return s_vp_state.running; }

esp_err_t voltage_protection_trigger_test(void) {
  if (!s_vp_state.initialized) {
    ESP_LOGE(TAG, "Cannot trigger test: not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  if (!s_vp_state.running) {
    ESP_LOGE(TAG, "Cannot trigger test: monitoring task not running");
    return ESP_ERR_INVALID_STATE;
  }
  if (xSemaphoreTake(s_vp_state.state_mutex, pdMS_TO_TICKS(1000))) {
    s_vp_state.test_mode = true;
    s_vp_state.test_voltage = s_vp_state.config.low_voltage_threshold - 0.5f;
    ESP_LOGW(TAG, "Test mode activated: simulating %.2fV (threshold: %.2fV)",
             s_vp_state.test_voltage, s_vp_state.config.low_voltage_threshold);
    ESP_LOGW(TAG, "Current state: %s, waiting for state change...",
             voltage_protection_get_state_name(s_vp_state.state));
    xSemaphoreGive(s_vp_state.state_mutex);
    return ESP_OK;
  } else {
    ESP_LOGE(TAG, "Failed to acquire mutex for test trigger");
    return ESP_FAIL;
  }
}

esp_err_t voltage_protection_reset(void) {
  if (!s_vp_state.initialized)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_vp_state.state_mutex, pdMS_TO_TICKS(1000))) {
    ESP_LOGI(TAG, "Protection reset requested - will restart ESP32-S3");

    // Disable test mode and reset state BEFORE restart
    s_vp_state.test_mode = false;
    s_vp_state.state = VOLTAGE_PROTECTION_STATE_NORMAL;
    s_vp_state.countdown_remaining_sec = 0;
    s_vp_state.recovery_timer_sec = 0;

    xSemaphoreGive(s_vp_state.state_mutex);

    ESP_LOGI(TAG, "Test mode disabled, restarting system");
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow log to flush
    esp_restart();

    return ESP_OK; // Never reached
  }
  return ESP_FAIL;
}

const char *
voltage_protection_get_state_name(voltage_protection_state_t state) {
  switch (state) {
  case VOLTAGE_PROTECTION_STATE_NORMAL:
    return "正常运行";
  case VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE:
    return "低电压保护";
  case VOLTAGE_PROTECTION_STATE_SHUTDOWN:
    return "关机中";
  case VOLTAGE_PROTECTION_STATE_RECOVERY:
    return "电压恢复中";
  case VOLTAGE_PROTECTION_STATE_PROTECTED:
    return "保护状态";
  default:
    return "未知";
  }
}

static void voltage_protection_task(void *pvParameters) {
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t check_period = pdMS_TO_TICKS(1000);
  static uint32_t voltage_read_fail_count = 0;
  uint32_t loop_count = 0;

  ESP_LOGE(TAG, ">>> TASK ENTRY: Voltage protection task entered <<<");
  ESP_LOGE(TAG, ">>> s_vp_state.running = %d <<<", s_vp_state.running);

  if (!s_vp_state.running) {
    ESP_LOGE(TAG, ">>> FATAL: Task started but running flag is FALSE! <<<");
    vTaskDelete(NULL);
    return;
  }

  while (s_vp_state.running) {
    loop_count++;

    if (loop_count == 1) {
      ESP_LOGE(TAG, ">>> FIRST LOOP ITERATION <<<");
    }
    if (loop_count <= 3) {
      ESP_LOGW(TAG, ">>> Loop iteration %lu <<<", (unsigned long)loop_count);
    }

    // Read test mode status with mutex protection
    bool test_mode_active = false;
    float test_voltage_value = 0.0f;
    if (xSemaphoreTake(s_vp_state.state_mutex, pdMS_TO_TICKS(10))) {
      test_mode_active = s_vp_state.test_mode;
      test_voltage_value = s_vp_state.test_voltage;
      xSemaphoreGive(s_vp_state.state_mutex);
    }

    // Force log every 10 seconds in test mode
    if (test_mode_active && (loop_count % 10 == 1)) {
      ESP_LOGW(TAG, ">>> Task loop %lu: test_mode=%d, test_voltage=%.2fV <<<",
               (unsigned long)loop_count, test_mode_active, test_voltage_value);
    }

    float voltage_to_check;
    bool voltage_valid = false;

    if (test_mode_active) {
      voltage_to_check = test_voltage_value;
      voltage_valid = true;
      static uint32_t test_loop_count = 0;
      test_loop_count++;
      // Only log first 3 times and then every 30 seconds to avoid log spam
      if (test_loop_count <= 3 || test_loop_count % 30 == 0) {
        ESP_LOGW(TAG, "Test mode loop #%lu: checking voltage %.2fV",
                 (unsigned long)test_loop_count, voltage_to_check);
      }
    } else {
      voltage_monitor_data_t voltage_data;
      esp_err_t ret = power_monitor_get_voltage_data(&voltage_data);
      if (ret == ESP_OK) {
        voltage_to_check = voltage_data.supply_voltage;
        // Ignore invalid voltage readings (0V or unreasonably low < 5V)
        // This happens during startup before power_monitor stabilizes
        if (voltage_to_check > 5.0f) {
          voltage_valid = true;
          if (voltage_read_fail_count > 0) {
            ESP_LOGI(TAG, "Voltage reading recovered: %.2fV", voltage_to_check);
            voltage_read_fail_count = 0;
          }
        } else {
          voltage_to_check = 0.0f;
          voltage_valid = false;
          voltage_read_fail_count++;
          if (voltage_read_fail_count == 1 ||
              voltage_read_fail_count % 10 == 0) {
            ESP_LOGW(TAG,
                     "Ignoring invalid voltage reading: %.2fV (waiting for "
                     "stabilization, count: %lu)",
                     voltage_to_check, (unsigned long)voltage_read_fail_count);
          }
        }
      } else {
        voltage_to_check = 0.0f;
        voltage_valid = false;
        voltage_read_fail_count++;
        if (voltage_read_fail_count == 1 || voltage_read_fail_count % 10 == 0) {
          ESP_LOGW(
              TAG, "Failed to read voltage from power_monitor (count: %lu): %s",
              (unsigned long)voltage_read_fail_count, esp_err_to_name(ret));
        }
      }
    }

    if (voltage_valid) {
      voltage_protection_check_voltage(voltage_to_check);
    }

    voltage_protection_check_device_status();

    if (xSemaphoreTake(s_vp_state.state_mutex, pdMS_TO_TICKS(100))) {
      switch (s_vp_state.state) {
      case VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE:
        if (s_vp_state.countdown_remaining_sec > 0) {
          s_vp_state.countdown_remaining_sec--;
          if (s_vp_state.countdown_remaining_sec % 10 == 0 ||
              s_vp_state.countdown_remaining_sec <= 5) {
            ESP_LOGW(TAG, "Low voltage countdown: %lus remaining",
                     (unsigned long)s_vp_state.countdown_remaining_sec);
          }
          if (s_vp_state.countdown_remaining_sec == 0) {
            ESP_LOGW(TAG, "Countdown complete, initiating shutdown");
            s_vp_state.state = VOLTAGE_PROTECTION_STATE_SHUTDOWN;
          }
        }
        break;
      case VOLTAGE_PROTECTION_STATE_RECOVERY:
        if (s_vp_state.recovery_timer_sec > 0) {
          s_vp_state.recovery_timer_sec--;
          ESP_LOGI(TAG, "Recovery timer: %lus remaining",
                   (unsigned long)s_vp_state.recovery_timer_sec);
          if (s_vp_state.recovery_timer_sec == 0) {
            ESP_LOGI(TAG, "Recovery confirmed, restarting devices");
            voltage_protection_execute_recovery();
            s_vp_state.state = VOLTAGE_PROTECTION_STATE_NORMAL;
          }
        }
        break;
      case VOLTAGE_PROTECTION_STATE_SHUTDOWN:
        voltage_protection_execute_shutdown();
        s_vp_state.state = VOLTAGE_PROTECTION_STATE_PROTECTED;
        ESP_LOGW(TAG, "Entered protected state");
        break;
      case VOLTAGE_PROTECTION_STATE_PROTECTED:
        // Handle fan shutdown timer
        if (!s_vp_state.fans_stopped && s_vp_state.shutdown_timer_sec > 0) {
          s_vp_state.shutdown_timer_sec--;
          if (s_vp_state.shutdown_timer_sec % 10 == 0 ||
              s_vp_state.shutdown_timer_sec <= 5) {
            ESP_LOGI(TAG, "Fan shutdown countdown: %lus remaining",
                     (unsigned long)s_vp_state.shutdown_timer_sec);
          }
          if (s_vp_state.shutdown_timer_sec == 0) {
            ESP_LOGW(TAG, "Stopping all fans (devices confirmed off)");
            for (uint8_t fan_id = 0; fan_id < 4; fan_id++) {
              fan_controller_enable(fan_id, false);
            }
            s_vp_state.fans_stopped = true;

            // Turn off Touch LED after fans stopped
            if (s_vp_state.config.enable_touch_led_feedback) {
              ESP_LOGI(TAG, "Turning off Touch LED (system fully shutdown)");
              touch_led_stop_animation();
              touch_led_clear();
              touch_led_update();
            }

            // Turn off Board LED
            ESP_LOGI(TAG, "Turning off Board LED (system fully shutdown)");
            board_led_stop_animation();
            board_led_clear();
          }
        }
        break;
      default:
        break;
      }
      xSemaphoreGive(s_vp_state.state_mutex);
    }

    voltage_protection_update_led_status();
    vTaskDelayUntil(&last_wake_time, check_period);
  }
  vTaskDelete(NULL);
}

static void voltage_protection_check_voltage(float voltage) {
  if (!s_vp_state.initialized) {
    ESP_LOGW(TAG, "check_voltage called but not initialized");
    return;
  }
  if (!xSemaphoreTake(s_vp_state.state_mutex, pdMS_TO_TICKS(100))) {
    ESP_LOGW(TAG, "check_voltage: failed to take mutex");
    return;
  }
  s_vp_state.current_voltage = voltage;
  ESP_LOGD(TAG, "check_voltage: voltage=%.2fV, state=%s", voltage,
           voltage_protection_get_state_name(s_vp_state.state));

  switch (s_vp_state.state) {
  case VOLTAGE_PROTECTION_STATE_NORMAL:
    if (voltage < s_vp_state.config.low_voltage_threshold) {
      ESP_LOGW(TAG, "[STATE CHANGE] NORMAL -> LOW_VOLTAGE: %.2fV < %.2fV",
               voltage, s_vp_state.config.low_voltage_threshold);
      s_vp_state.state = VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE;
      s_vp_state.countdown_remaining_sec = s_vp_state.config.shutdown_delay_sec;
      s_vp_state.protection_count++;
      ESP_LOGW(TAG, "Starting %lus shutdown countdown",
               (unsigned long)s_vp_state.countdown_remaining_sec);
    }
    break;
  case VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE:
    if (voltage >= s_vp_state.config.recovery_voltage_threshold) {
      // Voltage recovered during countdown - cancel shutdown and return to
      // normal
      ESP_LOGI(TAG,
               "[STATE CHANGE] LOW_VOLTAGE -> NORMAL: %.2fV >= %.2fV "
               "(countdown canceled)",
               voltage, s_vp_state.config.recovery_voltage_threshold);
      s_vp_state.state = VOLTAGE_PROTECTION_STATE_NORMAL;
      s_vp_state.countdown_remaining_sec = 0;
    }
    break;
  case VOLTAGE_PROTECTION_STATE_PROTECTED:
    if (voltage >= s_vp_state.config.recovery_voltage_threshold) {
      // Voltage recovered after shutdown - need to restart to recover system
      ESP_LOGI(TAG,
               "[STATE CHANGE] PROTECTED -> RECOVERY: %.2fV >= %.2fV (will "
               "restart after hold)",
               voltage, s_vp_state.config.recovery_voltage_threshold);
      s_vp_state.state = VOLTAGE_PROTECTION_STATE_RECOVERY;
      s_vp_state.recovery_timer_sec = s_vp_state.config.recovery_hold_sec;
    }
    break;
  case VOLTAGE_PROTECTION_STATE_RECOVERY:
    if (voltage < s_vp_state.config.recovery_voltage_threshold) {
      s_vp_state.recovery_timer_sec = 0;
      s_vp_state.state = voltage < s_vp_state.config.low_voltage_threshold
                             ? VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE
                             : VOLTAGE_PROTECTION_STATE_NORMAL;
      if (s_vp_state.state == VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE) {
        s_vp_state.countdown_remaining_sec =
            s_vp_state.config.shutdown_delay_sec;
      }
    }
    break;
  default:
    break;
  }
  xSemaphoreGive(s_vp_state.state_mutex);
}

static void voltage_protection_check_device_status(void) {
  power_state_t state;
  if (device_controller_agx_get_power_state(&state) == ESP_OK)
    s_vp_state.agx_powered = (state == POWER_STATE_ON);
  if (device_controller_lpmu_get_power_state(&state) == ESP_OK)
    s_vp_state.lpmu_powered = (state == POWER_STATE_ON);

  if (agx_monitor_is_running()) {
    agx_monitor_status_info_t status;
    if (agx_monitor_get_status(&status) == ESP_OK) {
      bool was_connected = s_vp_state.agx_websocket_connected;
      s_vp_state.agx_websocket_connected =
          (status.connection_status == AGX_MONITOR_STATUS_CONNECTED);
      if (s_vp_state.agx_websocket_connected != was_connected)
        s_vp_state.agx_last_connected_us = esp_timer_get_time();
    }
  }
}

static void voltage_protection_execute_shutdown(void) {
  ESP_LOGW(TAG, "Executing protective shutdown");

  // Put devices into reset/off state during low voltage protection
  ESP_LOGI(TAG, "Asserting reset on devices");

  // AGX reset pin: pull HIGH to assert reset (keep in reset state)
  if (s_vp_state.agx_powered) {
    ESP_LOGI(TAG, "Asserting AGX reset (GPIO1 HIGH)");
    gpio_controller_set_output(1, GPIO_STATE_HIGH); // AGX_RESET_PIN
  }

  // TODO: Temporarily disabled W5500 and RTL8367 reset for troubleshooting
  // W5500 and RTL8367 not recovering properly after reset, investigating issue
  // W5500 reset pin: pull LOW to assert reset
  // gpio_controller_set_output(39, GPIO_STATE_LOW); // W5500_RST_GPIO
  // RTL8367 switch reset pin: pull HIGH to assert reset
  // gpio_controller_set_output(17, GPIO_STATE_HIGH); // RTL8367_RST
  ESP_LOGW(TAG, "W5500/RTL8367 reset temporarily disabled for troubleshooting");

  // LPMU shutdown: Verify device is actually running before shutdown
  if (s_vp_state.lpmu_powered) {
    ESP_LOGI(TAG,
             "LPMU reported as powered on, checking if actually running...");

    // Ping LPMU to confirm it's actually online
    bool lpmu_online = is_device_online("10.10.99.99");

    if (lpmu_online) {
      ESP_LOGI(TAG, "LPMU confirmed online via ping, executing shutdown");
      device_controller_lpmu_power_toggle();
    } else {
      ESP_LOGW(TAG, "LPMU not responding to ping, skipping shutdown (may "
                    "already be off)");
    }
  } else {
    ESP_LOGI(TAG, "LPMU already powered off, skipping shutdown");
  }
  if (s_vp_state.config.enable_matrix_led_control) {
    matrix_led_clear();
    matrix_led_refresh();
  }
  // Start shutdown timer for fan control (will stop fans after 60s)
  s_vp_state.shutdown_timer_sec = 60;
  s_vp_state.fans_stopped = false;
  ESP_LOGI(TAG, "Shutdown timer started: will stop fans in 60s");
}

static void voltage_protection_execute_recovery(void) {
  ESP_LOGI(TAG, "Voltage recovered - restarting ESP32-S3 to restore system");

  // Give time for log to be flushed
  vTaskDelay(pdMS_TO_TICKS(100));

  // Restart ESP32-S3
  esp_restart();
}

static void voltage_protection_update_led_status(void) {
  if (!s_vp_state.config.enable_touch_led_feedback)
    return;
  static voltage_protection_state_t last_state =
      VOLTAGE_PROTECTION_STATE_NORMAL;
  voltage_protection_state_t current_state;
  if (xSemaphoreTake(s_vp_state.state_mutex, pdMS_TO_TICKS(10))) {
    current_state = s_vp_state.state;
    xSemaphoreGive(s_vp_state.state_mutex);
  } else {
    return;
  }

  if (current_state != last_state) {
    ESP_LOGI(TAG, "LED state change: %s -> %s",
             voltage_protection_get_state_name(last_state),
             voltage_protection_get_state_name(current_state));
    switch (current_state) {
    case VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE:
    case VOLTAGE_PROTECTION_STATE_SHUTDOWN:
    case VOLTAGE_PROTECTION_STATE_PROTECTED:
      ESP_LOGI(TAG, "Starting Touch LED orange breathe animation");
      touch_led_start_animation(TOUCH_LED_ANIM_BREATHE, 128,
                                TOUCH_LED_COLOR_ORANGE, TOUCH_LED_COLOR_OFF);
      break;
    case VOLTAGE_PROTECTION_STATE_NORMAL:
    case VOLTAGE_PROTECTION_STATE_RECOVERY:
      ESP_LOGI(TAG, "Restoring Touch LED to white");
      touch_led_stop_animation();
      touch_led_set_all_color(TOUCH_LED_COLOR_WHITE);
      touch_led_update();
      break;
    default:
      break;
    }
    last_state = current_state;
  }
}

static int cmd_voltprot_status(int argc, char **argv) {
  voltage_protection_status_t status;
  if (voltage_protection_get_status(&status) != ESP_OK) {
    printf("Failed to get status\n");
    return 1;
  }

  printf("\n=== Voltage Protection Status ===\n");
  printf("State: %s\n", voltage_protection_get_state_name(status.state));
  printf("Voltage: %.2fV", status.current_voltage);
  if (status.current_voltage < 0.1f) {
    printf(" [WARNING: No voltage data - check power_monitor]\n");
  } else {
    printf("\n");
  }
  printf("Protection Count: %lu\n", (unsigned long)status.protection_count);
  if (status.state == VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE)
    printf("Countdown: %lus\n", (unsigned long)status.countdown_remaining_sec);
  printf("\nDevice Status (tracked by voltage_protection):\n");
  printf("AGX: %s (%s)\n", status.device_status.agx_powered ? "ON" : "OFF",
         status.device_status.agx_connected ? "Connected" : "Disconnected");
  printf("LPMU: %s\n", status.device_status.lpmu_powered ? "ON" : "OFF");
  printf(
      "\nNOTE: Device status shows last known state from device_controller.\n");
  printf("      If devices were powered on before ESP32 boot, they may show as "
         "OFF.\n");
  printf("      Use 'agx status' or 'lpmu status' for device_controller's "
         "view.\n");
  return 0;
}

static int cmd_voltprot(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: voltprot status|test|reset|debug|ledtest\n");
    return 1;
  }
  if (strcmp(argv[1], "status") == 0)
    return cmd_voltprot_status(argc - 1, argv + 1);
  if (strcmp(argv[1], "test") == 0) {
    voltage_protection_trigger_test();
    printf("Test triggered - watch for Touch LED orange pulse\n");
    printf("Wait 2 seconds and run 'voltprot status' to check state change\n");
    printf("Expected: State should change to '低电压保护' with countdown\n");
    return 0;
  }
  if (strcmp(argv[1], "reset") == 0) {
    voltage_protection_reset();
    printf("State reset\n");
    return 0;
  }
  if (strcmp(argv[1], "ledtest") == 0) {
    printf("Testing Touch LED directly...\n");
    printf("Starting orange breathe animation...\n");
    touch_led_start_animation(TOUCH_LED_ANIM_BREATHE, 128,
                              TOUCH_LED_COLOR_ORANGE, TOUCH_LED_COLOR_OFF);
    printf("Orange breathe started. Run 'voltprot reset' to stop.\n");
    return 0;
  }
  if (strcmp(argv[1], "debug") == 0) {
    printf("\n=== Voltage Protection Debug ===\n");
    printf("Initialized: %s\n", s_vp_state.initialized ? "YES" : "NO");
    printf("Running flag: %s (value=%d)\n", s_vp_state.running ? "YES" : "NO",
           s_vp_state.running);
    printf("Monitor Task Handle: %p\n", (void *)s_vp_state.monitor_task_handle);
    printf("Test Mode: %s\n", s_vp_state.test_mode ? "ENABLED" : "DISABLED");
    if (s_vp_state.test_mode) {
      printf("Test Voltage: %.2fV\n", s_vp_state.test_voltage);
    }
    printf("Current Voltage (cached): %.2fV\n", s_vp_state.current_voltage);
    printf("Config: Low=%.1fV, Recovery=%.1fV, Delay=%lus\n",
           s_vp_state.config.low_voltage_threshold,
           s_vp_state.config.recovery_voltage_threshold,
           (unsigned long)s_vp_state.config.shutdown_delay_sec);
    printf("Touch LED feedback: %s\n",
           s_vp_state.config.enable_touch_led_feedback ? "ENABLED"
                                                       : "DISABLED");

    // Test power_monitor
    voltage_monitor_data_t voltage_data;
    esp_err_t ret = power_monitor_get_voltage_data(&voltage_data);
    printf("\nPower Monitor Test:\n");
    printf("  Result: %s\n", esp_err_to_name(ret));
    if (ret == ESP_OK) {
      printf("  Supply Voltage: %.2fV\n", voltage_data.supply_voltage);
    }

    // Check FreeRTOS task status
    printf("\nTask Status:\n");
    if (s_vp_state.monitor_task_handle != NULL) {
      eTaskState task_state = eTaskGetState(s_vp_state.monitor_task_handle);
      const char *state_names[] = {"Running",   "Ready",   "Blocked",
                                   "Suspended", "Deleted", "Invalid"};
      printf("  Monitor Task State: %s\n", state_names[task_state]);
    } else {
      printf("  Monitor Task: NOT CREATED\n");
    }

    return 0;
  }
  printf("Unknown command\n");
  return 1;
}

esp_err_t voltage_protection_register_console_commands(void) {
  const console_cmd_t cmd = {
      .command = "voltprot",
      .help = "Voltage protection: voltprot status|test|reset",
      .hint = NULL,
      .func = cmd_voltprot,
      .min_args = 0,
      .max_args = 2};
  return console_register_command(&cmd);
}
