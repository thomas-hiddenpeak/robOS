/**
 * @file web_server.c
 * @brief Simple Web Server Implementation for robOS
 *
 * Based on successful implementation from rm01-esp32s3-bsp project.
 * Simplified design that starts immediately when storage is available.
 *
 * @author robOS Team
 * @date 2025
 */

#include "web_server.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

/* Web root directory */
#define WEB_ROOT_PATH "/sdcard/web"
#define DEFAULT_FILE "index.htm"

/* MIME types for common file extensions */
typedef struct {
  const char *ext;
  const char *type;
} mime_map_t;

static const mime_map_t mime_map[] = {
    {".html", "text/html"},        {".htm", "text/html"},
    {".css", "text/css"},          {".js", "application/javascript"},
    {".json", "application/json"}, {".png", "image/png"},
    {".jpg", "image/jpeg"},        {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},         {".ico", "image/x-icon"},
    {".svg", "image/svg+xml"},     {".txt", "text/plain"}};

/**
 * @brief Get MIME type for file extension
 */
static const char *get_mime_type(const char *filename) {
  const char *ext = strrchr(filename, '.');
  if (!ext)
    return "application/octet-stream";

  for (int i = 0; i < sizeof(mime_map) / sizeof(mime_map[0]); i++) {
    if (strcmp(ext, mime_map[i].ext) == 0) {
      return mime_map[i].type;
    }
  }
  return "application/octet-stream";
}

/**
 * @brief Send file from filesystem
 */
static esp_err_t send_file(httpd_req_t *req, const char *filepath) {
  ESP_LOGI(TAG, "Attempting to open file: %s", filepath);
  FILE *file = fopen(filepath, "r");
  if (!file) {
    ESP_LOGW(TAG, "File not found: %s (errno: %d)", filepath, errno);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  // Set content type based on file extension
  const char *mime_type = get_mime_type(filepath);
  httpd_resp_set_type(req, mime_type);

  // Set CORS headers
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Send file in chunks
  char buffer[1024];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK) {
      fclose(file);
      return ESP_FAIL;
    }
  }

  // Finalize response
  httpd_resp_send_chunk(req, NULL, 0);
  fclose(file);

  ESP_LOGI(TAG, "Served file: %s", filepath);
  return ESP_OK;
}

/**
 * @brief Static file handler
 */
static esp_err_t static_file_handler(httpd_req_t *req) {
  char filepath[1024]; // 增加缓冲区大小以避免截断警告

  ESP_LOGI(TAG, "Request URI: %s", req->uri);

  // Build file path
  if (strcmp(req->uri, "/") == 0) {
    int ret = snprintf(filepath, sizeof(filepath), "%s/%s", WEB_ROOT_PATH,
                       DEFAULT_FILE);
    if (ret >= sizeof(filepath)) {
      ESP_LOGE(TAG, "Path too long");
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
  } else {
    int ret =
        snprintf(filepath, sizeof(filepath), "%s%s", WEB_ROOT_PATH, req->uri);
    if (ret >= sizeof(filepath)) {
      ESP_LOGE(TAG, "Path too long");
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
  }

  ESP_LOGI(TAG, "Trying to serve file: %s", filepath);

  // Security check - prevent directory traversal
  if (strstr(filepath, "..") != NULL) {
    ESP_LOGW(TAG, "Directory traversal attempt: %s", req->uri);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  return send_file(req, filepath);
}

/**
 * @brief Network status API handler
 */
static esp_err_t api_network_handler(httpd_req_t *req) {
  // Set response type
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  // Create JSON response
  cJSON *json = cJSON_CreateObject();
  cJSON *timestamp = cJSON_CreateNumber(time(NULL));
  cJSON *targets = cJSON_CreateArray();

  cJSON_AddItemToObject(json, "timestamp", timestamp);

  // Add mock network targets
  const char *names[] = {"AGX Xavier", "Gateway", "DNS Server"};
  const char *ips[] = {"10.10.99.1", "10.10.99.1", "8.8.8.8"};
  const char *status[] = {"UP", "UP", "UP"};
  const int response_times[] = {5, 2, 10};
  const float loss_rates[] = {0.0, 0.0, 0.0};

  for (int i = 0; i < 3; i++) {
    cJSON *target = cJSON_CreateObject();
    cJSON_AddStringToObject(target, "name", names[i]);
    cJSON_AddStringToObject(target, "ip", ips[i]);
    cJSON_AddStringToObject(target, "status", status[i]);
    cJSON_AddNumberToObject(target, "response_time", response_times[i]);
    cJSON_AddNumberToObject(target, "loss_rate", loss_rates[i]);
    cJSON_AddItemToArray(targets, target);
  }

  cJSON_AddItemToObject(json, "targets", targets);

  // Send response
  char *json_string = cJSON_Print(json);
  if (json_string) {
    httpd_resp_sendstr(req, json_string);
    free(json_string);
  } else {
    httpd_resp_send_500(req);
  }

  cJSON_Delete(json);
  return ESP_OK;
}

/**
 * @brief System status API handler
 */
static esp_err_t api_system_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  cJSON *json = cJSON_CreateObject();
  cJSON_AddStringToObject(json, "system", "robOS");
  cJSON_AddStringToObject(json, "version", "2.0.0");
  cJSON_AddStringToObject(json, "status", "running");
  cJSON_AddNumberToObject(json, "uptime", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(json, "free_heap", esp_get_free_heap_size());

  char *json_string = cJSON_Print(json);
  if (json_string) {
    httpd_resp_sendstr(req, json_string);
    free(json_string);
  } else {
    httpd_resp_send_500(req);
  }

  cJSON_Delete(json);
  return ESP_OK;
}

/**
 * @brief OPTIONS handler for CORS
 */
static esp_err_t options_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

/* ============================================================================
 * Public Functions
 * ============================================================================
 */

esp_err_t web_server_start(void) {
  if (server != NULL) {
    ESP_LOGW(TAG, "Web server already running");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Starting web server on port 80...");

  // Configure HTTP server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 16;
  config.max_resp_headers = 8;
  config.max_open_sockets = 7;
  config.stack_size = 8192;

  // CRITICAL: Enable wildcard URI matching
  config.uri_match_fn = httpd_uri_match_wildcard;

  // Start server
  esp_err_t ret = httpd_start(&server, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
    return ret;
  }

  // Register API handlers
  httpd_uri_t api_network_uri = {.uri = "/api/network",
                                 .method = HTTP_GET,
                                 .handler = api_network_handler,
                                 .user_ctx = NULL};
  httpd_register_uri_handler(server, &api_network_uri);

  httpd_uri_t api_system_uri = {.uri = "/api/system",
                                .method = HTTP_GET,
                                .handler = api_system_handler,
                                .user_ctx = NULL};
  httpd_register_uri_handler(server, &api_system_uri);

  // Register OPTIONS handler for CORS
  httpd_uri_t options_uri = {.uri = "/*",
                             .method = HTTP_OPTIONS,
                             .handler = options_handler,
                             .user_ctx = NULL};
  httpd_register_uri_handler(server, &options_uri);

  // Register static file handler (catch-all - must be last)
  httpd_uri_t static_uri = {.uri = "/*",
                            .method = HTTP_GET,
                            .handler = static_file_handler,
                            .user_ctx = NULL};
  httpd_register_uri_handler(server, &static_uri);

  ESP_LOGI(TAG, "Web server started successfully");
  ESP_LOGI(TAG, "Web interface: http://10.10.99.97/");
  ESP_LOGI(TAG, "API endpoints: /api/network, /api/system");

  return ESP_OK;
}

esp_err_t web_server_stop(void) {
  if (server == NULL) {
    ESP_LOGW(TAG, "Web server not running");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Stopping web server...");
  esp_err_t ret = httpd_stop(server);
  if (ret == ESP_OK) {
    server = NULL;
    ESP_LOGI(TAG, "Web server stopped");
  } else {
    ESP_LOGE(TAG, "Failed to stop web server: %s", esp_err_to_name(ret));
  }

  return ret;
}