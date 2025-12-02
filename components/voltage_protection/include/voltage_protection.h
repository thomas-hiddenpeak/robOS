/**
 * @file voltage_protection.h
 * @brief Voltage Protection Component for robOS
 *
 * @version 1.0.0
 * @date 2025-11-30
 */

#ifndef VOLTAGE_PROTECTION_H
#define VOLTAGE_PROTECTION_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOLTAGE_PROTECTION_VERSION "1.0.0"
#define VOLTAGE_PROTECTION_LOW_THRESHOLD 12.6f
#define VOLTAGE_PROTECTION_RECOVERY_THRESHOLD 18.0f
#define VOLTAGE_PROTECTION_SHUTDOWN_DELAY_SEC 60
#define VOLTAGE_PROTECTION_RECOVERY_HOLD_SEC 5
#define VOLTAGE_PROTECTION_WEBSOCKET_TIMEOUT_SEC 60

typedef enum {
  VOLTAGE_PROTECTION_STATE_NORMAL = 0,
  VOLTAGE_PROTECTION_STATE_LOW_VOLTAGE,
  VOLTAGE_PROTECTION_STATE_SHUTDOWN,
  VOLTAGE_PROTECTION_STATE_RECOVERY,
  VOLTAGE_PROTECTION_STATE_PROTECTED
} voltage_protection_state_t;

typedef struct {
  bool agx_powered;
  bool lpmu_powered;
  bool agx_connected;
  bool lpmu_connected;
  uint32_t agx_disconnect_time_sec;
  uint32_t lpmu_disconnect_time_sec;
} voltage_protection_device_status_t;

typedef struct {
  float low_voltage_threshold;
  float recovery_voltage_threshold;
  uint32_t shutdown_delay_sec;
  uint32_t recovery_hold_sec;
  uint32_t websocket_timeout_sec;
  bool auto_recovery_enabled;
  bool enable_touch_led_feedback;
  bool enable_matrix_led_control;
} voltage_protection_config_t;

typedef struct {
  bool initialized;
  bool running;
  voltage_protection_state_t state;
  float current_voltage;
  uint32_t countdown_remaining_sec;
  uint32_t recovery_timer_sec;
  voltage_protection_device_status_t device_status;
  uint32_t protection_count;
  uint64_t uptime_ms;
} voltage_protection_status_t;

esp_err_t
voltage_protection_get_default_config(voltage_protection_config_t *config);
esp_err_t voltage_protection_init(const voltage_protection_config_t *config);
esp_err_t voltage_protection_deinit(void);
esp_err_t voltage_protection_start(void);
esp_err_t voltage_protection_stop(void);
esp_err_t voltage_protection_get_status(voltage_protection_status_t *status);
bool voltage_protection_is_initialized(void);
bool voltage_protection_is_running(void);
esp_err_t voltage_protection_trigger_test(void);
esp_err_t voltage_protection_reset(void);
esp_err_t voltage_protection_register_console_commands(void);
const char *voltage_protection_get_state_name(voltage_protection_state_t state);

#ifdef __cplusplus
}
#endif

#endif // VOLTAGE_PROTECTION_H
