/**
 * @file storage_manager.c
 * @brief robOS 存储管理器实现
 * 
 * 实现异步的、事件驱动的TF卡存储管理功能，提供完整的
 * 文件系统操作接口和设备管理能力。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#include "storage_manager.h"
#include "storage_device.h"
#include "storage_fs.h"
#include "storage_console.h"
#include "event_manager.h"
#include "hardware_hal.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

/* ================================ 常量和宏定义 ================================ */

static const char* TAG = "storage_manager";

/* ================================ 事件基定义 ================================ */

ESP_EVENT_DEFINE_BASE(STORAGE_EVENTS);

/* ================================ 内部数据结构 ================================ */

/**
 * @brief 存储管理器上下文
 */
typedef struct {
    bool initialized;                           /**< 初始化状态 */
    bool running;                              /**< 运行状态 */
    storage_state_t state;                     /**< 当前状态 */
    storage_config_t config;                   /**< 配置信息 */
    
    TaskHandle_t task_handle;                  /**< 任务句柄 */
    QueueHandle_t operation_queue;             /**< 操作队列 */
    SemaphoreHandle_t mutex;                   /**< 互斥锁 */
    
    uint32_t next_request_id;                  /**< 下一个请求ID */
    uint32_t total_operations;                 /**< 总操作数 */
    uint32_t successful_operations;            /**< 成功操作数 */
    uint32_t failed_operations;                /**< 失败操作数 */
    
    esp_timer_handle_t monitor_timer;          /**< 监控定时器 */
} storage_manager_context_t;

/* ================================ 全局变量 ================================ */

static storage_manager_context_t s_storage_ctx = {0};

/* ================================ 内部函数声明 ================================ */

static void storage_manager_task(void* pvParameters);
static esp_err_t storage_manager_process_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_post_event(storage_event_type_t event_type, void* data, size_t data_size);
static void storage_manager_monitor_callback(void* arg);
static esp_err_t storage_manager_validate_config(const storage_config_t* config);

// 操作处理函数
static esp_err_t storage_manager_process_mount_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_unmount_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_format_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_read_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_write_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_append_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_delete_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_copy_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_move_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_mkdir_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_rmdir_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_list_dir_operation(const storage_operation_request_t* request);
static esp_err_t storage_manager_process_stat_operation(const storage_operation_request_t* request);

/* ================================ API 实现 ================================ */

esp_err_t storage_manager_init(const storage_config_t* config)
{
    if (s_storage_ctx.initialized) {
        ESP_LOGW(TAG, "Storage manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid config parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 验证配置参数
    esp_err_t ret = storage_manager_validate_config(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid configuration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Initializing storage manager...");
    
    // 初始化上下文
    memset(&s_storage_ctx, 0, sizeof(s_storage_ctx));
    memcpy(&s_storage_ctx.config, config, sizeof(storage_config_t));
    s_storage_ctx.state = STORAGE_STATE_UNINITIALIZED;
    s_storage_ctx.next_request_id = 1;
    s_storage_ctx.running = true;  // 在创建任务之前设置为true
    
    // 创建互斥锁
    s_storage_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_storage_ctx.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建操作队列
    s_storage_ctx.operation_queue = xQueueCreate(STORAGE_MANAGER_QUEUE_SIZE, 
                                                 sizeof(storage_operation_request_t));
    if (!s_storage_ctx.operation_queue) {
        ESP_LOGE(TAG, "Failed to create operation queue");
        vSemaphoreDelete(s_storage_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化存储设备
    storage_device_config_t device_config;
    storage_device_get_default_config(&device_config);
    
    ret = storage_device_init(&device_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize storage device: %s", esp_err_to_name(ret));
        vQueueDelete(s_storage_ctx.operation_queue);
        vSemaphoreDelete(s_storage_ctx.mutex);
        return ret;
    }
    
    // 创建存储管理任务
    BaseType_t task_ret = xTaskCreate(
        storage_manager_task,
        "storage_mgr",
        STORAGE_MANAGER_TASK_STACK_SIZE,
        NULL,
        STORAGE_MANAGER_TASK_PRIORITY,
        &s_storage_ctx.task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create storage manager task");
        storage_device_deinit();
        vQueueDelete(s_storage_ctx.operation_queue);
        vSemaphoreDelete(s_storage_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // 创建监控定时器
    const esp_timer_create_args_t timer_args = {
        .callback = storage_manager_monitor_callback,
        .arg = NULL,
        .name = "storage_monitor"
    };
    
    ret = esp_timer_create(&timer_args, &s_storage_ctx.monitor_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create monitor timer: %s", esp_err_to_name(ret));
        vTaskDelete(s_storage_ctx.task_handle);
        storage_device_deinit();
        vQueueDelete(s_storage_ctx.operation_queue);
        vSemaphoreDelete(s_storage_ctx.mutex);
        return ret;
    }
    
    // 启动监控定时器（每5秒检查一次）
    ret = esp_timer_start_periodic(s_storage_ctx.monitor_timer, 5000000); // 5秒
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start monitor timer: %s", esp_err_to_name(ret));
    }
    
    s_storage_ctx.initialized = true;
    s_storage_ctx.state = STORAGE_STATE_INITIALIZED;
    
    ESP_LOGI(TAG, "Storage manager initialized successfully");
    
    // 发布初始化完成事件
    storage_manager_post_event(STORAGE_EVENT_MOUNTED, NULL, 0);
    
    return ESP_OK;
}

esp_err_t storage_manager_deinit(void)
{
    if (!s_storage_ctx.initialized) {
        ESP_LOGW(TAG, "Storage manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing storage manager...");
    
    // 停止运行
    s_storage_ctx.running = false;
    
    // 停止并删除监控定时器
    if (s_storage_ctx.monitor_timer) {
        esp_timer_stop(s_storage_ctx.monitor_timer);
        esp_timer_delete(s_storage_ctx.monitor_timer);
        s_storage_ctx.monitor_timer = NULL;
    }
    
    // 等待任务结束
    if (s_storage_ctx.task_handle) {
        vTaskDelete(s_storage_ctx.task_handle);
        s_storage_ctx.task_handle = NULL;
    }
    
    // 反初始化存储设备
    storage_device_deinit();
    
    // 清理资源
    if (s_storage_ctx.operation_queue) {
        vQueueDelete(s_storage_ctx.operation_queue);
        s_storage_ctx.operation_queue = NULL;
    }
    
    if (s_storage_ctx.mutex) {
        vSemaphoreDelete(s_storage_ctx.mutex);
        s_storage_ctx.mutex = NULL;
    }
    
    s_storage_ctx.initialized = false;
    s_storage_ctx.state = STORAGE_STATE_UNINITIALIZED;
    
    ESP_LOGI(TAG, "Storage manager deinitialized");
    
    return ESP_OK;
}

bool storage_manager_is_initialized(void)
{
    return s_storage_ctx.initialized;
}

storage_state_t storage_manager_get_state(void)
{
    return s_storage_ctx.state;
}

esp_err_t storage_manager_get_stats(storage_stats_t* stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized || s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 获取设备容量信息
    uint64_t total_bytes, free_bytes;
    esp_err_t ret = storage_device_get_capacity(&total_bytes, &free_bytes);
    if (ret != ESP_OK) {
        return ret;
    }
    
    stats->total_bytes = total_bytes;
    stats->free_bytes = free_bytes;
    stats->used_bytes = total_bytes - free_bytes;
    
    // 计算文件和目录数量（这里简化处理，实际应该递归统计）
    stats->total_files = 0;
    stats->total_directories = 0;
    
    return ESP_OK;
}

void storage_manager_get_default_config(storage_config_t* config)
{
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(storage_config_t));
    
    strcpy(config->mount_point, STORAGE_MANAGER_DEFAULT_MOUNT_POINT);
    config->format_if_mount_failed = false;
    config->enable_hot_swap = true;
    config->max_files = 1000;
    config->allocation_unit_size = 4096;
    config->enable_cache = true;
    config->cache_size = 8192;
}

const char* storage_manager_error_to_string(esp_err_t error)
{
    switch (error) {
        case ESP_OK:
            return "Success";
        case ESP_ERR_INVALID_ARG:
            return "Invalid argument";
        case ESP_ERR_INVALID_STATE:
            return "Invalid state";
        case ESP_ERR_NO_MEM:
            return "Out of memory";
        case ESP_ERR_NOT_FOUND:
            return "File not found";
        case ESP_ERR_TIMEOUT:
            return "Operation timeout";
        default:
            return esp_err_to_name(error);
    }
}

/* ================================ 设备管理 API 实现 ================================ */

esp_err_t storage_manager_mount_async(storage_callback_t callback, void* user_data)
{
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_MOUNT;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 10000; // 10秒超时
    
    // 设置挂载点路径
    strncpy(request.path1, s_storage_ctx.config.mount_point, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_unmount_async(storage_callback_t callback, void* user_data)
{
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_UNMOUNT;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 5000; // 5秒超时
    
    // 设置挂载点路径
    strncpy(request.path1, s_storage_ctx.config.mount_point, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_format_async(storage_callback_t callback, void* user_data)
{
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_FORMAT;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 30000; // 30秒超时
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_read_file_async(const char* path, 
                                          storage_callback_t callback, 
                                          void* user_data)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_READ;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 10000; // 10秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_write_file_async(const char* path, 
                                           const void* data, 
                                           size_t size,
                                           storage_callback_t callback, 
                                           void* user_data)
{
    if (!path || !data || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 为异步操作复制数据
    void* data_copy = malloc(size);
    if (!data_copy) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(data_copy, data, size);
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_WRITE;
    request.callback = callback;
    request.user_data = user_data;
    request.data = data_copy;
    request.data_size = size;
    request.timeout_ms = 15000; // 15秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(data_copy);
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_append_file_async(const char* path, 
                                            const void* data, 
                                            size_t size,
                                            storage_callback_t callback, 
                                            void* user_data)
{
    if (!path || !data || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 为异步操作复制数据
    void* data_copy = malloc(size);
    if (!data_copy) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(data_copy, data, size);
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_APPEND;
    request.callback = callback;
    request.user_data = user_data;
    request.data = data_copy;
    request.data_size = size;
    request.timeout_ms = 15000; // 15秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(data_copy);
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_delete_file_async(const char* path, 
                                            storage_callback_t callback, 
                                            void* user_data)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_DELETE;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 5000; // 5秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_list_dir_async(const char* path, 
                                         storage_callback_t callback, 
                                         void* user_data)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_LIST_DIR;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 10000; // 10秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_mkdir_async(const char* path, 
                                      storage_callback_t callback, 
                                      void* user_data)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_MKDIR;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 5000; // 5秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_rmdir_async(const char* path, 
                                      bool recursive,
                                      storage_callback_t callback, 
                                      void* user_data)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 为递归标志分配内存
    bool* recursive_flag = malloc(sizeof(bool));
    if (!recursive_flag) {
        return ESP_ERR_NO_MEM;
    }
    *recursive_flag = recursive;
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_RMDIR;
    request.callback = callback;
    request.user_data = user_data;
    request.data = recursive_flag;
    request.data_size = sizeof(bool);
    request.timeout_ms = 15000; // 15秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(recursive_flag);
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_copy_async(const char* src_path, 
                                     const char* dst_path,
                                     bool recursive,
                                     storage_callback_t callback, 
                                     void* user_data)
{
    if (!src_path || !dst_path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_COPY;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 30000; // 30秒超时
    
    strncpy(request.path1, src_path, sizeof(request.path1) - 1);
    strncpy(request.path2, dst_path, sizeof(request.path2) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_move_async(const char* src_path, 
                                     const char* dst_path,
                                     storage_callback_t callback, 
                                     void* user_data)
{
    if (!src_path || !dst_path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_MOVE;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 10000; // 10秒超时
    
    strncpy(request.path1, src_path, sizeof(request.path1) - 1);
    strncpy(request.path2, dst_path, sizeof(request.path2) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t storage_manager_stat_async(const char* path, 
                                     storage_callback_t callback, 
                                     void* user_data)
{
    if (!path || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_storage_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_operation_request_t request = {0};
    request.request_id = s_storage_ctx.next_request_id++;
    request.operation = STORAGE_OP_STAT;
    request.callback = callback;
    request.user_data = user_data;
    request.timeout_ms = 5000; // 5秒超时
    
    strncpy(request.path1, path, sizeof(request.path1) - 1);
    
    if (xQueueSend(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Operation queue full");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

void storage_manager_free_dir_list(storage_dir_list_t* dir_list)
{
    if (dir_list && dir_list->files) {
        free(dir_list->files);
        dir_list->files = NULL;
        dir_list->count = 0;
        dir_list->capacity = 0;
    }
}

/* ================================ 内部函数实现 ================================ */

static void storage_manager_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Storage manager task started");
    
    storage_operation_request_t request;
    
    while (s_storage_ctx.running) {
        // 等待操作请求
        if (xQueueReceive(s_storage_ctx.operation_queue, &request, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGD(TAG, "Processing operation %lu, type: %d", request.request_id, request.operation);
            
            // 处理操作
            esp_err_t result = storage_manager_process_operation(&request);
            
            // 更新统计信息
            s_storage_ctx.total_operations++;
            if (result == ESP_OK) {
                s_storage_ctx.successful_operations++;
            } else {
                s_storage_ctx.failed_operations++;
            }
            
            // 调用回调函数
            if (request.callback) {
                request.callback(request.operation, result, NULL, request.user_data);
            }
            
            // 释放请求中的数据内存
            if (request.data) {
                free(request.data);
            }
            
            // 发布操作完成事件
            if (result == ESP_OK) {
                storage_manager_post_event(STORAGE_EVENT_OPERATION_COMPLETE, &request.request_id, sizeof(uint32_t));
            } else {
                storage_manager_post_event(STORAGE_EVENT_OPERATION_FAILED, &request.request_id, sizeof(uint32_t));
            }
        }
    }
    
    ESP_LOGI(TAG, "Storage manager task ended");
    vTaskDelete(NULL);
}

static esp_err_t storage_manager_process_operation(const storage_operation_request_t* request)
{
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Processing operation: %d", request->operation);
    
    // 这里根据操作类型分发到具体的处理函数
    switch (request->operation) {
        case STORAGE_OP_MOUNT:
            return storage_manager_process_mount_operation(request);
            
        case STORAGE_OP_UNMOUNT:
            return storage_manager_process_unmount_operation(request);
            
        case STORAGE_OP_FORMAT:
            return storage_manager_process_format_operation(request);
            
        case STORAGE_OP_READ:
            return storage_manager_process_read_operation(request);
            
        case STORAGE_OP_WRITE:
            return storage_manager_process_write_operation(request);
            
        case STORAGE_OP_APPEND:
            return storage_manager_process_append_operation(request);
            
        case STORAGE_OP_DELETE:
            return storage_manager_process_delete_operation(request);
            
        case STORAGE_OP_COPY:
            return storage_manager_process_copy_operation(request);
            
        case STORAGE_OP_MOVE:
            return storage_manager_process_move_operation(request);
            
        case STORAGE_OP_MKDIR:
            return storage_manager_process_mkdir_operation(request);
            
        case STORAGE_OP_RMDIR:
            return storage_manager_process_rmdir_operation(request);
            
        case STORAGE_OP_LIST_DIR:
            return storage_manager_process_list_dir_operation(request);
            
        case STORAGE_OP_STAT:
            return storage_manager_process_stat_operation(request);
            
        default:
            ESP_LOGW(TAG, "Unsupported operation: %d", request->operation);
            return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t storage_manager_post_event(storage_event_type_t event_type, void* data, size_t data_size)
{
    if (!event_manager_is_initialized()) {
        ESP_LOGD(TAG, "Event manager not initialized, skipping event");
        return ESP_ERR_INVALID_STATE;
    }
    
    return event_manager_post_event(STORAGE_EVENTS, event_type, data, data_size, portMAX_DELAY);
}

static void storage_manager_monitor_callback(void* arg)
{
    if (!s_storage_ctx.initialized || !s_storage_ctx.running) {
        return;
    }
    
    // 只有在已挂载状态下才进行监控检查，避免无卡时的无用检测
    if (s_storage_ctx.state == STORAGE_STATE_MOUNTED) {
        // 检查卡片是否仍然存在
        if (!storage_device_is_card_present()) {
            ESP_LOGI(TAG, "Card removed");
            s_storage_ctx.state = STORAGE_STATE_UNMOUNTED;
            storage_manager_post_event(STORAGE_EVENT_CARD_REMOVED, NULL, 0);
            return;  // 卡片移除后不再进行其他检查
        }
        
        // 检查存储空间（仅在挂载状态下）
        storage_stats_t stats;
        if (storage_manager_get_stats(&stats) == ESP_OK) {
            // 如果可用空间少于10%，发出警告
            if (stats.free_bytes < (stats.total_bytes / 10)) {
                ESP_LOGW(TAG, "Low storage space: %llu/%llu bytes", stats.free_bytes, stats.total_bytes);
                storage_manager_post_event(STORAGE_EVENT_LOW_SPACE_WARNING, &stats, sizeof(stats));
            }
        }
    }
    // 在UNMOUNTED状态下不进行任何检测，避免无用的尝试
}

static esp_err_t storage_manager_validate_config(const storage_config_t* config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查挂载点路径
    if (strlen(config->mount_point) == 0 || strlen(config->mount_point) >= STORAGE_MANAGER_MAX_PATH_LENGTH) {
        ESP_LOGE(TAG, "Invalid mount point path");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查最大文件数
    if (config->max_files == 0 || config->max_files > 10000) {
        ESP_LOGE(TAG, "Invalid max files count: %lu", config->max_files);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查分配单元大小
    if (config->allocation_unit_size == 0 || 
        (config->allocation_unit_size & (config->allocation_unit_size - 1)) != 0) {
        ESP_LOGE(TAG, "Invalid allocation unit size: %lu", config->allocation_unit_size);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/* ================================ 操作处理函数实现 ================================ */

static esp_err_t storage_manager_process_mount_operation(const storage_operation_request_t* request)
{
    ESP_LOGI(TAG, "Processing mount operation to %s", request->path1);
    
    if (s_storage_ctx.state == STORAGE_STATE_MOUNTED) {
        ESP_LOGW(TAG, "Storage already mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = storage_device_mount(request->path1, s_storage_ctx.config.format_if_mount_failed);
    if (ret == ESP_OK) {
        s_storage_ctx.state = STORAGE_STATE_MOUNTED;
        storage_manager_post_event(STORAGE_EVENT_MOUNTED, NULL, 0);
        ESP_LOGI(TAG, "Storage mounted successfully");
    } else {
        s_storage_ctx.state = STORAGE_STATE_UNMOUNTED;  // 设置为UNMOUNTED，而不是ERROR
        
        // 根据错误类型决定事件类型
        if (ret == ESP_ERR_TIMEOUT) {
            // 没有卡的情况，不发送错误事件
            storage_manager_post_event(STORAGE_EVENT_UNMOUNTED, NULL, 0);
            ESP_LOGD(TAG, "Storage mount skipped - no card present");  // 降级为DEBUG级别
        } else {
            // 其他错误才发送错误事件
            storage_manager_post_event(STORAGE_EVENT_FILESYSTEM_ERROR, &ret, sizeof(ret));
            ESP_LOGW(TAG, "Storage mount failed: %s", esp_err_to_name(ret));  // 降级为WARNING级别
        }
    }
    
    return ret;
}

static esp_err_t storage_manager_process_unmount_operation(const storage_operation_request_t* request)
{
    ESP_LOGI(TAG, "Processing unmount operation");
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        ESP_LOGW(TAG, "Storage not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = storage_device_unmount(request->path1);
    if (ret == ESP_OK) {
        s_storage_ctx.state = STORAGE_STATE_UNMOUNTED;
        storage_manager_post_event(STORAGE_EVENT_UNMOUNTED, NULL, 0);
        ESP_LOGI(TAG, "Storage unmounted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to unmount storage: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t storage_manager_process_format_operation(const storage_operation_request_t* request)
{
    ESP_LOGI(TAG, "Processing format operation");
    
    esp_err_t ret = storage_device_format();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Storage formatted successfully");
    } else {
        ESP_LOGE(TAG, "Failed to format storage: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t storage_manager_process_read_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing read operation: %s", request->path1);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    void* data;
    size_t size;
    esp_err_t ret = storage_fs_read_file(request->path1, &data, &size);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File read successfully: %zu bytes", size);
        // 注意：这里需要在回调中释放data内存
    }
    
    return ret;
}

static esp_err_t storage_manager_process_write_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing write operation: %s (%zu bytes)", request->path1, request->data_size);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_fs_file_options_t options = {0};
    storage_fs_get_default_file_options(&options);
    
    esp_err_t ret = storage_fs_write_file(request->path1, request->data, request->data_size, &options);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File written successfully");
    }
    
    return ret;
}

static esp_err_t storage_manager_process_append_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing append operation: %s (%zu bytes)", request->path1, request->data_size);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = storage_fs_append_file(request->path1, request->data, request->data_size);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File appended successfully");
    }
    
    return ret;
}

static esp_err_t storage_manager_process_delete_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing delete operation: %s", request->path1);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = storage_fs_delete_file(request->path1);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File deleted successfully");
    }
    
    return ret;
}

static esp_err_t storage_manager_process_copy_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing copy operation: %s -> %s", request->path1, request->path2);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_fs_copy_options_t options = {0};
    storage_fs_get_default_copy_options(&options);
    options.overwrite_existing = true; // 默认允许覆盖
    
    esp_err_t ret = storage_fs_copy_file(request->path1, request->path2, &options);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File copied successfully");
    }
    
    return ret;
}

static esp_err_t storage_manager_process_move_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing move operation: %s -> %s", request->path1, request->path2);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = storage_fs_move_file(request->path1, request->path2);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File moved successfully");
    }
    
    return ret;
}

static esp_err_t storage_manager_process_mkdir_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing mkdir operation: %s", request->path1);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_fs_dir_options_t options = {0};
    storage_fs_get_default_dir_options(&options);
    options.create_parents = true; // 默认创建父目录
    
    esp_err_t ret = storage_fs_create_directory(request->path1, &options);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Directory created successfully");
    }
    
    return ret;
}

static esp_err_t storage_manager_process_rmdir_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing rmdir operation: %s", request->path1);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_fs_dir_options_t options = {0};
    storage_fs_get_default_dir_options(&options);
    // 根据请求数据判断是否递归删除
    if (request->data && *(bool*)request->data) {
        options.recursive = true;
    }
    
    esp_err_t ret = storage_fs_remove_directory(request->path1, &options);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Directory removed successfully");
    }
    
    return ret;
}

static esp_err_t storage_manager_process_list_dir_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing list_dir operation: %s", request->path1);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_dir_list_t dir_list;
    esp_err_t ret = storage_fs_list_directory(request->path1, &dir_list);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Directory listed successfully: %zu items", dir_list.count);
        // 注意：这里需要在回调中释放dir_list内存
    }
    
    return ret;
}

static esp_err_t storage_manager_process_stat_operation(const storage_operation_request_t* request)
{
    ESP_LOGD(TAG, "Processing stat operation: %s", request->path1);
    
    if (s_storage_ctx.state != STORAGE_STATE_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    storage_file_info_t file_info;
    esp_err_t ret = storage_fs_get_info(request->path1, &file_info);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "File info retrieved successfully");
    }
    
    return ret;
}

/* ================================ 控制台命令函数 ================================ */

esp_err_t storage_manager_register_console_commands(void)
{
    ESP_LOGI(TAG, "Registering storage console commands");
    
    esp_err_t ret = storage_console_register_commands();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register console commands: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Storage console commands registered successfully");
    return ESP_OK;
}

esp_err_t storage_manager_unregister_console_commands(void)
{
    ESP_LOGI(TAG, "Unregistering storage console commands");
    
    esp_err_t ret = storage_console_unregister_commands();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister console commands: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Storage console commands unregistered successfully");
    return ESP_OK;
}