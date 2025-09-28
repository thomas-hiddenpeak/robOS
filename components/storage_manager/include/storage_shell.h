/**
 * @file storage_shell.h
 * @brief Shell风格存储命令 - 提供类Linux命令行接口
 * 
 * 提供类似Linux shell的文件和目录操作命令，包括ls、cat、
 * cp、mv、rm、mkdir、rmdir等常用命令，支持各种选项和参数。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#ifndef STORAGE_SHELL_H
#define STORAGE_SHELL_H

#include "esp_err.h"
#include "storage_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ 常量定义 ================================ */

#define STORAGE_SHELL_MAX_ARGS          (16)
#define STORAGE_SHELL_MAX_OUTPUT_SIZE   (8192)

/* ================================ 数据结构 ================================ */

/**
 * @brief ls命令选项
 */
typedef struct {
    bool long_format;                  /**< -l: 长格式显示 */
    bool show_all;                     /**< -a: 显示隐藏文件 */
    bool recursive;                    /**< -R: 递归显示 */
    bool human_readable;               /**< -h: 人类可读的大小格式 */
    bool sort_by_time;                 /**< -t: 按时间排序 */
    bool reverse_sort;                 /**< -r: 反向排序 */
    bool show_only_directories;        /**< -d: 只显示目录 */
    bool one_per_line;                 /**< -1: 每行一个文件 */
} storage_shell_ls_options_t;

/**
 * @brief cp命令选项
 */
typedef struct {
    bool recursive;                    /**< -r: 递归复制 */
    bool preserve_timestamps;          /**< -p: 保持时间戳 */
    bool force;                        /**< -f: 强制覆盖 */
    bool interactive;                  /**< -i: 交互式确认 */
    bool verbose;                      /**< -v: 详细输出 */
    bool no_clobber;                   /**< -n: 不覆盖已存在文件 */
} storage_shell_cp_options_t;

/**
 * @brief rm命令选项
 */
typedef struct {
    bool recursive;                    /**< -r: 递归删除 */
    bool force;                        /**< -f: 强制删除 */
    bool interactive;                  /**< -i: 交互式确认 */
    bool verbose;                      /**< -v: 详细输出 */
    bool preserve_root;                /**< --preserve-root: 保护根目录 */
} storage_shell_rm_options_t;

/**
 * @brief mkdir命令选项
 */
typedef struct {
    bool create_parents;               /**< -p: 创建父目录 */
    bool verbose;                      /**< -v: 详细输出 */
    mode_t mode;                       /**< -m: 权限模式 */
} storage_shell_mkdir_options_t;

/**
 * @brief find命令选项
 */
typedef struct {
    const char* name_pattern;          /**< -name: 文件名模式 */
    const char* type;                  /**< -type: 文件类型 (f/d) */
    bool case_insensitive;             /**< -iname: 忽略大小写 */
    size_t max_depth;                  /**< -maxdepth: 最大搜索深度 */
    size_t min_size;                   /**< -size: 最小文件大小 */
    size_t max_size;                   /**< -size: 最大文件大小 */
} storage_shell_find_options_t;

/**
 * @brief 命令执行结果
 */
typedef struct {
    esp_err_t result;                  /**< 执行结果 */
    char* output;                      /**< 输出内容 */
    size_t output_size;                /**< 输出大小 */
    uint32_t items_processed;          /**< 处理的项目数 */
    uint32_t items_failed;             /**< 失败的项目数 */
} storage_shell_result_t;

/* ================================ Shell 命令 API ================================ */

/**
 * @brief ls命令 - 列出目录内容
 * 
 * @param path 目录路径（NULL表示当前目录）
 * @param options ls选项
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 目录不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_ls(const char* path, 
                           const storage_shell_ls_options_t* options,
                           storage_shell_result_t* result);

/**
 * @brief cat命令 - 查看文件内容
 * 
 * @param path 文件路径
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 文件不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_cat(const char* path, storage_shell_result_t* result);

/**
 * @brief cp命令 - 复制文件或目录
 * 
 * @param src_path 源路径
 * @param dst_path 目标路径
 * @param options cp选项
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 源不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_cp(const char* src_path, 
                           const char* dst_path,
                           const storage_shell_cp_options_t* options,
                           storage_shell_result_t* result);

/**
 * @brief mv命令 - 移动/重命名文件或目录
 * 
 * @param src_path 源路径
 * @param dst_path 目标路径
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 源不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_mv(const char* src_path, 
                           const char* dst_path,
                           storage_shell_result_t* result);

/**
 * @brief rm命令 - 删除文件或目录
 * 
 * @param path 要删除的路径
 * @param options rm选项
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 路径不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_rm(const char* path, 
                           const storage_shell_rm_options_t* options,
                           storage_shell_result_t* result);

/**
 * @brief mkdir命令 - 创建目录
 * 
 * @param path 目录路径
 * @param options mkdir选项
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_mkdir(const char* path, 
                              const storage_shell_mkdir_options_t* options,
                              storage_shell_result_t* result);

/**
 * @brief rmdir命令 - 删除空目录
 * 
 * @param path 目录路径
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 目录不存在
 *         - ESP_ERR_INVALID_ARG: 目录非空
 */
esp_err_t storage_shell_rmdir(const char* path, storage_shell_result_t* result);

/**
 * @brief touch命令 - 创建空文件或更新时间戳
 * 
 * @param path 文件路径
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_touch(const char* path, storage_shell_result_t* result);

/**
 * @brief stat命令 - 显示文件/目录详细信息
 * 
 * @param path 路径
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 路径不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_stat(const char* path, storage_shell_result_t* result);

/**
 * @brief find命令 - 搜索文件和目录
 * 
 * @param root_path 搜索根路径
 * @param options find选项
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 根路径不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_find(const char* root_path,
                             const storage_shell_find_options_t* options,
                             storage_shell_result_t* result);

/**
 * @brief du命令 - 显示目录使用情况
 * 
 * @param path 目录路径
 * @param human_readable 是否使用人类可读格式
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_NOT_FOUND: 目录不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_du(const char* path, 
                           bool human_readable,
                           storage_shell_result_t* result);

/**
 * @brief df命令 - 显示文件系统使用情况
 * 
 * @param human_readable 是否使用人类可读格式
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_INVALID_STATE: 存储未挂载
 */
esp_err_t storage_shell_df(bool human_readable, storage_shell_result_t* result);

/* ================================ 命令解析和执行 ================================ */

/**
 * @brief 解析并执行shell命令
 * 
 * @param command_line 完整的命令行
 * @param result 输出结果
 * @return esp_err_t 
 *         - ESP_OK: 执行成功
 *         - ESP_ERR_INVALID_ARG: 命令格式错误
 *         - ESP_ERR_NOT_SUPPORTED: 不支持的命令
 */
esp_err_t storage_shell_execute_command(const char* command_line, 
                                        storage_shell_result_t* result);

/**
 * @brief 获取支持的命令列表
 * 
 * @param commands 输出命令列表（以NULL结尾的字符串数组）
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 */
esp_err_t storage_shell_get_supported_commands(const char*** commands);

/* ================================ 辅助函数 ================================ */

/**
 * @brief 释放命令结果内存
 * 
 * @param result 命令结果指针
 */
void storage_shell_free_result(storage_shell_result_t* result);

/**
 * @brief 格式化文件大小为人类可读格式
 * 
 * @param bytes 字节数
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return esp_err_t 
 *         - ESP_OK: 格式化成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_format_size(uint64_t bytes, char* buffer, size_t buffer_size);

/**
 * @brief 格式化时间戳
 * 
 * @param timestamp 时间戳
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return esp_err_t 
 *         - ESP_OK: 格式化成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_shell_format_time(time_t timestamp, char* buffer, size_t buffer_size);

/**
 * @brief 获取默认ls选项
 * 
 * @param options 输出的选项结构
 */
void storage_shell_get_default_ls_options(storage_shell_ls_options_t* options);

/**
 * @brief 获取默认cp选项
 * 
 * @param options 输出的选项结构
 */
void storage_shell_get_default_cp_options(storage_shell_cp_options_t* options);

/**
 * @brief 获取默认rm选项
 * 
 * @param options 输出的选项结构
 */
void storage_shell_get_default_rm_options(storage_shell_rm_options_t* options);

/**
 * @brief 解析命令行参数
 * 
 * @param command_line 命令行字符串
 * @param argc 输出参数个数
 * @param argv 输出参数数组
 * @param max_args 最大参数个数
 * @return esp_err_t 
 *         - ESP_OK: 解析成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_INVALID_SIZE: 参数过多
 */
esp_err_t storage_shell_parse_args(const char* command_line, 
                                   int* argc, 
                                   char** argv, 
                                   int max_args);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_SHELL_H