/**
 * @file storage_console.c
 * @brief Storage Manager Console Commands Implementation
 * 
 * @version 1.0.0
 * @date 2025-01-27
 */

#include "storage_console.h"
#include "storage_manager.h"
#include "storage_shell.h"
#include "storage_shell_mode.h"
#include "console_core.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ============================================================================
 * Constants and Macros
 * ============================================================================ */

static const char *TAG = "STORAGE_CONSOLE";

#define MAX_CONSOLE_OUTPUT_SIZE     (4096)
#define DEFAULT_HEAD_TAIL_LINES     (10)
#define STORAGE_CONSOLE_MAX_PATH_LENGTH (512)

/* ============================================================================
 * Static Variables
 * ============================================================================ */

static char s_current_directory[STORAGE_CONSOLE_MAX_PATH_LENGTH] = "/";
static bool s_commands_registered = false;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static esp_err_t parse_ls_options(int argc, char **argv, char **path, bool *long_format, bool *show_all, bool *human_readable);
static esp_err_t parse_du_options(int argc, char **argv, char **path, bool *human_readable, bool *summary_only);
static esp_err_t parse_mkdir_options(int argc, char **argv, char **path, bool *create_parents);
static esp_err_t parse_cp_options(int argc, char **argv, char **source, char **dest, bool *recursive);
static esp_err_t parse_rm_options(int argc, char **argv, char **path, bool *recursive, bool *force);
static esp_err_t parse_find_options(int argc, char **argv, char **path, char **name_pattern, char **type_filter);
static esp_err_t parse_head_tail_options(int argc, char **argv, char **path, int *num_lines);
static void format_size_human_readable(size_t size, char *buffer, size_t buffer_size);
static const char* resolve_path(const char *path);
static esp_err_t change_directory(const char *new_dir);

/* ============================================================================
 * Command Registration
 * ============================================================================ */

static const console_cmd_t storage_commands[] = {
    // Mount/Unmount commands
    {
        .command = "mount",
        .help = "Mount TF card storage device",
        .hint = "mount",
        .func = storage_console_cmd_mount,
        .min_args = 0,
        .max_args = 0
    },
    {
        .command = "unmount",
        .help = "Unmount TF card storage device",
        .hint = "unmount",
        .func = storage_console_cmd_unmount,
        .min_args = 0,
        .max_args = 0
    },
    {
        .command = "format",
        .help = "Format TF card storage device",
        .hint = "format [--force]",
        .func = storage_console_cmd_format,
        .min_args = 0,
        .max_args = 1
    },
    
    // Directory listing commands
    {
        .command = "ls",
        .help = "List directory contents",
        .hint = "ls [path] [-l] [-a] [-h]",
        .func = storage_console_cmd_ls,
        .min_args = 0,
        .max_args = 6
    },
    {
        .command = "pwd",
        .help = "Print working directory",
        .hint = "pwd",
        .func = storage_console_cmd_pwd,
        .min_args = 0,
        .max_args = 0
    },
    {
        .command = "cd",
        .help = "Change directory",
        .hint = "cd [path]",
        .func = storage_console_cmd_cd,
        .min_args = 0,
        .max_args = 1
    },
    
    // File content commands
    {
        .command = "cat",
        .help = "Display file contents",
        .hint = "cat <file_path>",
        .func = storage_console_cmd_cat,
        .min_args = 1,
        .max_args = 1
    },
    {
        .command = "head",
        .help = "Display first lines of file",
        .hint = "head [-n lines] <file_path>",
        .func = storage_console_cmd_head,
        .min_args = 1,
        .max_args = 3
    },
    {
        .command = "tail",
        .help = "Display last lines of file",
        .hint = "tail [-n lines] <file_path>",
        .func = storage_console_cmd_tail,
        .min_args = 1,
        .max_args = 3
    },
    
    // File operations commands
    {
        .command = "touch",
        .help = "Create empty file",
        .hint = "touch <file_path>",
        .func = storage_console_cmd_touch,
        .min_args = 1,
        .max_args = 1
    },
    {
        .command = "mkdir",
        .help = "Create directory",
        .hint = "mkdir [-p] <dir_path>",
        .func = storage_console_cmd_mkdir,
        .min_args = 1,
        .max_args = 2
    },
    {
        .command = "cp",
        .help = "Copy files/directories",
        .hint = "cp [-r] <source> <destination>",
        .func = storage_console_cmd_cp,
        .min_args = 2,
        .max_args = 3
    },
    {
        .command = "mv",
        .help = "Move/rename files/directories",
        .hint = "mv <source> <destination>",
        .func = storage_console_cmd_mv,
        .min_args = 2,
        .max_args = 2
    },
    {
        .command = "rm",
        .help = "Remove files/directories",
        .hint = "rm [-r] [-f] <path>",
        .func = storage_console_cmd_rm,
        .min_args = 1,
        .max_args = 3
    },
    {
        .command = "rmdir",
        .help = "Remove empty directory",
        .hint = "rmdir <dir_path>",
        .func = storage_console_cmd_rmdir,
        .min_args = 1,
        .max_args = 1
    },
    
    // Information commands
    {
        .command = "stat",
        .help = "Display file/directory statistics",
        .hint = "stat <path>",
        .func = storage_console_cmd_stat,
        .min_args = 1,
        .max_args = 1
    },
    {
        .command = "du",
        .help = "Display disk usage",
        .hint = "du [-h] [-s] [path]",
        .func = storage_console_cmd_du,
        .min_args = 0,
        .max_args = 3
    },
    {
        .command = "df",
        .help = "Display filesystem information",
        .hint = "df [-h]",
        .func = storage_console_cmd_df,
        .min_args = 0,
        .max_args = 1
    },
    
    // Search commands
    {
        .command = "find",
        .help = "Find files/directories",
        .hint = "find <path> [-name pattern] [-type f|d]",
        .func = storage_console_cmd_find,
        .min_args = 1,
        .max_args = 5
    },
    
    // Status commands
    {
        .command = "storage-status",
        .help = "Display storage status",
        .hint = "storage-status",
        .func = storage_console_cmd_status,
        .min_args = 0,
        .max_args = 0
    },
    {
        .command = "storage-info",
        .help = "Display storage information",
        .hint = "storage-info",
        .func = storage_console_cmd_info,
        .min_args = 0,
        .max_args = 0
    },
    
    // Interactive shell mode
    {
        .command = "sdcard",
        .help = "Enter interactive SD card shell mode",
        .hint = "sdcard",
        .func = storage_console_cmd_sdcard,
        .min_args = 0,
        .max_args = 0
    }
};

#define STORAGE_COMMANDS_COUNT (sizeof(storage_commands) / sizeof(console_cmd_t))

/* ============================================================================
 * Public Functions
 * ============================================================================ */

esp_err_t storage_console_register_commands(void)
{
    if (s_commands_registered) {
        ESP_LOGW(TAG, "Storage commands already registered");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Registering %d storage console commands", STORAGE_COMMANDS_COUNT);
    
    for (size_t i = 0; i < STORAGE_COMMANDS_COUNT; i++) {
        esp_err_t ret = console_register_command(&storage_commands[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register command '%s': %s", 
                     storage_commands[i].command, esp_err_to_name(ret));
            return ret;
        }
    }
    
    s_commands_registered = true;
    ESP_LOGI(TAG, "All storage commands registered successfully");
    
    return ESP_OK;
}

esp_err_t storage_console_unregister_commands(void)
{
    if (!s_commands_registered) {
        return ESP_OK;
    }
    
    for (size_t i = 0; i < STORAGE_COMMANDS_COUNT; i++) {
        console_unregister_command(storage_commands[i].command);
    }
    
    s_commands_registered = false;
    ESP_LOGI(TAG, "All storage commands unregistered");
    
    return ESP_OK;
}

/* ============================================================================
 * Mount/Unmount Commands
 * ============================================================================ */

esp_err_t storage_console_cmd_mount(int argc, char **argv)
{
    console_printf("Mounting TF card...\n");
    
    esp_err_t ret = storage_manager_mount_async(NULL, NULL);
    if (ret != ESP_OK) {
        console_printf("Mount failed: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    console_printf("Mount operation initiated successfully\n");
    return ESP_OK;
}

esp_err_t storage_console_cmd_unmount(int argc, char **argv)
{
    console_printf("Unmounting TF card...\n");
    
    esp_err_t ret = storage_manager_unmount_async(NULL, NULL);
    if (ret != ESP_OK) {
        console_printf("Unmount failed: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    console_printf("Unmount operation initiated successfully\n");
    return ESP_OK;
}

esp_err_t storage_console_cmd_format(int argc, char **argv)
{
    bool force = false;
    
    // Parse options
    if (argc > 0 && strcmp(argv[0], "--force") == 0) {
        force = true;
    }
    
    if (!force) {
        console_printf("WARNING: This will erase all data on the TF card!\n");
        console_printf("Use 'format --force' to confirm.\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    console_printf("Formatting TF card...\n");
    
    esp_err_t ret = storage_manager_format_async(NULL, NULL);
    if (ret != ESP_OK) {
        console_printf("Format failed: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    console_printf("Format operation initiated successfully\n");
    return ESP_OK;
}

/* ============================================================================
 * Directory Listing Commands
 * ============================================================================ */

esp_err_t storage_console_cmd_ls(int argc, char **argv)
{
    char *path = NULL;
    bool long_format = false;
    bool show_all = false;
    bool human_readable = false;
    
    esp_err_t ret = parse_ls_options(argc, argv, &path, &long_format, &show_all, &human_readable);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Use current directory if no path specified
    const char *target_path = path ? resolve_path(path) : s_current_directory;
    
    // Create ls options
    storage_shell_ls_options_t ls_options = {
        .long_format = long_format,
        .show_all = show_all,
        .human_readable = human_readable
    };
    
    storage_shell_result_t result = {0};
    ret = storage_shell_ls(target_path, &ls_options, &result);
    if (ret != ESP_OK) {
        console_printf("ls: %s: %s\n", target_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (!result.output || result.output_size == 0) {
        console_printf("(empty directory)\n");
        if (result.output) {
            free(result.output);
        }
        return ESP_OK;
    }
    
    // Display result (assume it's a formatted string)
    console_printf("%s", (char*)result.output);
    
    if (result.output) {
        free(result.output);
    }
    return ESP_OK;
}

esp_err_t storage_console_cmd_pwd(int argc, char **argv)
{
    console_printf("%s\n", s_current_directory);
    return ESP_OK;
}

esp_err_t storage_console_cmd_cd(int argc, char **argv)
{
    const char *new_dir = (argc > 0) ? argv[0] : "/";
    return change_directory(new_dir);
}

/* ============================================================================
 * File Content Commands
 * ============================================================================ */

esp_err_t storage_console_cmd_cat(int argc, char **argv)
{
    const char *file_path = resolve_path(argv[0]);
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_cat(file_path, &result);
    if (ret != ESP_OK) {
        console_printf("cat: %s: %s\n", file_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output && result.output_size > 0) {
        // Print content in chunks to avoid console buffer overflow
        const size_t chunk_size = 512;
        char *content = (char*)result.output;
        for (size_t i = 0; i < result.output_size; i += chunk_size) {
            size_t remaining = result.output_size - i;
            size_t to_print = (remaining > chunk_size) ? chunk_size : remaining;
            
            char temp = content[i + to_print];
            content[i + to_print] = '\0';
            console_printf("%s", &content[i]);
            content[i + to_print] = temp;
        }
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_head(int argc, char **argv)
{
    char *file_path = NULL;
    int num_lines = DEFAULT_HEAD_TAIL_LINES;
    
    esp_err_t ret = parse_head_tail_options(argc, argv, &file_path, &num_lines);
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *resolved_path = resolve_path(file_path);
    
    storage_shell_result_t result = {0};
    ret = storage_shell_cat(resolved_path, &result);
    if (ret != ESP_OK) {
        console_printf("head: %s: %s\n", resolved_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output && result.output_size > 0) {
        int lines_printed = 0;
        char *content = (char*)result.output;
        for (size_t i = 0; i < result.output_size && lines_printed < num_lines; i++) {
            console_printf("%c", content[i]);
            if (content[i] == '\n') {
                lines_printed++;
            }
        }
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_tail(int argc, char **argv)
{
    char *file_path = NULL;
    int num_lines = DEFAULT_HEAD_TAIL_LINES;
    
    esp_err_t ret = parse_head_tail_options(argc, argv, &file_path, &num_lines);
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *resolved_path = resolve_path(file_path);
    
    storage_shell_result_t result = {0};
    ret = storage_shell_cat(resolved_path, &result);
    if (ret != ESP_OK) {
        console_printf("tail: %s: %s\n", resolved_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output && result.output_size > 0) {
        char *content = (char*)result.output;
        // Count total lines first
        int total_lines = 0;
        for (size_t i = 0; i < result.output_size; i++) {
            if (content[i] == '\n') {
                total_lines++;
            }
        }
        
        // Find starting position for last N lines
        int lines_to_skip = (total_lines > num_lines) ? (total_lines - num_lines) : 0;
        int lines_skipped = 0;
        size_t start_pos = 0;
        
        for (size_t i = 0; i < result.output_size && lines_skipped < lines_to_skip; i++) {
            if (content[i] == '\n') {
                lines_skipped++;
                if (lines_skipped == lines_to_skip) {
                    start_pos = i + 1;
                    break;
                }
            }
        }
        
        // Print from start_pos to end
        for (size_t i = start_pos; i < result.output_size; i++) {
            console_printf("%c", content[i]);
        }
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

/* ============================================================================
 * File Operations Commands
 * ============================================================================ */

esp_err_t storage_console_cmd_touch(int argc, char **argv)
{
    const char *file_path = resolve_path(argv[0]);
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_touch(file_path, &result);
    if (ret != ESP_OK) {
        console_printf("touch: %s: %s\n", file_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_mkdir(int argc, char **argv)
{
    char *dir_path = NULL;
    bool create_parents = false;
    
    esp_err_t ret = parse_mkdir_options(argc, argv, &dir_path, &create_parents);
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *resolved_path = resolve_path(dir_path);
    
    storage_shell_mkdir_options_t mkdir_options = {
        .create_parents = create_parents
    };
    
    storage_shell_result_t result = {0};
    ret = storage_shell_mkdir(resolved_path, &mkdir_options, &result);
    if (ret != ESP_OK) {
        console_printf("mkdir: %s: %s\n", resolved_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_cp(int argc, char **argv)
{
    char *source = NULL;
    char *dest = NULL;
    bool recursive = false;
    
    esp_err_t ret = parse_cp_options(argc, argv, &source, &dest, &recursive);
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *resolved_source = resolve_path(source);
    const char *resolved_dest = resolve_path(dest);
    
    storage_shell_cp_options_t cp_options = {
        .recursive = recursive
    };
    
    storage_shell_result_t result = {0};
    ret = storage_shell_cp(resolved_source, resolved_dest, &cp_options, &result);
    if (ret != ESP_OK) {
        console_printf("cp: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_mv(int argc, char **argv)
{
    const char *source = resolve_path(argv[0]);
    const char *dest = resolve_path(argv[1]);
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_mv(source, dest, &result);
    if (ret != ESP_OK) {
        console_printf("mv: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_rm(int argc, char **argv)
{
    char *path = NULL;
    bool recursive = false;
    bool force = false;
    
    esp_err_t ret = parse_rm_options(argc, argv, &path, &recursive, &force);
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *resolved_path = resolve_path(path);
    
    storage_shell_rm_options_t rm_options = {
        .recursive = recursive,
        .force = force
    };
    
    storage_shell_result_t result = {0};
    ret = storage_shell_rm(resolved_path, &rm_options, &result);
    if (ret != ESP_OK) {
        console_printf("rm: %s: %s\n", resolved_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_rmdir(int argc, char **argv)
{
    const char *dir_path = resolve_path(argv[0]);
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_rmdir(dir_path, &result);
    if (ret != ESP_OK) {
        console_printf("rmdir: %s: %s\n", dir_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        free(result.output);
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Information Commands
 * ============================================================================ */

esp_err_t storage_console_cmd_stat(int argc, char **argv)
{
    const char *path = resolve_path(argv[0]);
    
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_stat(path, &result);
    if (ret != ESP_OK) {
        console_printf("stat: %s: %s\n", path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        console_printf("%s", (char*)result.output);
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_du(int argc, char **argv)
{
    char *path = NULL;
    bool human_readable = false;
    bool summary_only = false;
    
    esp_err_t ret = parse_du_options(argc, argv, &path, &human_readable, &summary_only);
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *target_path = path ? resolve_path(path) : s_current_directory;
    
    storage_shell_result_t result = {0};
    ret = storage_shell_du(target_path, human_readable, &result);
    if (ret != ESP_OK) {
        console_printf("du: %s: %s\n", target_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        console_printf("%s", (char*)result.output);
        free(result.output);
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_df(int argc, char **argv)
{
    bool human_readable = false;
    
    if (argc > 0 && strcmp(argv[0], "-h") == 0) {
        human_readable = true;
    }
    
    storage_stats_t stats;
    esp_err_t ret = storage_manager_get_stats(&stats);
    if (ret != ESP_OK) {
        console_printf("df: Failed to get storage stats: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    // Check if storage is mounted by checking if we have valid stats
    if (stats.total_bytes == 0) {
        console_printf("df: Storage not mounted\n");
        return ESP_ERR_INVALID_STATE;
    }
    
    char total_str[32], used_str[32], free_str[32];
    
    if (human_readable) {
        format_size_human_readable(stats.total_bytes, total_str, sizeof(total_str));
        format_size_human_readable(stats.used_bytes, used_str, sizeof(used_str));
        format_size_human_readable(stats.free_bytes, free_str, sizeof(free_str));
    } else {
        snprintf(total_str, sizeof(total_str), "%llu", stats.total_bytes);
        snprintf(used_str, sizeof(used_str), "%llu", stats.used_bytes);
        snprintf(free_str, sizeof(free_str), "%llu", stats.free_bytes);
    }
    
    int usage_percent = (stats.total_bytes > 0) ? 
        (int)((stats.used_bytes * 100) / stats.total_bytes) : 0;
    
    console_printf("Filesystem    Size    Used   Avail Use%% Mounted on\n");
    console_printf("TF Card    %8s %8s %8s %3d%% /\n", 
                   total_str, used_str, free_str, usage_percent);
    
    return ESP_OK;
}

/* ============================================================================
 * Search Commands
 * ============================================================================ */

esp_err_t storage_console_cmd_find(int argc, char **argv)
{
    char *path = NULL;
    char *name_pattern = NULL;
    char *type_filter = NULL;
    
    esp_err_t ret = parse_find_options(argc, argv, &path, &name_pattern, &type_filter);
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *search_path = resolve_path(path);
    
    storage_shell_find_options_t find_options = {
        .name_pattern = name_pattern,
        .type = type_filter
    };
    
    storage_shell_result_t result = {0};
    ret = storage_shell_find(search_path, &find_options, &result);
    if (ret != ESP_OK) {
        console_printf("find: %s: %s\n", search_path, esp_err_to_name(ret));
        return ret;
    }
    
    if (result.output) {
        console_printf("%s", (char*)result.output);
        free(result.output);
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Status Commands
 * ============================================================================ */

esp_err_t storage_console_cmd_status(int argc, char **argv)
{
    bool initialized = storage_manager_is_initialized();
    storage_state_t state = storage_manager_get_state();
    
    const char* state_str = "Unknown";
    switch (state) {
        case STORAGE_STATE_UNINITIALIZED: state_str = "Uninitialized"; break;
        case STORAGE_STATE_INITIALIZED: state_str = "Initialized"; break;
        case STORAGE_STATE_MOUNTED: state_str = "Mounted"; break;
        case STORAGE_STATE_UNMOUNTED: state_str = "Unmounted"; break;
        case STORAGE_STATE_ERROR: state_str = "Error"; break;
    }
    
    console_printf("Storage Status:\n");
    console_printf("  Initialized: %s\n", initialized ? "Yes" : "No");
    console_printf("  State: %s\n", state_str);
    
    // Try to get stats if mounted
    if (state == STORAGE_STATE_MOUNTED) {
        storage_stats_t stats;
        esp_err_t ret = storage_manager_get_stats(&stats);
        if (ret == ESP_OK) {
            console_printf("  Total Files: %lu\n", stats.total_files);
            console_printf("  Total Directories: %lu\n", stats.total_directories);
        }
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_info(int argc, char **argv)
{
    storage_state_t state = storage_manager_get_state();
    bool mounted = (state == STORAGE_STATE_MOUNTED);
    
    console_printf("Storage Information:\n");
    console_printf("  Mounted: %s\n", mounted ? "Yes" : "No");
    
    if (mounted) {
        storage_stats_t stats;
        esp_err_t ret = storage_manager_get_stats(&stats);
        if (ret == ESP_OK) {
            char total_str[32], used_str[32], free_str[32];
            format_size_human_readable(stats.total_bytes, total_str, sizeof(total_str));
            format_size_human_readable(stats.used_bytes, used_str, sizeof(used_str));
            format_size_human_readable(stats.free_bytes, free_str, sizeof(free_str));
            
            console_printf("  Total Size: %s (%llu bytes)\n", total_str, stats.total_bytes);
            console_printf("  Used Size: %s (%llu bytes)\n", used_str, stats.used_bytes);
            console_printf("  Free Size: %s (%llu bytes)\n", free_str, stats.free_bytes);
            console_printf("  Total Files: %lu\n", stats.total_files);
            console_printf("  Total Directories: %lu\n", stats.total_directories);
            console_printf("  Mount Point: /sdcard\n");
        }
    }
    
    return ESP_OK;
}

esp_err_t storage_console_cmd_sdcard(int argc, char **argv)
{
    console_printf("Entering SD card interactive shell mode...\n");
    
    esp_err_t ret = storage_shell_mode_enter();
    if (ret != ESP_OK) {
        console_printf("Failed to enter SD card shell mode: %s\n", esp_err_to_name(ret));
    }
    
    return ret;
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static esp_err_t parse_ls_options(int argc, char **argv, char **path, bool *long_format, bool *show_all, bool *human_readable)
{
    *path = NULL;
    *long_format = false;
    *show_all = false;
    *human_readable = false;
    
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            // Parse options
            for (int j = 1; argv[i][j] != '\0'; j++) {
                switch (argv[i][j]) {
                    case 'l':
                        *long_format = true;
                        break;
                    case 'a':
                        *show_all = true;
                        break;
                    case 'h':
                        *human_readable = true;
                        break;
                    default:
                        console_printf("ls: invalid option -- '%c'\n", argv[i][j]);
                        return ESP_ERR_INVALID_ARG;
                }
            }
        } else {
            // This is a path
            if (*path == NULL) {
                *path = argv[i];
            } else {
                console_printf("ls: too many arguments\n");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    return ESP_OK;
}

static esp_err_t parse_du_options(int argc, char **argv, char **path, bool *human_readable, bool *summary_only)
{
    *path = NULL;
    *human_readable = false;
    *summary_only = false;
    
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            // Parse options
            for (int j = 1; argv[i][j] != '\0'; j++) {
                switch (argv[i][j]) {
                    case 'h':
                        *human_readable = true;
                        break;
                    case 's':
                        *summary_only = true;
                        break;
                    default:
                        console_printf("du: invalid option -- '%c'\n", argv[i][j]);
                        return ESP_ERR_INVALID_ARG;
                }
            }
        } else {
            // This is a path
            if (*path == NULL) {
                *path = argv[i];
            } else {
                console_printf("du: too many arguments\n");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    return ESP_OK;
}

static esp_err_t parse_mkdir_options(int argc, char **argv, char **path, bool *create_parents)
{
    *path = NULL;
    *create_parents = false;
    
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            *create_parents = true;
        } else if (argv[i][0] == '-') {
            console_printf("mkdir: invalid option -- '%s'\n", argv[i]);
            return ESP_ERR_INVALID_ARG;
        } else {
            if (*path == NULL) {
                *path = argv[i];
            } else {
                console_printf("mkdir: too many arguments\n");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    if (*path == NULL) {
        console_printf("mkdir: missing operand\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

static esp_err_t parse_cp_options(int argc, char **argv, char **source, char **dest, bool *recursive)
{
    *source = NULL;
    *dest = NULL;
    *recursive = false;
    
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-R") == 0) {
            *recursive = true;
        } else if (argv[i][0] == '-') {
            console_printf("cp: invalid option -- '%s'\n", argv[i]);
            return ESP_ERR_INVALID_ARG;
        } else {
            if (*source == NULL) {
                *source = argv[i];
            } else if (*dest == NULL) {
                *dest = argv[i];
            } else {
                console_printf("cp: too many arguments\n");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    if (*source == NULL || *dest == NULL) {
        console_printf("cp: missing file operand\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

static esp_err_t parse_rm_options(int argc, char **argv, char **path, bool *recursive, bool *force)
{
    *path = NULL;
    *recursive = false;
    *force = false;
    
    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            // Parse options
            for (int j = 1; argv[i][j] != '\0'; j++) {
                switch (argv[i][j]) {
                    case 'r':
                    case 'R':
                        *recursive = true;
                        break;
                    case 'f':
                        *force = true;
                        break;
                    default:
                        console_printf("rm: invalid option -- '%c'\n", argv[i][j]);
                        return ESP_ERR_INVALID_ARG;
                }
            }
        } else {
            if (*path == NULL) {
                *path = argv[i];
            } else {
                console_printf("rm: too many arguments\n");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    if (*path == NULL) {
        console_printf("rm: missing operand\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

static esp_err_t parse_find_options(int argc, char **argv, char **path, char **name_pattern, char **type_filter)
{
    *path = NULL;
    *name_pattern = NULL;
    *type_filter = NULL;
    
    if (argc < 1) {
        console_printf("find: missing path argument\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    *path = argv[0];
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0) {
            if (i + 1 >= argc) {
                console_printf("find: option '-name' requires an argument\n");
                return ESP_ERR_INVALID_ARG;
            }
            *name_pattern = argv[i + 1];
            i++; // Skip next argument
        } else if (strcmp(argv[i], "-type") == 0) {
            if (i + 1 >= argc) {
                console_printf("find: option '-type' requires an argument\n");
                return ESP_ERR_INVALID_ARG;
            }
            *type_filter = argv[i + 1];
            i++; // Skip next argument
        } else {
            console_printf("find: invalid option -- '%s'\n", argv[i]);
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    return ESP_OK;
}

static esp_err_t parse_head_tail_options(int argc, char **argv, char **path, int *num_lines)
{
    *path = NULL;
    *num_lines = DEFAULT_HEAD_TAIL_LINES;
    
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                console_printf("option '-n' requires an argument\n");
                return ESP_ERR_INVALID_ARG;
            }
            *num_lines = atoi(argv[i + 1]);
            if (*num_lines <= 0) {
                console_printf("invalid number of lines: '%s'\n", argv[i + 1]);
                return ESP_ERR_INVALID_ARG;
            }
            i++; // Skip next argument
        } else if (argv[i][0] == '-') {
            console_printf("invalid option -- '%s'\n", argv[i]);
            return ESP_ERR_INVALID_ARG;
        } else {
            if (*path == NULL) {
                *path = argv[i];
            } else {
                console_printf("too many arguments\n");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    
    if (*path == NULL) {
        console_printf("missing file operand\n");
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

static void format_size_human_readable(size_t size, char *buffer, size_t buffer_size)
{
    const char *units[] = {"B", "K", "M", "G", "T"};
    const int unit_count = sizeof(units) / sizeof(units[0]);
    
    double size_double = (double)size;
    int unit_index = 0;
    
    while (size_double >= 1024.0 && unit_index < unit_count - 1) {
        size_double /= 1024.0;
        unit_index++;
    }
    
    if (unit_index == 0) {
        snprintf(buffer, buffer_size, "%u%s", (unsigned int)size, units[unit_index]);
    } else {
        snprintf(buffer, buffer_size, "%.1f%s", size_double, units[unit_index]);
    }
}

static const char* resolve_path(const char *path)
{
    static char resolved[STORAGE_CONSOLE_MAX_PATH_LENGTH];
    
    if (!path) {
        return s_current_directory;
    }
    
    // If path is absolute, return as-is
    if (path[0] == '/') {
        strncpy(resolved, path, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
        return resolved;
    }
    
    // Relative path - combine with current directory
    if (strcmp(s_current_directory, "/") == 0) {
        strncpy(resolved, "/", sizeof(resolved));
        strncat(resolved, path, sizeof(resolved) - strlen(resolved) - 1);
    } else {
        strncpy(resolved, s_current_directory, sizeof(resolved) - 1);
        resolved[sizeof(resolved) - 1] = '\0';
        strncat(resolved, "/", sizeof(resolved) - strlen(resolved) - 1);
        strncat(resolved, path, sizeof(resolved) - strlen(resolved) - 1);
    }
    
    return resolved;
}

static esp_err_t change_directory(const char *new_dir)
{
    const char *target_dir = resolve_path(new_dir);
    
    // Check if directory exists using stat command
    storage_shell_result_t result = {0};
    esp_err_t ret = storage_shell_stat(target_dir, &result);
    if (ret != ESP_OK) {
        console_printf("cd: %s: No such file or directory\n", target_dir);
        return ret;
    }
    
    // For simplicity, assume stat succeeded means it's a valid directory
    // The actual directory check would be in the stat result
    if (result.output) {
        free(result.output);
    }
    
    // Update current directory
    strncpy(s_current_directory, target_dir, sizeof(s_current_directory) - 1);
    s_current_directory[sizeof(s_current_directory) - 1] = '\0';
    
    return ESP_OK;
}