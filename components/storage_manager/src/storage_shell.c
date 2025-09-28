/**
 * @file storage_shell.c
 * @brief Shell风格存储命令实现
 * 
 * 实现类似Linux shell的文件和目录操作命令，包括ls、cat、
 * cp、mv、rm、mkdir、rmdir等常用命令，支持各种选项和参数。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#include "storage_shell.h"
#include "storage_fs.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================================ 常量和宏定义 ================================ */

static const char* TAG = "storage_shell";

/* 支持的命令列表 */
static const char* SUPPORTED_COMMANDS[] = {
    "ls", "cat", "cp", "mv", "rm", "mkdir", "rmdir", 
    "touch", "stat", "find", "du", "df", NULL
};

/* ================================ 内部函数声明 ================================ */

static esp_err_t format_file_list(const storage_dir_list_t* dir_list,
                                  const storage_shell_ls_options_t* options,
                                  char** output, size_t* output_size);
static esp_err_t format_file_permissions(mode_t mode, char* buffer, size_t buffer_size);
static int compare_files_by_name(const void* a, const void* b);
static int compare_files_by_time(const void* a, const void* b);

/* ================================ Shell 命令 API 实现 ================================ */

esp_err_t storage_shell_ls(const char* path, 
                           const storage_shell_ls_options_t* options,
                           storage_shell_result_t* result)
{
    if (!result) {
        ESP_LOGE(TAG, "Invalid result parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    const char* list_path = path ? path : ".";
    
    ESP_LOGD(TAG, "Listing directory: %s", list_path);
    
    // 检查路径是否存在
    if (!storage_fs_exists(list_path)) {
        result->result = ESP_ERR_NOT_FOUND;
        result->output = strdup("Directory not found");
        result->output_size = strlen(result->output);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 如果是文件，显示文件信息
    if (storage_fs_is_file(list_path)) {
        storage_file_info_t file_info;
        esp_err_t ret = storage_fs_get_info(list_path, &file_info);
        if (ret != ESP_OK) {
            result->result = ret;
            result->output = strdup("Failed to get file info");
            result->output_size = strlen(result->output);
            return ret;
        }
        
        storage_dir_list_t single_file = {0};
        single_file.files = &file_info;
        single_file.count = 1;
        
        result->result = format_file_list(&single_file, options, &result->output, &result->output_size);
        result->items_processed = 1;
        return result->result;
    }
    
    // 列出目录内容
    storage_dir_list_t dir_list;
    esp_err_t ret = storage_fs_list_directory(list_path, &dir_list);
    if (ret != ESP_OK) {
        result->result = ret;
        result->output = strdup("Failed to list directory");
        result->output_size = strlen(result->output);
        return ret;
    }
    
    // 过滤隐藏文件
    if (options && !options->show_all) {
        size_t filtered_count = 0;
        for (size_t i = 0; i < dir_list.count; i++) {
            if (dir_list.files[i].name[0] != '.') {
                if (filtered_count != i) {
                    dir_list.files[filtered_count] = dir_list.files[i];
                }
                filtered_count++;
            }
        }
        dir_list.count = filtered_count;
    }
    
    // 排序
    if (dir_list.count > 1) {
        if (options && options->sort_by_time) {
            qsort(dir_list.files, dir_list.count, sizeof(storage_file_info_t), compare_files_by_time);
        } else {
            qsort(dir_list.files, dir_list.count, sizeof(storage_file_info_t), compare_files_by_name);
        }
        
        // 反向排序
        if (options && options->reverse_sort) {
            for (size_t i = 0; i < dir_list.count / 2; i++) {
                storage_file_info_t temp = dir_list.files[i];
                dir_list.files[i] = dir_list.files[dir_list.count - 1 - i];
                dir_list.files[dir_list.count - 1 - i] = temp;
            }
        }
    }
    
    // 格式化输出
    result->result = format_file_list(&dir_list, options, &result->output, &result->output_size);
    result->items_processed = dir_list.count;
    
    // 释放目录列表
    free(dir_list.files);
    
    return result->result;
}

esp_err_t storage_shell_cat(const char* path, storage_shell_result_t* result)
{
    if (!path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Reading file: %s", path);
    
    // 检查是否为文件
    if (!storage_fs_is_file(path)) {
        result->result = ESP_ERR_INVALID_ARG;
        result->output = strdup("Not a regular file");
        result->output_size = strlen(result->output);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 读取文件内容
    void* data;
    size_t size;
    esp_err_t ret = storage_fs_read_file(path, &data, &size);
    if (ret != ESP_OK) {
        result->result = ret;
        result->output = strdup("Failed to read file");
        result->output_size = strlen(result->output);
        return ret;
    }
    
    result->result = ESP_OK;
    result->output = (char*)data;
    result->output_size = size;
    result->items_processed = 1;
    
    return ESP_OK;
}

esp_err_t storage_shell_touch(const char* path, storage_shell_result_t* result)
{
    if (!path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Touching file: %s", path);
    
    esp_err_t ret;
    
    if (storage_fs_exists(path)) {
        // 文件存在，ESP-IDF不支持utime，跳过时间戳更新
        ESP_LOGW(TAG, "Timestamp update not supported on ESP-IDF");
    } else {
        // 文件不存在，创建空文件
        storage_fs_file_options_t options = {0};
        options.create_if_not_exists = true;
        
        ret = storage_fs_write_file(path, "", 0, &options);
        if (ret != ESP_OK) {
            result->result = ret;
            result->output = strdup("Failed to create file");
            result->output_size = strlen(result->output);
            return ret;
        }
    }
    
    result->result = ESP_OK;
    result->output = strdup("File touched successfully");
    result->output_size = strlen(result->output);
    result->items_processed = 1;
    
    return ESP_OK;
}

esp_err_t storage_shell_mkdir(const char* path, 
                              const storage_shell_mkdir_options_t* options,
                              storage_shell_result_t* result)
{
    if (!path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Creating directory: %s", path);
    
    storage_fs_dir_options_t dir_options = {0};
    if (options) {
        dir_options.create_parents = options->create_parents;
        dir_options.permissions = options->mode;
    }
    storage_fs_get_default_dir_options(&dir_options);
    
    esp_err_t ret = storage_fs_create_directory(path, &dir_options);
    if (ret != ESP_OK) {
        result->result = ret;
        result->output = strdup("Failed to create directory");
        result->output_size = strlen(result->output);
        return ret;
    }
    
    result->result = ESP_OK;
    result->output = strdup("Directory created successfully");
    result->output_size = strlen(result->output);
    result->items_processed = 1;
    
    return ESP_OK;
}

esp_err_t storage_shell_df(bool human_readable, storage_shell_result_t* result)
{
    if (!result) {
        ESP_LOGE(TAG, "Invalid result parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Getting filesystem usage");
    
    // 这里需要调用存储管理器的统计信息API
    // 暂时返回一个示例
    char* output = malloc(512);
    if (!output) {
        result->result = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    
    if (human_readable) {
        snprintf(output, 512, 
                "Filesystem     Size  Used Avail Use%% Mounted on\n"
                "/dev/sdcard0   32G   8.5G   23G  27%% /sdcard\n");
    } else {
        snprintf(output, 512,
                "Filesystem     1K-blocks    Used Available Use%% Mounted on\n"
                "/dev/sdcard0    33554432  8912896  24641536  27%% /sdcard\n");
    }
    
    result->result = ESP_OK;
    result->output = output;
    result->output_size = strlen(output);
    result->items_processed = 1;
    
    return ESP_OK;
}

/* ================================ 命令解析和执行 ================================ */

esp_err_t storage_shell_execute_command(const char* command_line, 
                                        storage_shell_result_t* result)
{
    if (!command_line || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 解析命令行
    char* args[STORAGE_SHELL_MAX_ARGS];
    int argc;
    
    esp_err_t ret = storage_shell_parse_args(command_line, &argc, args, STORAGE_SHELL_MAX_ARGS);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (argc == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* command = args[0];
    
    // 分发到具体命令处理函数
    if (strcmp(command, "ls") == 0) {
        storage_shell_ls_options_t options = {0};
        storage_shell_get_default_ls_options(&options);
        
        // 解析选项（简化处理）
        const char* path = (argc > 1) ? args[1] : NULL;
        return storage_shell_ls(path, &options, result);
    }
    else if (strcmp(command, "cat") == 0) {
        if (argc < 2) {
            result->result = ESP_ERR_INVALID_ARG;
            result->output = strdup("Usage: cat <file>");
            result->output_size = strlen(result->output);
            return ESP_ERR_INVALID_ARG;
        }
        return storage_shell_cat(args[1], result);
    }
    else if (strcmp(command, "touch") == 0) {
        if (argc < 2) {
            result->result = ESP_ERR_INVALID_ARG;
            result->output = strdup("Usage: touch <file>");
            result->output_size = strlen(result->output);
            return ESP_ERR_INVALID_ARG;
        }
        return storage_shell_touch(args[1], result);
    }
    else if (strcmp(command, "mkdir") == 0) {
        if (argc < 2) {
            result->result = ESP_ERR_INVALID_ARG;
            result->output = strdup("Usage: mkdir <directory>");
            result->output_size = strlen(result->output);
            return ESP_ERR_INVALID_ARG;
        }
        storage_shell_mkdir_options_t options = {0};
        return storage_shell_mkdir(args[1], &options, result);
    }
    else if (strcmp(command, "df") == 0) {
        bool human_readable = false;
        // 检查-h选项
        if (argc > 1 && strcmp(args[1], "-h") == 0) {
            human_readable = true;
        }
        return storage_shell_df(human_readable, result);
    }
    else {
        result->result = ESP_ERR_NOT_SUPPORTED;
        result->output = strdup("Command not supported");
        result->output_size = strlen(result->output);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t storage_shell_get_supported_commands(const char*** commands)
{
    if (!commands) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *commands = SUPPORTED_COMMANDS;
    return ESP_OK;
}

/* ================================ 辅助函数实现 ================================ */

void storage_shell_free_result(storage_shell_result_t* result)
{
    if (result && result->output) {
        free(result->output);
        result->output = NULL;
        result->output_size = 0;
    }
}

esp_err_t storage_shell_format_size(uint64_t bytes, char* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* units[] = {"B", "K", "M", "G", "T"};
    int unit = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        unit++;
    }
    
    if (unit == 0) {
        snprintf(buffer, buffer_size, "%.0f%s", size, units[unit]);
    } else {
        snprintf(buffer, buffer_size, "%.1f%s", size, units[unit]);
    }
    
    return ESP_OK;
}

esp_err_t storage_shell_format_time(time_t timestamp, char* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct tm* tm_info = localtime(&timestamp);
    strftime(buffer, buffer_size, "%b %d %H:%M", tm_info);
    
    return ESP_OK;
}

void storage_shell_get_default_ls_options(storage_shell_ls_options_t* options)
{
    if (!options) {
        return;
    }
    
    memset(options, 0, sizeof(storage_shell_ls_options_t));
}

void storage_shell_get_default_cp_options(storage_shell_cp_options_t* options)
{
    if (!options) {
        return;
    }
    
    memset(options, 0, sizeof(storage_shell_cp_options_t));
    options->preserve_timestamps = true;
}

void storage_shell_get_default_rm_options(storage_shell_rm_options_t* options)
{
    if (!options) {
        return;
    }
    
    memset(options, 0, sizeof(storage_shell_rm_options_t));
    options->preserve_root = true;
}

esp_err_t storage_shell_parse_args(const char* command_line, 
                                   int* argc, 
                                   char** argv, 
                                   int max_args)
{
    if (!command_line || !argc || !argv) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *argc = 0;
    
    // 创建命令行副本
    size_t len = strlen(command_line);
    char* line = malloc(len + 1);
    if (!line) {
        return ESP_ERR_NO_MEM;
    }
    strcpy(line, command_line);
    
    // 分割参数
    char* token = strtok(line, " \t\n");
    while (token && *argc < max_args) {
        argv[*argc] = strdup(token);
        if (!argv[*argc]) {
            // 清理已分配的内存
            for (int i = 0; i < *argc; i++) {
                free(argv[i]);
            }
            free(line);
            return ESP_ERR_NO_MEM;
        }
        (*argc)++;
        token = strtok(NULL, " \t\n");
    }
    
    free(line);
    
    return ESP_OK;
}

/* ================================ 内部函数实现 ================================ */

static esp_err_t format_file_list(const storage_dir_list_t* dir_list,
                                  const storage_shell_ls_options_t* options,
                                  char** output, size_t* output_size)
{
    if (!dir_list || !output || !output_size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t buffer_size = STORAGE_SHELL_MAX_OUTPUT_SIZE;
    char* buffer = malloc(buffer_size);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }
    
    size_t pos = 0;
    
    for (size_t i = 0; i < dir_list->count; i++) {
        const storage_file_info_t* file = &dir_list->files[i];
        
        // 检查缓冲区空间
        if (pos + 256 > buffer_size) {
            buffer_size *= 2;
            char* new_buffer = realloc(buffer, buffer_size);
            if (!new_buffer) {
                free(buffer);
                return ESP_ERR_NO_MEM;
            }
            buffer = new_buffer;
        }
        
        if (options && options->long_format) {
            // 长格式显示
            char perms[11];
            format_file_permissions(file->mode, perms, sizeof(perms));
            
            char size_str[32];
            if (options->human_readable) {
                storage_shell_format_size(file->size, size_str, sizeof(size_str));
            } else {
                snprintf(size_str, sizeof(size_str), "%zu", file->size);
            }
            
            char time_str[32];
            storage_shell_format_time(file->mtime, time_str, sizeof(time_str));
            
            pos += snprintf(buffer + pos, buffer_size - pos,
                           "%s %8s %s %s\n",
                           perms, size_str, time_str, file->name);
        } else {
            // 简单格式显示
            if (options && options->one_per_line) {
                pos += snprintf(buffer + pos, buffer_size - pos, "%s\n", file->name);
            } else {
                pos += snprintf(buffer + pos, buffer_size - pos, "%s  ", file->name);
            }
        }
    }
    
    // 如果不是每行一个，添加换行符
    if (!(options && (options->long_format || options->one_per_line)) && pos > 0) {
        if (pos < buffer_size - 1) {
            buffer[pos++] = '\n';
            buffer[pos] = '\0';
        }
    }
    
    *output = buffer;
    *output_size = pos;
    
    return ESP_OK;
}

static esp_err_t format_file_permissions(mode_t mode, char* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 11) {
        return ESP_ERR_INVALID_ARG;
    }
    
    buffer[0] = S_ISDIR(mode)  ? 'd' : 
                S_ISLNK(mode)  ? 'l' : 
                S_ISREG(mode)  ? '-' : '?';
                
    buffer[1] = (mode & S_IRUSR) ? 'r' : '-';
    buffer[2] = (mode & S_IWUSR) ? 'w' : '-';
    buffer[3] = (mode & S_IXUSR) ? 'x' : '-';
    buffer[4] = (mode & S_IRGRP) ? 'r' : '-';
    buffer[5] = (mode & S_IWGRP) ? 'w' : '-';
    buffer[6] = (mode & S_IXGRP) ? 'x' : '-';
    buffer[7] = (mode & S_IROTH) ? 'r' : '-';
    buffer[8] = (mode & S_IWOTH) ? 'w' : '-';
    buffer[9] = (mode & S_IXOTH) ? 'x' : '-';
    buffer[10] = '\0';
    
    return ESP_OK;
}

static int compare_files_by_name(const void* a, const void* b)
{
    const storage_file_info_t* file_a = (const storage_file_info_t*)a;
    const storage_file_info_t* file_b = (const storage_file_info_t*)b;
    
    return strcmp(file_a->name, file_b->name);
}

static int compare_files_by_time(const void* a, const void* b)
{
    const storage_file_info_t* file_a = (const storage_file_info_t*)a;
    const storage_file_info_t* file_b = (const storage_file_info_t*)b;
    
    if (file_a->mtime < file_b->mtime) return 1;
    if (file_a->mtime > file_b->mtime) return -1;
    return 0;
}

/* ================================ 缺失的Shell命令实现 ================================ */

esp_err_t storage_shell_cp(const char* src_path, 
                           const char* dst_path,
                           const storage_shell_cp_options_t* options,
                           storage_shell_result_t* result)
{
    if (!src_path || !dst_path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Copying: %s -> %s", src_path, dst_path);
    
    // 检查源文件是否存在
    if (!storage_fs_exists(src_path)) {
        result->result = ESP_ERR_NOT_FOUND;
        result->output = strdup("Source file not found");
        result->output_size = strlen(result->output);
        return ESP_ERR_NOT_FOUND;
    }
    
    storage_fs_copy_options_t copy_options = {0};
    if (options) {
        copy_options.recursive = options->recursive;
        copy_options.preserve_timestamps = options->preserve_timestamps;
        copy_options.overwrite_existing = !options->no_clobber;
    } else {
        storage_fs_get_default_copy_options(&copy_options);
        copy_options.overwrite_existing = true;
    }
    
    esp_err_t ret;
    if (storage_fs_is_directory(src_path)) {
        ret = storage_fs_copy_directory(src_path, dst_path, &copy_options);
    } else {
        ret = storage_fs_copy_file(src_path, dst_path, &copy_options);
    }
    
    if (ret == ESP_OK) {
        result->result = ESP_OK;
        result->output = strdup("File copied successfully");
        result->output_size = strlen(result->output);
        result->items_processed = 1;
    } else {
        result->result = ret;
        result->output = strdup("Failed to copy file");
        result->output_size = strlen(result->output);
    }
    
    return ret;
}

esp_err_t storage_shell_mv(const char* src_path, 
                           const char* dst_path,
                           storage_shell_result_t* result)
{
    if (!src_path || !dst_path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Moving: %s -> %s", src_path, dst_path);
    
    // 检查源文件是否存在
    if (!storage_fs_exists(src_path)) {
        result->result = ESP_ERR_NOT_FOUND;
        result->output = strdup("Source file not found");
        result->output_size = strlen(result->output);
        return ESP_ERR_NOT_FOUND;
    }
    
    esp_err_t ret = storage_fs_move_file(src_path, dst_path);
    
    if (ret == ESP_OK) {
        result->result = ESP_OK;
        result->output = strdup("File moved successfully");
        result->output_size = strlen(result->output);
        result->items_processed = 1;
    } else {
        result->result = ret;
        result->output = strdup("Failed to move file");
        result->output_size = strlen(result->output);
    }
    
    return ret;
}

esp_err_t storage_shell_rm(const char* path, 
                           const storage_shell_rm_options_t* options,
                           storage_shell_result_t* result)
{
    if (!path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Removing: %s", path);
    
    // 检查文件是否存在
    if (!storage_fs_exists(path)) {
        if (options && options->force) {
            result->result = ESP_OK;
            result->output = strdup("File does not exist (ignored)");
            result->output_size = strlen(result->output);
            return ESP_OK;
        }
        result->result = ESP_ERR_NOT_FOUND;
        result->output = strdup("File not found");
        result->output_size = strlen(result->output);
        return ESP_ERR_NOT_FOUND;
    }
    
    esp_err_t ret;
    if (storage_fs_is_directory(path)) {
        storage_fs_dir_options_t dir_options = {0};
        if (options && options->recursive) {
            dir_options.recursive = true;
        }
        ret = storage_fs_remove_directory(path, &dir_options);
    } else {
        ret = storage_fs_delete_file(path);
    }
    
    if (ret == ESP_OK) {
        result->result = ESP_OK;
        result->output = strdup("File removed successfully");
        result->output_size = strlen(result->output);
        result->items_processed = 1;
    } else {
        result->result = ret;
        result->output = strdup("Failed to remove file");
        result->output_size = strlen(result->output);
    }
    
    return ret;
}

esp_err_t storage_shell_rmdir(const char* path, storage_shell_result_t* result)
{
    if (!path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Removing directory: %s", path);
    
    // 检查是否为目录
    if (!storage_fs_is_directory(path)) {
        result->result = ESP_ERR_INVALID_ARG;
        result->output = strdup("Not a directory");
        result->output_size = strlen(result->output);
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_fs_dir_options_t options = {0};
    options.recursive = false; // rmdir只删除空目录
    
    esp_err_t ret = storage_fs_remove_directory(path, &options);
    
    if (ret == ESP_OK) {
        result->result = ESP_OK;
        result->output = strdup("Directory removed successfully");
        result->output_size = strlen(result->output);
        result->items_processed = 1;
    } else {
        result->result = ret;
        if (ret == ESP_ERR_INVALID_ARG) {
            result->output = strdup("Directory not empty");
        } else {
            result->output = strdup("Failed to remove directory");
        }
        result->output_size = strlen(result->output);
    }
    
    return ret;
}

esp_err_t storage_shell_stat(const char* path, storage_shell_result_t* result)
{
    if (!path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Getting file info: %s", path);
    
    storage_file_info_t file_info;
    esp_err_t ret = storage_fs_get_info(path, &file_info);
    
    if (ret != ESP_OK) {
        result->result = ret;
        result->output = strdup("Failed to get file info");
        result->output_size = strlen(result->output);
        return ret;
    }
    
    // 格式化输出
    char* output = malloc(512);
    if (!output) {
        result->result = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    
    char perms[11];
    format_file_permissions(file_info.mode, perms, sizeof(perms));
    
    char size_str[32];
    storage_shell_format_size(file_info.size, size_str, sizeof(size_str));
    
    char time_str[32];
    storage_shell_format_time(file_info.mtime, time_str, sizeof(time_str));
    
    snprintf(output, 512,
             "  File: %s\n"
             "  Size: %s (%zu bytes)\n"
             "  Type: %s\n"
             "Access: %s\n"
             "Modify: %s\n",
             file_info.name,
             size_str, file_info.size,
             file_info.is_directory ? "directory" : "regular file",
             perms,
             time_str);
    
    result->result = ESP_OK;
    result->output = output;
    result->output_size = strlen(output);
    result->items_processed = 1;
    
    return ESP_OK;
}

esp_err_t storage_shell_find(const char* root_path,
                             const storage_shell_find_options_t* options,
                             storage_shell_result_t* result)
{
    if (!root_path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Finding files in: %s", root_path);
    
    storage_fs_search_options_t search_options = {0};
    if (options) {
        search_options.name_pattern = options->name_pattern;
        search_options.case_sensitive = !options->case_insensitive;
        search_options.include_directories = (options->type == NULL || strcmp(options->type, "d") == 0);
        search_options.recursive = true;
        search_options.max_results = 1000; // 限制结果数量
    } else {
        search_options.include_directories = true;
        search_options.recursive = true;
        search_options.max_results = 1000;
    }
    
    storage_dir_list_t search_results;
    esp_err_t ret = storage_fs_search_files(root_path, &search_options, &search_results);
    
    if (ret != ESP_OK) {
        result->result = ret;
        result->output = strdup("Failed to search files");
        result->output_size = strlen(result->output);
        return ret;
    }
    
    // 格式化输出
    char* output = malloc(STORAGE_SHELL_MAX_OUTPUT_SIZE);
    if (!output) {
        storage_manager_free_dir_list(&search_results);
        result->result = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    
    size_t pos = 0;
    for (size_t i = 0; i < search_results.count && pos < STORAGE_SHELL_MAX_OUTPUT_SIZE - 100; i++) {
        pos += snprintf(output + pos, STORAGE_SHELL_MAX_OUTPUT_SIZE - pos,
                       "%s\n", search_results.files[i].name);
    }
    
    result->result = ESP_OK;
    result->output = output;
    result->output_size = pos;
    result->items_processed = search_results.count;
    
    storage_manager_free_dir_list(&search_results);
    
    return ESP_OK;
}

esp_err_t storage_shell_du(const char* path, 
                           bool human_readable,
                           storage_shell_result_t* result)
{
    if (!path || !result) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化结果
    memset(result, 0, sizeof(storage_shell_result_t));
    
    ESP_LOGD(TAG, "Calculating directory usage: %s", path);
    
    uint64_t total_size;
    uint32_t file_count, dir_count;
    
    esp_err_t ret = storage_fs_calculate_directory_size(path, &total_size, &file_count, &dir_count);
    
    if (ret != ESP_OK) {
        result->result = ret;
        result->output = strdup("Failed to calculate directory size");
        result->output_size = strlen(result->output);
        return ret;
    }
    
    char* output = malloc(256);
    if (!output) {
        result->result = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    
    if (human_readable) {
        char size_str[32];
        storage_shell_format_size(total_size, size_str, sizeof(size_str));
        snprintf(output, 256, "%s\t%s\n", size_str, path);
    } else {
        snprintf(output, 256, "%llu\t%s\n", total_size, path);
    }
    
    result->result = ESP_OK;
    result->output = output;
    result->output_size = strlen(output);
    result->items_processed = file_count + dir_count;
    
    return ESP_OK;
}