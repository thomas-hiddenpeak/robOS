/**
 * @file storage_shell_mode.c
 * @brief 存储管理器交互式Shell模式实现
 * 
 * 提供类似Linux shell的交互式存储操作环境，用户可以进入
 * 专门的sdcard提示符进行文件操作。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#include "storage_shell_mode.h"
#include "storage_manager.h"
#include "storage_shell.h"
#include "storage_fs.h"
#include "console_core.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================ 常量和宏定义 ================================ */

// static const char *TAG = "storage_shell_mode";

#define STORAGE_SHELL_PROMPT_MAX_LEN    (64)
#define STORAGE_SHELL_INPUT_MAX_LEN     (256)
#define STORAGE_SHELL_MAX_ARGS          (16)

/* ================================ 内部数据结构 ================================ */

/**
 * @brief 存储Shell模式上下文
 */
typedef struct {
    bool active;                                    /**< 是否处于存储Shell模式 */
    char current_path[STORAGE_MANAGER_MAX_PATH_LENGTH]; /**< 当前工作目录 */
    char prompt[STORAGE_SHELL_PROMPT_MAX_LEN];      /**< 提示符 */
} storage_shell_context_t;

/* ================================ 全局变量 ================================ */

static storage_shell_context_t s_shell_ctx = {
    .active = false,
    .current_path = STORAGE_MANAGER_DEFAULT_MOUNT_POINT,
    .prompt = "sdcard:/"
};

/* ================================ 内部函数声明 ================================ */

static void update_prompt(void);
static esp_err_t build_full_path(const char* relative_path, char* full_path, size_t full_path_size);
static esp_err_t parse_command_line(const char* line, int* argc, char** argv);
static esp_err_t execute_storage_command(int argc, char** argv);
static void print_storage_help(void);

/* ================================ 存储Shell命令处理函数 ================================ */

static esp_err_t cmd_ls(int argc, char** argv);
static esp_err_t cmd_cd(int argc, char** argv);
static esp_err_t cmd_pwd(int argc, char** argv);
static esp_err_t cmd_cat(int argc, char** argv);
static esp_err_t cmd_touch(int argc, char** argv);
static esp_err_t cmd_mkdir(int argc, char** argv);
static esp_err_t cmd_rm(int argc, char** argv);
static esp_err_t cmd_cp(int argc, char** argv);
static esp_err_t cmd_mv(int argc, char** argv);
static esp_err_t cmd_df(int argc, char** argv);
static esp_err_t cmd_du(int argc, char** argv);
static esp_err_t cmd_stat(int argc, char** argv);
static esp_err_t cmd_help(int argc, char** argv);
static esp_err_t cmd_exit(int argc, char** argv);

/* ================================ 命令表 ================================ */

typedef struct {
    const char* command;
    const char* help;
    esp_err_t (*func)(int argc, char** argv);
} storage_shell_cmd_t;

static const storage_shell_cmd_t storage_shell_commands[] = {
    {"ls",     "列出目录内容",              cmd_ls},
    {"cd",     "切换目录",                  cmd_cd},
    {"pwd",    "显示当前目录",              cmd_pwd},
    {"cat",    "显示文件内容",              cmd_cat},
    {"touch",  "创建空文件",                cmd_touch},
    {"mkdir",  "创建目录",                  cmd_mkdir},
    {"rm",     "删除文件或目录",            cmd_rm},
    {"cp",     "复制文件或目录",            cmd_cp},
    {"mv",     "移动/重命名文件或目录",     cmd_mv},
    {"df",     "显示磁盘空间使用情况",      cmd_df},
    {"du",     "显示目录空间使用情况",      cmd_du},
    {"stat",   "显示文件或目录详细信息",    cmd_stat},
    {"help",   "显示帮助信息",              cmd_help},
    {"exit",   "退出存储Shell模式",         cmd_exit},
};

#define STORAGE_SHELL_CMD_COUNT (sizeof(storage_shell_commands) / sizeof(storage_shell_cmd_t))

/* ================================ API 实现 ================================ */

esp_err_t storage_shell_mode_enter(void)
{
    if (s_shell_ctx.active) {
        console_printf("Already in storage shell mode\n");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查存储是否已挂载
    storage_state_t state = storage_manager_get_state();
    if (state != STORAGE_STATE_MOUNTED) {
        console_printf("Storage not mounted. Please mount storage first.\n");
        return ESP_ERR_INVALID_STATE;
    }
    
    s_shell_ctx.active = true;
    strcpy(s_shell_ctx.current_path, STORAGE_MANAGER_DEFAULT_MOUNT_POINT);
    update_prompt();
    
    console_printf("Entering storage shell mode. Type 'help' for commands, 'exit' to quit.\n");
    
    // 进入交互式循环
    char input_line[STORAGE_SHELL_INPUT_MAX_LEN];
    while (s_shell_ctx.active) {
        // 显示提示符并等待输入
        console_printf("%s> ", s_shell_ctx.prompt);
        
        esp_err_t ret = console_readline(input_line, sizeof(input_line), 300000); // 5分钟超时
        if (ret != ESP_OK) {
            console_printf("\nConsole input timeout, exiting shell mode.\n");
            break;  // 输入超时或错误
        }
        
        // 解析并执行命令
        int argc;
        char* argv[STORAGE_SHELL_MAX_ARGS];
        
        if (parse_command_line(input_line, &argc, argv) == ESP_OK) {
            if (argc > 0) {
                execute_storage_command(argc, argv);
            }
            // 如果是空命令（只按了回车），什么都不做，继续下一次循环
            // console_readline 应该已经处理了换行，这里不需要额外操作
        }
    }
    
    // 确保状态正确重置
    if (s_shell_ctx.active) {
        s_shell_ctx.active = false;
        console_printf("Storage shell mode exited.\n");
    }
    
    return ESP_OK;
}

esp_err_t storage_shell_mode_exit(void)
{
    if (!s_shell_ctx.active) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_shell_ctx.active = false;
    console_printf("Exiting storage shell mode.\n");
    
    return ESP_OK;
}

bool storage_shell_mode_is_active(void)
{
    return s_shell_ctx.active;
}

const char* storage_shell_mode_get_current_path(void)
{
    return s_shell_ctx.current_path;
}

/* ================================ 内部函数实现 ================================ */

static void update_prompt(void)
{
    if (strcmp(s_shell_ctx.current_path, STORAGE_MANAGER_DEFAULT_MOUNT_POINT) == 0) {
        snprintf(s_shell_ctx.prompt, sizeof(s_shell_ctx.prompt), "sdcard:/");
    } else {
        // 显示相对于挂载点的路径
        const char* relative_path = s_shell_ctx.current_path + strlen(STORAGE_MANAGER_DEFAULT_MOUNT_POINT);
        snprintf(s_shell_ctx.prompt, sizeof(s_shell_ctx.prompt), "sdcard:%.48s", relative_path);
    }
}

// 安全构建完整路径
static esp_err_t build_full_path(const char* relative_path, char* full_path, size_t full_path_size) {
    if (!relative_path || !full_path || full_path_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (relative_path[0] == '/') {
        // 绝对路径
        if (strlen(relative_path) >= full_path_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        strncpy(full_path, relative_path, full_path_size - 1);
    } else {
        // 相对路径
        size_t current_len = strlen(s_shell_ctx.current_path);
        size_t relative_len = strlen(relative_path);
        size_t required_len = current_len + 1 + relative_len + 1; // +1 for '/', +1 for '\0'
        
        if (required_len > full_path_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        
        if (strcmp(s_shell_ctx.current_path, STORAGE_MANAGER_DEFAULT_MOUNT_POINT) == 0) {
            snprintf(full_path, full_path_size, "%s/%s", STORAGE_MANAGER_DEFAULT_MOUNT_POINT, relative_path);
        } else {
            snprintf(full_path, full_path_size, "%s/%s", s_shell_ctx.current_path, relative_path);
        }
    }
    full_path[full_path_size - 1] = '\0';
    return ESP_OK;
}

static esp_err_t parse_command_line(const char* line, int* argc, char** argv)
{
    if (!line || !argc || !argv) {
        return ESP_ERR_INVALID_ARG;
    }
    
    static char cmd_buffer[STORAGE_SHELL_INPUT_MAX_LEN];
    strncpy(cmd_buffer, line, sizeof(cmd_buffer) - 1);
    cmd_buffer[sizeof(cmd_buffer) - 1] = '\0';
    
    *argc = 0;
    char* token = strtok(cmd_buffer, " \t\n\r");
    
    while (token && *argc < STORAGE_SHELL_MAX_ARGS - 1) {
        argv[*argc] = token;
        (*argc)++;
        token = strtok(NULL, " \t\n\r");
    }
    
    argv[*argc] = NULL;
    return ESP_OK;
}

static esp_err_t execute_storage_command(int argc, char** argv)
{
    if (argc == 0 || !argv[0]) {
        return ESP_OK;  // 空命令
    }
    
    // 查找命令
    for (size_t i = 0; i < STORAGE_SHELL_CMD_COUNT; i++) {
        if (strcmp(argv[0], storage_shell_commands[i].command) == 0) {
            return storage_shell_commands[i].func(argc, argv);
        }
    }
    
    console_printf("Unknown command: %s. Type 'help' for available commands.\n", argv[0]);
    return ESP_ERR_NOT_FOUND;
}

/* ================================ 存储Shell命令实现 ================================ */

static esp_err_t cmd_ls(int argc, char** argv)
{
    storage_shell_ls_options_t options = {
        .long_format = false,
        .show_all = false,
        .human_readable = true,
        .one_per_line = false
    };
    
    // 解析选项和路径
    const char* path = s_shell_ctx.current_path;  // 默认路径
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            // 这是一个选项
            if (strcmp(argv[i], "-l") == 0) {
                options.long_format = true;
            } else if (strcmp(argv[i], "-a") == 0) {
                options.show_all = true;
            } else if (strcmp(argv[i], "-h") == 0) {
                options.human_readable = true;
            } else if (strcmp(argv[i], "-1") == 0) {
                options.one_per_line = true;
            } else {
                console_printf("ls: unknown option '%s'\n", argv[i]);
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            // 这是路径参数
            path = argv[i];
        }
    }
    
    // 构建完整路径
    char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    if (build_full_path(path, full_path, sizeof(full_path)) != ESP_OK) {
        console_printf("ls: path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_ls(full_path, &options, &result);
    
    if (ret == ESP_OK) {
        if (result.output && result.output_size > 0) {
            console_printf("%s", (char*)result.output);
        } else {
            console_printf("(empty directory)\n");
        }
        
        if (result.output) {
            free(result.output);
        }
    } else {
        console_printf("ls: %s: %s\n", full_path, esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t cmd_cd(int argc, char** argv)
{
    const char* new_path;
    
    if (argc < 2) {
        new_path = "/";  // 默认回到根目录
    } else {
        new_path = argv[1];
    }
    
    char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    
    if (new_path[0] == '/') {
        // 绝对路径
        strncpy(full_path, new_path, sizeof(full_path) - 1);
    } else {
        // 相对路径
        if (strcmp(new_path, "..") == 0) {
            // 返回上级目录
            if (strcmp(s_shell_ctx.current_path, STORAGE_MANAGER_DEFAULT_MOUNT_POINT) == 0) {
                // 已经在根目录，无法再上级
                strcpy(full_path, STORAGE_MANAGER_DEFAULT_MOUNT_POINT);
            } else {
                char* last_slash = strrchr(s_shell_ctx.current_path, '/');
                if (last_slash && last_slash > s_shell_ctx.current_path + strlen(STORAGE_MANAGER_DEFAULT_MOUNT_POINT)) {
                    *last_slash = '\0';
                    strcpy(full_path, s_shell_ctx.current_path);
                    *last_slash = '/';  // 恢复原路径
                } else {
                    strcpy(full_path, STORAGE_MANAGER_DEFAULT_MOUNT_POINT);
                }
            }
        } else {
            // 普通相对路径
            if (build_full_path(new_path, full_path, sizeof(full_path)) != ESP_OK) {
                console_printf("cd: path too long\n");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    full_path[sizeof(full_path) - 1] = '\0';
    
    // 验证目录是否存在
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_stat(full_path, &result);
    
    if (ret == ESP_OK) {
        strncpy(s_shell_ctx.current_path, full_path, sizeof(s_shell_ctx.current_path) - 1);
        s_shell_ctx.current_path[sizeof(s_shell_ctx.current_path) - 1] = '\0';
        update_prompt();
        
        if (result.output) {
            free(result.output);
        }
    } else {
        console_printf("cd: %s: No such directory\n", full_path);
    }
    
    return ret;
}

static esp_err_t cmd_pwd(int argc, char** argv)
{
    console_printf("%s\n", s_shell_ctx.current_path);
    return ESP_OK;
}

static bool is_binary_data(const char* data, size_t size)
{
    // 简单的二进制检测：查找明显的二进制字符
    const size_t check_size = (size > 256) ? 256 : size;  // 只检查前256字节
    size_t binary_count = 0;
    
    for (size_t i = 0; i < check_size; i++) {
        unsigned char c = (unsigned char)data[i];
        // 检查明显的二进制字符（不包括常见的文本控制字符）
        if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) {
            binary_count++;
        }
    }
    
    // 如果超过5%的字符是二进制字符，认为是二进制文件
    return (binary_count * 100 / check_size) > 5;
}

static esp_err_t cmd_cat(int argc, char** argv)
{
    if (argc < 2) {
        console_printf("Usage: cat <file>\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* file_path = argv[1];
    char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    
    if (build_full_path(file_path, full_path, sizeof(full_path)) != ESP_OK) {
        console_printf("cat: path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_cat(full_path, &result);
    
    if (ret == ESP_OK) {
        if (result.output && result.output_size > 0) {
            char* content = (char*)result.output;
            
            // 检查是否是二进制文件
            if (is_binary_data(content, result.output_size)) {
                console_printf("cat: %s: binary file\n", full_path);
            } else {
                // 文本文件，简单可靠的输出方式
                const size_t CHUNK_SIZE = 256;  // 使用较小的块大小
                
                if (result.output_size > CHUNK_SIZE) {
                    // 大文件分块输出
                    for (size_t offset = 0; offset < result.output_size; offset += CHUNK_SIZE) {
                        size_t remaining = result.output_size - offset;
                        size_t current_chunk = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
                        
                        // 使用临时缓冲区确保null终止
                        char temp_buffer[CHUNK_SIZE + 1];
                        memcpy(temp_buffer, &content[offset], current_chunk);
                        temp_buffer[current_chunk] = '\0';
                        
                        console_printf("%s", temp_buffer);
                        
                        // 增加延时，避免串口缓冲区溢出
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                } else {
                    // 小文件直接输出
                    char* safe_content = malloc(result.output_size + 1);
                    if (safe_content) {
                        memcpy(safe_content, result.output, result.output_size);
                        safe_content[result.output_size] = '\0';
                        console_printf("%s", safe_content);
                        free(safe_content);
                    } else {
                        console_printf("cat: out of memory\n");
                    }
                }
            }
        } else {
            console_printf("cat: %s: empty file\n", full_path);
        }
        
        if (result.output) {
            free(result.output);
        }
    } else {
        console_printf("cat: %s: %s\n", full_path, esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t cmd_touch(int argc, char** argv)
{
    if (argc < 2) {
        console_printf("Usage: touch <file>\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* file_path = argv[1];
    char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    
    if (build_full_path(file_path, full_path, sizeof(full_path)) != ESP_OK) {
        console_printf("touch: path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_touch(full_path, &result);
    
    if (ret != ESP_OK) {
        console_printf("touch: %s: %s\n", full_path, esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t cmd_mkdir(int argc, char** argv)
{
    if (argc < 2) {
        console_printf("Usage: mkdir [-p] <directory>\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    bool create_parents = false;
    const char* dir_path = argv[1];
    
    // 检查 -p 选项
    if (argc > 2 && strcmp(argv[1], "-p") == 0) {
        create_parents = true;
        dir_path = argv[2];
    }
    
    char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    
    if (build_full_path(dir_path, full_path, sizeof(full_path)) != ESP_OK) {
        console_printf("mkdir: path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_mkdir_options_t options = {
        .create_parents = create_parents,
        .verbose = false,
        .mode = 0755
    };
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_mkdir(full_path, &options, &result);
    
    if (ret != ESP_OK) {
        console_printf("mkdir: %s: %s\n", full_path, esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t cmd_help(int argc, char** argv)
{
    print_storage_help();
    return ESP_OK;
}

static esp_err_t cmd_exit(int argc, char** argv)
{
    return storage_shell_mode_exit();
}

// 其他命令的简单实现
static esp_err_t cmd_rm(int argc, char** argv)
{
    if (argc < 2) {
        console_printf("Usage: rm [-r] [-f] [-i] [-v] <file|directory>...\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_rm_options_t options = {
        .recursive = false,
        .force = false,
        .interactive = false,
        .verbose = false,
        .preserve_root = true
    };
    
    // 解析选项和文件列表
    int path_start = 1;
    
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            // 这是一个选项
            for (int j = 1; argv[i][j] != '\0'; j++) {
                switch (argv[i][j]) {
                    case 'r':
                    case 'R':
                        options.recursive = true;
                        break;
                    case 'f':
                        options.force = true;
                        break;
                    case 'i':
                        options.interactive = true;
                        break;
                    case 'v':
                        options.verbose = true;
                        break;
                    default:
                        console_printf("rm: unknown option '-%c'\n", argv[i][j]);
                        return ESP_ERR_INVALID_ARG;
                }
            }
            path_start = i + 1;
        } else {
            // 这是文件路径，开始处理
            break;
        }
    }
    
    if (path_start >= argc) {
        console_printf("rm: missing file operand\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 处理每个文件/目录
    for (int i = path_start; i < argc; i++) {
        char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
        if (build_full_path(argv[i], full_path, sizeof(full_path)) != ESP_OK) {
            console_printf("rm: path too long: %s\n", argv[i]);
            continue;
        }
        
        // 交互式确认
        if (options.interactive) {
            console_printf("rm: remove '%s'? (y/N): ", full_path);
            char response[10];
            if (console_readline(response, sizeof(response), 5000) != ESP_OK) {
                continue;
            }
            if (response[0] != 'y' && response[0] != 'Y') {
                continue;
            }
        }
        
        storage_shell_result_t result = {0};
        esp_err_t ret = storage_shell_rm(full_path, &options, &result);
        
        if (ret != ESP_OK) {
            if (!options.force) {
                // 提供更有用的错误消息
                if (ret == ESP_ERR_INVALID_ARG && storage_fs_is_directory(full_path)) {
                    console_printf("rm: cannot remove '%s': Directory not empty (use -r to remove recursively)\n", full_path);
                } else if (ret == ESP_ERR_NOT_FOUND) {
                    console_printf("rm: cannot remove '%s': No such file or directory\n", full_path);
                } else {
                    console_printf("rm: cannot remove '%s': %s\n", 
                                 full_path, result.output ? result.output : esp_err_to_name(ret));
                }
            }
        } else if (options.verbose) {
            console_printf("removed '%s'\n", full_path);
        }
        
        // 清理结果输出
        if (result.output) {
            free(result.output);
        }
    }
    
    return ESP_OK;
}

static esp_err_t cmd_cp(int argc, char** argv)
{
    if (argc < 3) {
        console_printf("Usage: cp [-r] [-f] [-i] [-v] [-p] [-n] <source> <destination>\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_cp_options_t options = {
        .recursive = false,
        .preserve_timestamps = false,
        .force = false,
        .interactive = false,
        .verbose = false,
        .no_clobber = false
    };
    
    // 解析选项和路径参数
    int path_start = 1;
    
    for (int i = 1; i < argc - 2; i++) {
        if (argv[i][0] == '-') {
            // 这是一个选项
            for (int j = 1; argv[i][j] != '\0'; j++) {
                switch (argv[i][j]) {
                    case 'r':
                    case 'R':
                        options.recursive = true;
                        break;
                    case 'f':
                        options.force = true;
                        break;
                    case 'i':
                        options.interactive = true;
                        break;
                    case 'v':
                        options.verbose = true;
                        break;
                    case 'p':
                        options.preserve_timestamps = true;
                        break;
                    case 'n':
                        options.no_clobber = true;
                        break;
                    default:
                        console_printf("cp: unknown option '-%c'\n", argv[i][j]);
                        return ESP_ERR_INVALID_ARG;
                }
            }
            path_start = i + 1;
        } else {
            // 这是路径参数，开始处理
            break;
        }
    }
    
    if (path_start + 1 >= argc) {
        console_printf("cp: missing source or destination operand\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* src_path = argv[path_start];
    const char* dst_path = argv[path_start + 1];
    
    // 构建完整路径
    char full_src_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    char full_dst_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    
    if (build_full_path(src_path, full_src_path, sizeof(full_src_path)) != ESP_OK) {
        console_printf("cp: source path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (build_full_path(dst_path, full_dst_path, sizeof(full_dst_path)) != ESP_OK) {
        console_printf("cp: destination path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 交互式确认覆盖
    if (options.interactive && storage_fs_exists(full_dst_path)) {
        console_printf("cp: overwrite '%s'? (y/N): ", full_dst_path);
        char response[10];
        if (console_readline(response, sizeof(response), 5000) != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        if (response[0] != 'y' && response[0] != 'Y') {
            console_printf("cp: not overwritten\n");
            return ESP_OK;
        }
    }
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_cp(full_src_path, full_dst_path, &options, &result);
    
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            console_printf("cp: cannot stat '%s': No such file or directory\n", full_src_path);
        } else if (ret == ESP_ERR_INVALID_ARG && storage_fs_is_directory(full_src_path)) {
            console_printf("cp: -r not specified; omitting directory '%s'\n", full_src_path);
        } else {
            console_printf("cp: cannot copy '%s' to '%s': %s\n", 
                         full_src_path, full_dst_path,
                         result.output ? result.output : esp_err_to_name(ret));
        }
    } else if (options.verbose) {
        console_printf("'%s' -> '%s'\n", full_src_path, full_dst_path);
    }
    
    // 清理结果输出
    if (result.output) {
        free(result.output);
    }
    
    return ret;
}

static esp_err_t cmd_mv(int argc, char** argv)
{
    if (argc < 3) {
        console_printf("Usage: mv <source> <destination>\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    console_printf("mv command not fully implemented yet\n");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t cmd_df(int argc, char** argv)
{
    storage_stats_t stats;
    esp_err_t ret = storage_manager_get_stats(&stats);
    
    if (ret == ESP_OK) {
        console_printf("Filesystem     Size      Used      Avail     Use%%\n");
        console_printf("/sdcard        %llu    %llu    %llu    %d%%\n",
                       stats.total_bytes / 1024,
                       stats.used_bytes / 1024,
                       stats.free_bytes / 1024,
                       (int)((stats.used_bytes * 100) / stats.total_bytes));
    } else {
        console_printf("df: Failed to get storage stats: %s\n", esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t cmd_du(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : s_shell_ctx.current_path;
    bool human_readable = true;
    
    // 检查选项
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            human_readable = true;
        } else if (strcmp(argv[i], "-b") == 0) {
            human_readable = false;
        }
    }
    
    // 构建完整路径
    char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    if (build_full_path(path, full_path, sizeof(full_path)) != ESP_OK) {
        console_printf("du: path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_du(full_path, human_readable, &result);
    
    if (ret == ESP_OK) {
        if (result.output && result.output_size > 0) {
            console_printf("%s", (char*)result.output);
        } else {
            console_printf("0\t%s\n", full_path);
        }
        
        if (result.output) {
            free(result.output);
        }
    } else {
        console_printf("du: %s: %s\n", full_path, esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t cmd_stat(int argc, char** argv)
{
    if (argc < 2) {
        console_printf("Usage: stat <file|directory>\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* path = argv[1];
    
    // 构建完整路径
    char full_path[STORAGE_MANAGER_MAX_PATH_LENGTH];
    if (build_full_path(path, full_path, sizeof(full_path)) != ESP_OK) {
        console_printf("stat: path too long\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_stat(full_path, &result);
    
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_FOUND) {
            console_printf("stat: cannot stat '%s': No such file or directory\n", full_path);
        } else {
            console_printf("stat: cannot stat '%s': %s\n", 
                         full_path, result.output ? result.output : esp_err_to_name(ret));
        }
    } else {
        // 显示统计信息
        if (result.output) {
            console_printf("%s", result.output);
        }
    }
    
    // 清理结果输出
    if (result.output) {
        free(result.output);
    }
    
    return ret;
}

static void print_storage_help(void)
{
    console_printf("Storage Shell Commands:\n");
    console_printf("=======================\n");
    
    for (size_t i = 0; i < STORAGE_SHELL_CMD_COUNT; i++) {
        console_printf("  %-8s - %s\n", 
                       storage_shell_commands[i].command,
                       storage_shell_commands[i].help);
    }
    
    console_printf("\nTip: Use absolute paths (starting with /) or relative paths\n");
    console_printf("Current directory: %s\n", s_shell_ctx.current_path);
}