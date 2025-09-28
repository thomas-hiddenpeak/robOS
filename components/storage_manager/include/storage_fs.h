/**
 * @file storage_fs.h
 * @brief 文件系统抽象层 - 提供统一的文件系统操作接口
 * 
 * 封装底层文件系统操作，提供统一的API接口，支持各种文件
 * 和目录操作，包括批量操作和递归操作。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#ifndef STORAGE_FS_H
#define STORAGE_FS_H

#include "esp_err.h"
#include "storage_manager.h"
#include <sys/stat.h>
#include <dirent.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ 常量定义 ================================ */

#define STORAGE_FS_MAX_OPEN_FILES       (16)
#define STORAGE_FS_BUFFER_SIZE          (4096)
#define STORAGE_FS_MAX_COPY_BUFFER      (8192)

/* ================================ 数据结构 ================================ */

/**
 * @brief 文件操作选项
 */
typedef struct {
    bool create_if_not_exists;         /**< 文件不存在时是否创建 */
    bool truncate_if_exists;           /**< 文件存在时是否截断 */
    bool append_mode;                  /**< 是否为追加模式 */
    mode_t permissions;                /**< 文件权限 */
} storage_fs_file_options_t;

/**
 * @brief 目录操作选项
 */
typedef struct {
    bool recursive;                    /**< 是否递归操作 */
    bool create_parents;               /**< 是否创建父目录 */
    bool force;                        /**< 是否强制操作 */
    mode_t permissions;                /**< 目录权限 */
} storage_fs_dir_options_t;

/**
 * @brief 复制操作选项
 */
typedef struct {
    bool recursive;                    /**< 是否递归复制 */
    bool preserve_timestamps;          /**< 是否保持时间戳 */
    bool overwrite_existing;           /**< 是否覆盖已存在文件 */
    bool follow_symlinks;              /**< 是否跟随符号链接 */
} storage_fs_copy_options_t;

/**
 * @brief 搜索选项
 */
typedef struct {
    const char* name_pattern;          /**< 文件名模式 */
    bool case_sensitive;               /**< 是否大小写敏感 */
    bool include_directories;          /**< 是否包含目录 */
    bool recursive;                    /**< 是否递归搜索 */
    size_t max_results;                /**< 最大结果数 */
} storage_fs_search_options_t;

/* ================================ 文件操作 API ================================ */

/**
 * @brief 读取整个文件内容
 * 
 * @param path 文件路径
 * @param data 输出数据缓冲区指针
 * @param size 输出数据大小
 * @return esp_err_t 
 *         - ESP_OK: 读取成功
 *         - ESP_ERR_NOT_FOUND: 文件不存在
 *         - ESP_ERR_NO_MEM: 内存不足
 *         - ESP_FAIL: 读取失败
 */
esp_err_t storage_fs_read_file(const char* path, void** data, size_t* size);

/**
 * @brief 写入数据到文件
 * 
 * @param path 文件路径
 * @param data 要写入的数据
 * @param size 数据大小
 * @param options 文件操作选项
 * @return esp_err_t 
 *         - ESP_OK: 写入成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_NO_MEM: 磁盘空间不足
 *         - ESP_FAIL: 写入失败
 */
esp_err_t storage_fs_write_file(const char* path, 
                                const void* data, 
                                size_t size,
                                const storage_fs_file_options_t* options);

/**
 * @brief 追加数据到文件
 * 
 * @param path 文件路径
 * @param data 要追加的数据
 * @param size 数据大小
 * @return esp_err_t 
 *         - ESP_OK: 追加成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_NO_MEM: 磁盘空间不足
 *         - ESP_FAIL: 追加失败
 */
esp_err_t storage_fs_append_file(const char* path, const void* data, size_t size);

/**
 * @brief 删除文件
 * 
 * @param path 文件路径
 * @return esp_err_t 
 *         - ESP_OK: 删除成功
 *         - ESP_ERR_NOT_FOUND: 文件不存在
 *         - ESP_FAIL: 删除失败
 */
esp_err_t storage_fs_delete_file(const char* path);

/**
 * @brief 复制文件
 * 
 * @param src_path 源文件路径
 * @param dst_path 目标文件路径
 * @param options 复制选项
 * @return esp_err_t 
 *         - ESP_OK: 复制成功
 *         - ESP_ERR_NOT_FOUND: 源文件不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_FAIL: 复制失败
 */
esp_err_t storage_fs_copy_file(const char* src_path, 
                               const char* dst_path,
                               const storage_fs_copy_options_t* options);

/**
 * @brief 移动/重命名文件
 * 
 * @param src_path 源路径
 * @param dst_path 目标路径
 * @return esp_err_t 
 *         - ESP_OK: 移动成功
 *         - ESP_ERR_NOT_FOUND: 源文件不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_FAIL: 移动失败
 */
esp_err_t storage_fs_move_file(const char* src_path, const char* dst_path);

/* ================================ 目录操作 API ================================ */

/**
 * @brief 列出目录内容
 * 
 * @param path 目录路径
 * @param dir_list 输出的目录列表
 * @return esp_err_t 
 *         - ESP_OK: 列出成功
 *         - ESP_ERR_NOT_FOUND: 目录不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t storage_fs_list_directory(const char* path, storage_dir_list_t* dir_list);

/**
 * @brief 创建目录
 * 
 * @param path 目录路径
 * @param options 目录操作选项
 * @return esp_err_t 
 *         - ESP_OK: 创建成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_FAIL: 创建失败
 */
esp_err_t storage_fs_create_directory(const char* path, 
                                      const storage_fs_dir_options_t* options);

/**
 * @brief 删除目录
 * 
 * @param path 目录路径
 * @param options 目录操作选项
 * @return esp_err_t 
 *         - ESP_OK: 删除成功
 *         - ESP_ERR_NOT_FOUND: 目录不存在
 *         - ESP_ERR_INVALID_ARG: 目录非空且非递归删除
 *         - ESP_FAIL: 删除失败
 */
esp_err_t storage_fs_remove_directory(const char* path, 
                                      const storage_fs_dir_options_t* options);

/**
 * @brief 复制目录
 * 
 * @param src_path 源目录路径
 * @param dst_path 目标目录路径
 * @param options 复制选项
 * @return esp_err_t 
 *         - ESP_OK: 复制成功
 *         - ESP_ERR_NOT_FOUND: 源目录不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_FAIL: 复制失败
 */
esp_err_t storage_fs_copy_directory(const char* src_path, 
                                    const char* dst_path,
                                    const storage_fs_copy_options_t* options);

/* ================================ 信息查询 API ================================ */

/**
 * @brief 获取文件/目录信息
 * 
 * @param path 路径
 * @param info 输出的文件信息
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_NOT_FOUND: 路径不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_fs_get_info(const char* path, storage_file_info_t* info);

/**
 * @brief 检查路径是否存在
 * 
 * @param path 路径
 * @return true 路径存在
 * @return false 路径不存在
 */
bool storage_fs_exists(const char* path);

/**
 * @brief 检查是否为文件
 * 
 * @param path 路径
 * @return true 是文件
 * @return false 不是文件或不存在
 */
bool storage_fs_is_file(const char* path);

/**
 * @brief 检查是否为目录
 * 
 * @param path 路径
 * @return true 是目录
 * @return false 不是目录或不存在
 */
bool storage_fs_is_directory(const char* path);

/**
 * @brief 获取文件大小
 * 
 * @param path 文件路径
 * @param size 输出的文件大小
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_NOT_FOUND: 文件不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_fs_get_file_size(const char* path, size_t* size);

/* ================================ 高级功能 API ================================ */

/**
 * @brief 递归搜索文件
 * 
 * @param root_path 搜索根路径
 * @param options 搜索选项
 * @param results 输出的搜索结果
 * @return esp_err_t 
 *         - ESP_OK: 搜索成功
 *         - ESP_ERR_NOT_FOUND: 根路径不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t storage_fs_search_files(const char* root_path,
                                  const storage_fs_search_options_t* options,
                                  storage_dir_list_t* results);

/**
 * @brief 计算目录大小（递归）
 * 
 * @param path 目录路径
 * @param total_size 输出的总大小
 * @param file_count 输出的文件数量
 * @param dir_count 输出的目录数量
 * @return esp_err_t 
 *         - ESP_OK: 计算成功
 *         - ESP_ERR_NOT_FOUND: 目录不存在
 *         - ESP_ERR_INVALID_ARG: 无效参数
 */
esp_err_t storage_fs_calculate_directory_size(const char* path,
                                              uint64_t* total_size,
                                              uint32_t* file_count,
                                              uint32_t* dir_count);

/* ================================ 辅助函数 ================================ */

/**
 * @brief 规范化路径
 * 
 * @param path 输入路径
 * @param normalized 输出的规范化路径
 * @param max_len 最大长度
 * @return esp_err_t 
 *         - ESP_OK: 规范化成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_INVALID_SIZE: 路径过长
 */
esp_err_t storage_fs_normalize_path(const char* path, char* normalized, size_t max_len);

/**
 * @brief 获取文件扩展名
 * 
 * @param filename 文件名
 * @return const char* 扩展名指针（不包含点号），如果没有扩展名返回NULL
 */
const char* storage_fs_get_file_extension(const char* filename);

/**
 * @brief 获取文件名（不包含路径）
 * 
 * @param path 完整路径
 * @return const char* 文件名指针
 */
const char* storage_fs_get_filename(const char* path);

/**
 * @brief 获取目录路径（不包含文件名）
 * 
 * @param path 完整路径
 * @param dir_path 输出的目录路径
 * @param max_len 最大长度
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_INVALID_SIZE: 路径过长
 */
esp_err_t storage_fs_get_directory_path(const char* path, char* dir_path, size_t max_len);

/**
 * @brief 获取默认文件操作选项
 * 
 * @param options 输出的选项结构
 */
void storage_fs_get_default_file_options(storage_fs_file_options_t* options);

/**
 * @brief 获取默认目录操作选项
 * 
 * @param options 输出的选项结构
 */
void storage_fs_get_default_dir_options(storage_fs_dir_options_t* options);

/**
 * @brief 获取默认复制选项
 * 
 * @param options 输出的选项结构
 */
void storage_fs_get_default_copy_options(storage_fs_copy_options_t* options);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_FS_H