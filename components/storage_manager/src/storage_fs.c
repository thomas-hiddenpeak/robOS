/**
 * @file storage_fs.c
 * @brief 文件系统抽象层实现
 * 
 * 封装底层文件系统操作，提供统一的API接口，支持各种文件
 * 和目录操作，包括批量操作和递归操作。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#include "storage_fs.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ================================ 常量和宏定义 ================================ */

static const char* TAG = "storage_fs";

/* ================================ 内部函数声明 ================================ */

static esp_err_t copy_file_internal(const char* src_path, const char* dst_path, 
                                    const storage_fs_copy_options_t* options);
static esp_err_t remove_directory_recursive(const char* path);
static bool match_pattern(const char* name, const char* pattern, bool case_sensitive);
static esp_err_t search_files_recursive(const char* path,
                                        const storage_fs_search_options_t* options,
                                        storage_dir_list_t* results,
                                        size_t depth);
static esp_err_t calculate_directory_size_recursive(const char* path,
                                                    uint64_t* total_size,
                                                    uint32_t* file_count,
                                                    uint32_t* dir_count);

/* ================================ 文件操作 API 实现 ================================ */

esp_err_t storage_fs_read_file(const char* path, void** data, size_t* size)
{
    if (!path || !data || !size) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Reading file: %s", path);
    
    FILE* file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file %s: %s", path, strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < 0) {
        ESP_LOGE(TAG, "Failed to get file size: %s", strerror(errno));
        fclose(file);
        return ESP_FAIL;
    }
    
    // 分配内存
    void* buffer = malloc(file_size + 1); // +1 for null terminator
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for file content");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    // 读取文件内容
    size_t bytes_read = fread(buffer, 1, file_size, file);
    if (bytes_read != (size_t)file_size) {
        ESP_LOGE(TAG, "Failed to read file content: expected %ld, got %zu", file_size, bytes_read);
        free(buffer);
        fclose(file);
        return ESP_FAIL;
    }
    
    // 添加null terminator
    ((char*)buffer)[file_size] = '\0';
    
    fclose(file);
    
    *data = buffer;
    *size = file_size;
    
    ESP_LOGD(TAG, "File read successfully: %zu bytes", *size);
    
    return ESP_OK;
}

esp_err_t storage_fs_write_file(const char* path, const void* data, size_t size,
                                const storage_fs_file_options_t* options)
{
    if (!path || !data) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Writing file: %s (%zu bytes)", path, size);
    
    const char* mode = "wb";
    if (options) {
        if (options->append_mode) {
            mode = "ab";
        } else if (!options->truncate_if_exists && !options->create_if_not_exists) {
            mode = "r+b";
        }
    }
    
    FILE* file = fopen(path, mode);
    if (!file) {
        if (options && options->create_if_not_exists) {
            file = fopen(path, "wb");
        }
        
        if (!file) {
            ESP_LOGE(TAG, "Failed to open file %s: %s", path, strerror(errno));
            return ESP_FAIL;
        }
    }
    
    size_t bytes_written = fwrite(data, 1, size, file);
    if (bytes_written != size) {
        ESP_LOGE(TAG, "Failed to write file: expected %zu, wrote %zu", size, bytes_written);
        fclose(file);
        return ESP_FAIL;
    }
    
    fclose(file);
    
    ESP_LOGD(TAG, "File written successfully: %zu bytes", bytes_written);
    
    return ESP_OK;
}

esp_err_t storage_fs_append_file(const char* path, const void* data, size_t size)
{
    storage_fs_file_options_t options = {0};
    options.append_mode = true;
    options.create_if_not_exists = true;
    
    return storage_fs_write_file(path, data, size, &options);
}

esp_err_t storage_fs_delete_file(const char* path)
{
    if (!path) {
        ESP_LOGE(TAG, "Invalid path parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Deleting file: %s", path);
    
    if (unlink(path) != 0) {
        ESP_LOGE(TAG, "Failed to delete file %s: %s", path, strerror(errno));
        if (errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "File deleted successfully");
    
    return ESP_OK;
}

esp_err_t storage_fs_copy_file(const char* src_path, const char* dst_path,
                               const storage_fs_copy_options_t* options)
{
    if (!src_path || !dst_path) {
        ESP_LOGE(TAG, "Invalid path parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    return copy_file_internal(src_path, dst_path, options);
}

esp_err_t storage_fs_move_file(const char* src_path, const char* dst_path)
{
    if (!src_path || !dst_path) {
        ESP_LOGE(TAG, "Invalid path parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Moving file: %s -> %s", src_path, dst_path);
    
    if (rename(src_path, dst_path) != 0) {
        ESP_LOGE(TAG, "Failed to move file: %s", strerror(errno));
        if (errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "File moved successfully");
    
    return ESP_OK;
}

/* ================================ 目录操作 API 实现 ================================ */

esp_err_t storage_fs_list_directory(const char* path, storage_dir_list_t* dir_list)
{
    if (!path || !dir_list) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Listing directory: %s", path);
    
    DIR* dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory %s: %s", path, strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }
    
    // 初始化目录列表
    dir_list->files = NULL;
    dir_list->count = 0;
    dir_list->capacity = 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // 跳过当前目录和父目录
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 扩展数组容量
        if (dir_list->count >= dir_list->capacity) {
            size_t new_capacity = dir_list->capacity == 0 ? 16 : dir_list->capacity * 2;
            storage_file_info_t* new_files = realloc(dir_list->files, 
                                                     new_capacity * sizeof(storage_file_info_t));
            if (!new_files) {
                ESP_LOGE(TAG, "Failed to allocate memory for directory list");
                closedir(dir);
                free(dir_list->files);
                return ESP_ERR_NO_MEM;
            }
            dir_list->files = new_files;
            dir_list->capacity = new_capacity;
        }
        
        // 填充文件信息
        storage_file_info_t* file_info = &dir_list->files[dir_list->count];
        strncpy(file_info->name, entry->d_name, sizeof(file_info->name) - 1);
        file_info->name[sizeof(file_info->name) - 1] = '\0';
        
        // 获取文件详细信息
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            file_info->size = st.st_size;
            file_info->mtime = st.st_mtime;
            file_info->is_directory = S_ISDIR(st.st_mode);
            file_info->mode = st.st_mode;
        } else {
            file_info->size = 0;
            file_info->mtime = 0;
            file_info->is_directory = (entry->d_type == DT_DIR);
            file_info->mode = 0;
        }
        
        dir_list->count++;
    }
    
    closedir(dir);
    
    ESP_LOGD(TAG, "Directory listed successfully: %zu items", dir_list->count);
    
    return ESP_OK;
}

esp_err_t storage_fs_create_directory(const char* path, const storage_fs_dir_options_t* options)
{
    if (!path) {
        ESP_LOGE(TAG, "Invalid path parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Creating directory: %s", path);
    
    mode_t mode = 0755; // 默认权限
    if (options && options->permissions != 0) {
        mode = options->permissions;
    }
    
    // 如果需要创建父目录
    if (options && options->create_parents) {
        char* path_copy = strdup(path);
        if (!path_copy) {
            return ESP_ERR_NO_MEM;
        }
        
        char* p = path_copy;
        while (*p) {
            p++;
            while (*p && *p != '/') p++;
            
            char saved = *p;
            *p = '\0';
            
            if (mkdir(path_copy, mode) != 0 && errno != EEXIST) {
                ESP_LOGE(TAG, "Failed to create directory %s: %s", path_copy, strerror(errno));
                free(path_copy);
                return ESP_FAIL;
            }
            
            *p = saved;
        }
        
        free(path_copy);
        return ESP_OK;
    }
    
    if (mkdir(path, mode) != 0) {
        ESP_LOGE(TAG, "Failed to create directory %s: %s", path, strerror(errno));
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Directory created successfully");
    
    return ESP_OK;
}

esp_err_t storage_fs_remove_directory(const char* path, const storage_fs_dir_options_t* options)
{
    if (!path) {
        ESP_LOGE(TAG, "Invalid path parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Removing directory: %s", path);
    
    if (options && options->recursive) {
        return remove_directory_recursive(path);
    }
    
    if (rmdir(path) != 0) {
        ESP_LOGE(TAG, "Failed to remove directory %s: %s", path, strerror(errno));
        if (errno == ENOENT) {
            return ESP_ERR_NOT_FOUND;
        }
        if (errno == ENOTEMPTY) {
            return ESP_ERR_INVALID_ARG;
        }
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Directory removed successfully");
    
    return ESP_OK;
}

/* ================================ 信息查询 API 实现 ================================ */

esp_err_t storage_fs_get_info(const char* path, storage_file_info_t* info)
{
    if (!path || !info) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "Failed to get file info for %s: %s", path, strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }
    
    // 提取文件名
    const char* filename = strrchr(path, '/');
    if (filename) {
        filename++; // 跳过'/'
    } else {
        filename = path;
    }
    
    strncpy(info->name, filename, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    info->size = st.st_size;
    info->mtime = st.st_mtime;
    info->is_directory = S_ISDIR(st.st_mode);
    info->mode = st.st_mode;
    
    return ESP_OK;
}

bool storage_fs_exists(const char* path)
{
    if (!path) {
        return false;
    }
    
    struct stat st;
    return (stat(path, &st) == 0);
}

bool storage_fs_is_file(const char* path)
{
    if (!path) {
        return false;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    
    return S_ISREG(st.st_mode);
}

bool storage_fs_is_directory(const char* path)
{
    if (!path) {
        return false;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    
    return S_ISDIR(st.st_mode);
}

esp_err_t storage_fs_get_file_size(const char* path, size_t* size)
{
    if (!path || !size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    *size = st.st_size;
    return ESP_OK;
}

/* ================================ 辅助函数实现 ================================ */

void storage_fs_get_default_file_options(storage_fs_file_options_t* options)
{
    if (!options) {
        return;
    }
    
    memset(options, 0, sizeof(storage_fs_file_options_t));
    options->create_if_not_exists = true;
    options->truncate_if_exists = true;
    options->permissions = 0644;
}

void storage_fs_get_default_dir_options(storage_fs_dir_options_t* options)
{
    if (!options) {
        return;
    }
    
    memset(options, 0, sizeof(storage_fs_dir_options_t));
    options->permissions = 0755;
}

void storage_fs_get_default_copy_options(storage_fs_copy_options_t* options)
{
    if (!options) {
        return;
    }
    
    memset(options, 0, sizeof(storage_fs_copy_options_t));
    options->preserve_timestamps = true;
    options->overwrite_existing = false;
}

/* ================================ 内部函数实现 ================================ */

static esp_err_t copy_file_internal(const char* src_path, const char* dst_path, 
                                    const storage_fs_copy_options_t* options)
{
    ESP_LOGD(TAG, "Copying file: %s -> %s", src_path, dst_path);
    
    // 检查源文件是否存在
    if (!storage_fs_exists(src_path)) {
        ESP_LOGE(TAG, "Source file does not exist: %s", src_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 检查目标文件是否存在
    if (storage_fs_exists(dst_path)) {
        if (options && !options->overwrite_existing) {
            ESP_LOGE(TAG, "Destination file exists and overwrite not allowed: %s", dst_path);
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    // 读取源文件
    void* data;
    size_t size;
    esp_err_t ret = storage_fs_read_file(src_path, &data, &size);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 写入目标文件
    storage_fs_file_options_t file_options = {0};
    file_options.create_if_not_exists = true;
    file_options.truncate_if_exists = true;
    
    ret = storage_fs_write_file(dst_path, data, size, &file_options);
    free(data);
    
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 保持时间戳（ESP-IDF不支持utime，跳过）
    if (options && options->preserve_timestamps) {
        ESP_LOGW(TAG, "Timestamp preservation not supported on ESP-IDF");
    }
    
    ESP_LOGD(TAG, "File copied successfully");
    
    return ESP_OK;
}

static esp_err_t remove_directory_recursive(const char* path)
{
    storage_dir_list_t dir_list;
    esp_err_t ret = storage_fs_list_directory(path, &dir_list);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 递归删除所有内容
    for (size_t i = 0; i < dir_list.count; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, dir_list.files[i].name);
        
        if (dir_list.files[i].is_directory) {
            ret = remove_directory_recursive(full_path);
        } else {
            ret = storage_fs_delete_file(full_path);
        }
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove %s: %s", full_path, esp_err_to_name(ret));
        }
    }
    
    // 释放目录列表
    free(dir_list.files);
    
    // 删除空目录
    if (rmdir(path) != 0) {
        ESP_LOGE(TAG, "Failed to remove directory %s: %s", path, strerror(errno));
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t storage_fs_copy_directory(const char* src_path, 
                                    const char* dst_path,
                                    const storage_fs_copy_options_t* options)
{
    if (!src_path || !dst_path) {
        ESP_LOGE(TAG, "Invalid path parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Copying directory: %s -> %s", src_path, dst_path);
    
    // 检查源目录是否存在
    if (!storage_fs_is_directory(src_path)) {
        ESP_LOGE(TAG, "Source is not a directory: %s", src_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 创建目标目录
    storage_fs_dir_options_t dir_options = {0};
    storage_fs_get_default_dir_options(&dir_options);
    dir_options.create_parents = true;
    
    esp_err_t ret = storage_fs_create_directory(dst_path, &dir_options);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_ARG) { // 目录已存在不算错误
        return ret;
    }
    
    // 如果不是递归复制，只创建目录
    if (!options || !options->recursive) {
        return ESP_OK;
    }
    
    // 递归复制目录内容
    storage_dir_list_t dir_list;
    ret = storage_fs_list_directory(src_path, &dir_list);
    if (ret != ESP_OK) {
        return ret;
    }
    
    for (size_t i = 0; i < dir_list.count; i++) {
        char src_item[512], dst_item[512];
        snprintf(src_item, sizeof(src_item), "%s/%s", src_path, dir_list.files[i].name);
        snprintf(dst_item, sizeof(dst_item), "%s/%s", dst_path, dir_list.files[i].name);
        
        if (dir_list.files[i].is_directory) {
            ret = storage_fs_copy_directory(src_item, dst_item, options);
        } else {
            ret = copy_file_internal(src_item, dst_item, options);
        }
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to copy %s: %s", src_item, esp_err_to_name(ret));
        }
    }
    
    free(dir_list.files);
    
    ESP_LOGD(TAG, "Directory copied successfully");
    
    return ESP_OK;
}

esp_err_t storage_fs_search_files(const char* root_path,
                                  const storage_fs_search_options_t* options,
                                  storage_dir_list_t* results)
{
    if (!root_path || !options || !results) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Searching files in: %s", root_path);
    
    // 初始化结果
    results->files = NULL;
    results->count = 0;
    results->capacity = 0;
    
    // 检查根路径是否存在
    if (!storage_fs_is_directory(root_path)) {
        ESP_LOGE(TAG, "Root path is not a directory: %s", root_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 开始递归搜索
    return search_files_recursive(root_path, options, results, 0);
}

esp_err_t storage_fs_calculate_directory_size(const char* path,
                                              uint64_t* total_size,
                                              uint32_t* file_count,
                                              uint32_t* dir_count)
{
    if (!path || !total_size || !file_count || !dir_count) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Calculating directory size: %s", path);
    
    *total_size = 0;
    *file_count = 0;
    *dir_count = 0;
    
    if (!storage_fs_is_directory(path)) {
        ESP_LOGE(TAG, "Path is not a directory: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    return calculate_directory_size_recursive(path, total_size, file_count, dir_count);
}

esp_err_t storage_fs_normalize_path(const char* path, char* normalized, size_t max_len)
{
    if (!path || !normalized || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 简化的路径规范化实现
    size_t path_len = strlen(path);
    if (path_len >= max_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(normalized, path);
    
    // 移除末尾的斜杠（除非是根路径）
    if (path_len > 1 && normalized[path_len - 1] == '/') {
        normalized[path_len - 1] = '\0';
    }
    
    return ESP_OK;
}

const char* storage_fs_get_file_extension(const char* filename)
{
    if (!filename) {
        return NULL;
    }
    
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return NULL;
    }
    
    return dot + 1;
}

const char* storage_fs_get_filename(const char* path)
{
    if (!path) {
        return NULL;
    }
    
    const char* filename = strrchr(path, '/');
    if (filename) {
        return filename + 1;
    }
    
    return path;
}

esp_err_t storage_fs_get_directory_path(const char* path, char* dir_path, size_t max_len)
{
    if (!path || !dir_path || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) {
        // 没有路径分隔符，返回当前目录
        strncpy(dir_path, ".", max_len - 1);
        dir_path[max_len - 1] = '\0';
        return ESP_OK;
    }
    
    size_t dir_len = last_slash - path;
    if (dir_len == 0) {
        // 根目录
        strncpy(dir_path, "/", max_len - 1);
    } else {
        if (dir_len >= max_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        strncpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
    }
    
    return ESP_OK;
}

// 内部递归搜索函数
static esp_err_t search_files_recursive(const char* path,
                                        const storage_fs_search_options_t* options,
                                        storage_dir_list_t* results,
                                        size_t depth)
{
    // 检查深度限制
    if (options->max_results > 0 && results->count >= options->max_results) {
        return ESP_OK;
    }
    
    storage_dir_list_t dir_list;
    esp_err_t ret = storage_fs_list_directory(path, &dir_list);
    if (ret != ESP_OK) {
        return ret;
    }
    
    for (size_t i = 0; i < dir_list.count && (options->max_results == 0 || results->count < options->max_results); i++) {
        const storage_file_info_t* file = &dir_list.files[i];
        
        // 检查文件类型过滤
        if (file->is_directory && !options->include_directories) {
            goto continue_search;
        }
        
        if (!file->is_directory && options->include_directories && !options->recursive) {
            goto continue_search;
        }
        
        // 检查名称模式匹配
        if (options->name_pattern) {
            if (!match_pattern(file->name, options->name_pattern, options->case_sensitive)) {
                goto continue_search;
            }
        }
        
        // 添加到结果中
        if (results->count >= results->capacity) {
            size_t new_capacity = results->capacity == 0 ? 16 : results->capacity * 2;
            storage_file_info_t* new_files = realloc(results->files, 
                                                     new_capacity * sizeof(storage_file_info_t));
            if (!new_files) {
                free(dir_list.files);
                return ESP_ERR_NO_MEM;
            }
            results->files = new_files;
            results->capacity = new_capacity;
        }
        
        // 复制文件信息，包含完整路径
        results->files[results->count] = *file;
        int path_len = snprintf(results->files[results->count].name, sizeof(results->files[results->count].name),
                "%s/%s", path, file->name);
        if (path_len >= sizeof(results->files[results->count].name)) {
            ESP_LOGW(TAG, "Path too long for file: %s/%s", path, file->name);
        }
        results->count++;
        
continue_search:
        // 递归搜索子目录
        if (options->recursive && file->is_directory) {
            char subdir_path[512];
            snprintf(subdir_path, sizeof(subdir_path), "%s/%s", path, file->name);
            search_files_recursive(subdir_path, options, results, depth + 1);
        }
    }
    
    free(dir_list.files);
    return ESP_OK;
}

// 内部递归计算目录大小函数
static esp_err_t calculate_directory_size_recursive(const char* path,
                                                    uint64_t* total_size,
                                                    uint32_t* file_count,
                                                    uint32_t* dir_count)
{
    storage_dir_list_t dir_list;
    esp_err_t ret = storage_fs_list_directory(path, &dir_list);
    if (ret != ESP_OK) {
        return ret;
    }
    
    (*dir_count)++; // 计算当前目录
    
    for (size_t i = 0; i < dir_list.count; i++) {
        const storage_file_info_t* file = &dir_list.files[i];
        
        if (file->is_directory) {
            char* subdir_path = malloc(strlen(path) + strlen(file->name) + 2);
            if (subdir_path) {
                snprintf(subdir_path, strlen(path) + strlen(file->name) + 2, "%s/%s", path, file->name);
                calculate_directory_size_recursive(subdir_path, total_size, file_count, dir_count);
                free(subdir_path);
            }
        } else {
            (*file_count)++;
            *total_size += file->size;
        }
    }
    
    free(dir_list.files);
    return ESP_OK;
}

static bool match_pattern(const char* name, const char* pattern, bool case_sensitive)
{
    if (!name || !pattern) {
        return false;
    }
    
    // 简化的模式匹配，支持*通配符
    if (strcmp(pattern, "*") == 0) {
        return true;
    }
    
    // 精确匹配
    if (case_sensitive) {
        return strcmp(name, pattern) == 0;
    } else {
        return strcasecmp(name, pattern) == 0;
    }
}