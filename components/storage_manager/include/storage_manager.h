/**
 * @file storage_manager.h
 * @brief robOS 存储管理器 - TF卡文件系统管理
 * 
 * 提供异步的、事件驱动的TF卡存储管理功能，包括文件系统操作、
 * 设备管理和类Linux shell命令支持。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ 常量定义 ================================ */

#define STORAGE_MANAGER_TASK_STACK_SIZE     (8192)
#define STORAGE_MANAGER_TASK_PRIORITY       (5)
#define STORAGE_MANAGER_QUEUE_SIZE          (16)
#define STORAGE_MANAGER_MAX_PATH_LENGTH     (512)
#define STORAGE_MANAGER_MAX_FILENAME_LENGTH (255)
#define STORAGE_MANAGER_DEFAULT_MOUNT_POINT "/sdcard"

/* ================================ 事件定义 ================================ */

ESP_EVENT_DECLARE_BASE(STORAGE_EVENTS);

/**
 * @brief 存储管理器事件类型
 */
typedef enum {
    STORAGE_EVENT_MOUNTED,              /**< TF卡挂载成功 */
    STORAGE_EVENT_UNMOUNTED,            /**< TF卡卸载完成 */
    STORAGE_EVENT_CARD_INSERTED,        /**< 卡片插入检测 */
    STORAGE_EVENT_CARD_REMOVED,         /**< 卡片拔出检测 */
    STORAGE_EVENT_OPERATION_COMPLETE,   /**< 文件操作完成 */
    STORAGE_EVENT_OPERATION_FAILED,     /**< 文件操作失败 */
    STORAGE_EVENT_LOW_SPACE_WARNING,    /**< 存储空间不足警告 */
    STORAGE_EVENT_FILESYSTEM_ERROR,     /**< 文件系统错误 */
    STORAGE_EVENT_IO_ERROR              /**< IO错误 */
} storage_event_type_t;

/* ================================ 数据结构 ================================ */

/**
 * @brief 存储设备状态
 */
typedef enum {
    STORAGE_STATE_UNINITIALIZED = 0,   /**< 未初始化 */
    STORAGE_STATE_INITIALIZED,         /**< 已初始化 */
    STORAGE_STATE_MOUNTED,             /**< 已挂载 */
    STORAGE_STATE_UNMOUNTED,           /**< 已卸载 */
    STORAGE_STATE_ERROR                /**< 错误状态 */
} storage_state_t;

/**
 * @brief 文件操作类型
 */
typedef enum {
    STORAGE_OP_READ = 0,               /**< 读取文件 */
    STORAGE_OP_WRITE,                  /**< 写入文件 */
    STORAGE_OP_APPEND,                 /**< 追加到文件 */
    STORAGE_OP_DELETE,                 /**< 删除文件 */
    STORAGE_OP_COPY,                   /**< 复制文件 */
    STORAGE_OP_MOVE,                   /**< 移动文件 */
    STORAGE_OP_MKDIR,                  /**< 创建目录 */
    STORAGE_OP_RMDIR,                  /**< 删除目录 */
    STORAGE_OP_LIST_DIR,               /**< 列出目录 */
    STORAGE_OP_STAT,                   /**< 获取文件信息 */
    STORAGE_OP_MOUNT,                  /**< 挂载设备 */
    STORAGE_OP_UNMOUNT,                /**< 卸载设备 */
    STORAGE_OP_FORMAT                  /**< 格式化设备 */
} storage_operation_type_t;

/**
 * @brief 文件信息结构
 */
typedef struct {
    char name[STORAGE_MANAGER_MAX_FILENAME_LENGTH + 1]; /**< 文件名 */
    size_t size;                       /**< 文件大小 */
    time_t mtime;                      /**< 修改时间 */
    bool is_directory;                 /**< 是否为目录 */
    mode_t mode;                       /**< 文件权限 */
} storage_file_info_t;

/**
 * @brief 目录列表结构
 */
typedef struct {
    storage_file_info_t* files;        /**< 文件信息数组 */
    size_t count;                      /**< 文件数量 */
    size_t capacity;                   /**< 数组容量 */
} storage_dir_list_t;

/**
 * @brief 存储统计信息
 */
typedef struct {
    uint64_t total_bytes;              /**< 总容量（字节） */
    uint64_t used_bytes;               /**< 已用空间（字节） */
    uint64_t free_bytes;               /**< 可用空间（字节） */
    uint32_t total_files;              /**< 总文件数 */
    uint32_t total_directories;        /**< 总目录数 */
} storage_stats_t;

/**
 * @brief 存储配置结构
 */
typedef struct {
    char mount_point[STORAGE_MANAGER_MAX_PATH_LENGTH]; /**< 挂载点路径 */
    bool format_if_mount_failed;       /**< 挂载失败时是否格式化 */
    bool enable_hot_swap;              /**< 是否启用热插拔检测 */
    uint32_t max_files;                /**< 最大文件数限制 */
    uint32_t allocation_unit_size;     /**< 分配单元大小 */
    bool enable_cache;                 /**< 是否启用缓存 */
    uint32_t cache_size;               /**< 缓存大小 */
} storage_config_t;

/**
 * @brief 异步操作回调函数类型
 * 
 * @param operation 操作类型
 * @param result 操作结果 (ESP_OK表示成功)
 * @param data 操作相关数据 (可为NULL)
 * @param user_data 用户数据指针
 */
typedef void (*storage_callback_t)(storage_operation_type_t operation, 
                                   esp_err_t result, 
                                   void* data, 
                                   void* user_data);

/**
 * @brief 异步操作请求结构
 */
typedef struct {
    uint32_t request_id;               /**< 请求ID */
    storage_operation_type_t operation; /**< 操作类型 */
    char path1[STORAGE_MANAGER_MAX_PATH_LENGTH]; /**< 源路径 */
    char path2[STORAGE_MANAGER_MAX_PATH_LENGTH]; /**< 目标路径（可选） */
    void* data;                        /**< 操作数据 */
    size_t data_size;                  /**< 数据大小 */
    storage_callback_t callback;       /**< 回调函数 */
    void* user_data;                   /**< 用户数据 */
    uint32_t timeout_ms;               /**< 超时时间（毫秒） */
} storage_operation_request_t;

/* ================================ API 函数 ================================ */

/**
 * @brief 初始化存储管理器
 * 
 * @param config 存储配置参数
 * @return esp_err_t 
 *         - ESP_OK: 初始化成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_NO_MEM: 内存不足
 *         - ESP_FAIL: 初始化失败
 */
esp_err_t storage_manager_init(const storage_config_t* config);

/**
 * @brief 反初始化存储管理器
 * 
 * @return esp_err_t 
 *         - ESP_OK: 反初始化成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 */
esp_err_t storage_manager_deinit(void);

/**
 * @brief 检查存储管理器是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool storage_manager_is_initialized(void);

/**
 * @brief 获取当前存储状态
 * 
 * @return storage_state_t 当前状态
 */
storage_state_t storage_manager_get_state(void);

/**
 * @brief 获取存储统计信息
 * 
 * @param stats 输出的统计信息
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_INVALID_STATE: 存储未挂载
 */
esp_err_t storage_manager_get_stats(storage_stats_t* stats);

/* ================================ 设备管理 API ================================ */

/**
 * @brief 异步挂载TF卡
 * 
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_mount_async(storage_callback_t callback, void* user_data);

/**
 * @brief 异步卸载TF卡
 * 
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_unmount_async(storage_callback_t callback, void* user_data);

/**
 * @brief 异步格式化TF卡
 * 
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_format_async(storage_callback_t callback, void* user_data);

/* ================================ 文件操作 API ================================ */

/**
 * @brief 异步读取文件
 * 
 * @param path 文件路径
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_read_file_async(const char* path, 
                                          storage_callback_t callback, 
                                          void* user_data);

/**
 * @brief 异步写入文件
 * 
 * @param path 文件路径
 * @param data 要写入的数据
 * @param size 数据大小
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_write_file_async(const char* path, 
                                           const void* data, 
                                           size_t size,
                                           storage_callback_t callback, 
                                           void* user_data);

/**
 * @brief 异步追加到文件
 * 
 * @param path 文件路径
 * @param data 要追加的数据
 * @param size 数据大小
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_append_file_async(const char* path, 
                                            const void* data, 
                                            size_t size,
                                            storage_callback_t callback, 
                                            void* user_data);

/**
 * @brief 异步删除文件
 * 
 * @param path 文件路径
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_delete_file_async(const char* path, 
                                            storage_callback_t callback, 
                                            void* user_data);

/* ================================ 目录操作 API ================================ */

/**
 * @brief 异步列出目录内容
 * 
 * @param path 目录路径
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_list_dir_async(const char* path, 
                                         storage_callback_t callback, 
                                         void* user_data);

/**
 * @brief 异步创建目录
 * 
 * @param path 目录路径
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_mkdir_async(const char* path, 
                                      storage_callback_t callback, 
                                      void* user_data);

/**
 * @brief 异步删除目录
 * 
 * @param path 目录路径
 * @param recursive 是否递归删除
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_rmdir_async(const char* path, 
                                      bool recursive,
                                      storage_callback_t callback, 
                                      void* user_data);

/* ================================ Shell 风格 API ================================ */

/**
 * @brief 异步复制文件或目录
 * 
 * @param src_path 源路径
 * @param dst_path 目标路径
 * @param recursive 是否递归复制目录
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_copy_async(const char* src_path, 
                                     const char* dst_path,
                                     bool recursive,
                                     storage_callback_t callback, 
                                     void* user_data);

/**
 * @brief 异步移动文件或目录
 * 
 * @param src_path 源路径
 * @param dst_path 目标路径
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_move_async(const char* src_path, 
                                     const char* dst_path,
                                     storage_callback_t callback, 
                                     void* user_data);

/**
 * @brief 异步获取文件/目录信息
 * 
 * @param path 路径
 * @param callback 完成回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 *         - ESP_OK: 请求提交成功
 *         - ESP_ERR_INVALID_ARG: 路径无效
 *         - ESP_ERR_NO_MEM: 队列满
 */
esp_err_t storage_manager_stat_async(const char* path, 
                                     storage_callback_t callback, 
                                     void* user_data);

/* ================================ 辅助函数 ================================ */

/**
 * @brief 释放目录列表内存
 * 
 * @param dir_list 目录列表指针
 */
void storage_manager_free_dir_list(storage_dir_list_t* dir_list);

/**
 * @brief 获取默认配置
 * 
 * @param config 输出的配置结构
 */
void storage_manager_get_default_config(storage_config_t* config);

/**
 * @brief 获取错误描述字符串
 * 
 * @param error 错误码
 * @return const char* 错误描述
 */
const char* storage_manager_error_to_string(esp_err_t error);

/* ================================ 控制台命令 ================================ */

/**
 * @brief 注册存储管理器控制台命令
 * 
 * 在控制台系统中注册所有与存储管理相关的命令，包括文件操作、
 * 目录操作、挂载/卸载等。需要在console_core和storage_manager
 * 都初始化完成后调用。
 * 
 * @return esp_err_t ESP_OK成功，其他值表示失败
 */
esp_err_t storage_manager_register_console_commands(void);

/**
 * @brief 注销存储管理器控制台命令
 * 
 * @return esp_err_t ESP_OK成功，其他值表示失败
 */
esp_err_t storage_manager_unregister_console_commands(void);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_MANAGER_H