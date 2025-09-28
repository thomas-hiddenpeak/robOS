/**
 * @file storage_console.h
 * @brief Storage Manager Console Commands Interface
 * 
 * This file provides console command definitions for the storage manager,
 * integrating storage operations with the robOS console system.
 * 
 * Features:
 * - Linux-style storage commands (ls, cat, cp, mv, rm, etc.)
 * - Mount/unmount operations
 * - Storage status and information commands
 * - Async operation support with progress reporting
 * 
 * @version 1.0.0
 * @date 2025-01-27
 */

#ifndef STORAGE_CONSOLE_H
#define STORAGE_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants and Macros
 * ============================================================================ */

#define STORAGE_CONSOLE_MAX_PATH_LENGTH     (512)   ///< Maximum path length for console commands
#define STORAGE_CONSOLE_MAX_OUTPUT_SIZE     (2048)  ///< Maximum output buffer size

/* ============================================================================
 * Public Functions
 * ============================================================================ */

/**
 * @brief Register all storage console commands
 * 
 * This function registers all storage-related commands with the console core.
 * Should be called after both console_core and storage_manager are initialized.
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_register_commands(void);

/**
 * @brief Unregister all storage console commands
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_unregister_commands(void);

/* ============================================================================
 * Storage Mount/Unmount Commands
 * ============================================================================ */

/**
 * @brief Mount storage device command
 * 
 * Usage: mount
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_mount(int argc, char **argv);

/**
 * @brief Unmount storage device command
 * 
 * Usage: unmount
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_unmount(int argc, char **argv);

/**
 * @brief Format storage device command
 * 
 * Usage: format [--force]
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_format(int argc, char **argv);

/* ============================================================================
 * File and Directory Listing Commands
 * ============================================================================ */

/**
 * @brief List directory contents command
 * 
 * Usage: ls [path] [-l] [-a] [-h]
 * Options:
 *   -l: Long format (detailed)
 *   -a: Show all files (including hidden)
 *   -h: Human readable sizes
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_ls(int argc, char **argv);

/**
 * @brief Print working directory command
 * 
 * Usage: pwd
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_pwd(int argc, char **argv);

/**
 * @brief Change directory command
 * 
 * Usage: cd [path]
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_cd(int argc, char **argv);

/* ============================================================================
 * File Content Commands
 * ============================================================================ */

/**
 * @brief Display file contents command
 * 
 * Usage: cat <file_path>
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_cat(int argc, char **argv);

/**
 * @brief Display first lines of file command
 * 
 * Usage: head [-n lines] <file_path>
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_head(int argc, char **argv);

/**
 * @brief Display last lines of file command
 * 
 * Usage: tail [-n lines] <file_path>
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_tail(int argc, char **argv);

/* ============================================================================
 * File and Directory Operations
 * ============================================================================ */

/**
 * @brief Create file command
 * 
 * Usage: touch <file_path>
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_touch(int argc, char **argv);

/**
 * @brief Create directory command
 * 
 * Usage: mkdir [-p] <dir_path>
 * Options:
 *   -p: Create parent directories as needed
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_mkdir(int argc, char **argv);

/**
 * @brief Copy files/directories command
 * 
 * Usage: cp [-r] <source> <destination>
 * Options:
 *   -r: Copy directories recursively
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_cp(int argc, char **argv);

/**
 * @brief Move/rename files/directories command
 * 
 * Usage: mv <source> <destination>
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_mv(int argc, char **argv);

/**
 * @brief Remove files command
 * 
 * Usage: rm [-r] [-f] <path>
 * Options:
 *   -r: Remove directories recursively
 *   -f: Force removal without confirmation
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_rm(int argc, char **argv);

/**
 * @brief Remove directory command
 * 
 * Usage: rmdir <dir_path>
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_rmdir(int argc, char **argv);

/* ============================================================================
 * File Information Commands
 * ============================================================================ */

/**
 * @brief Display file/directory statistics command
 * 
 * Usage: stat <path>
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_stat(int argc, char **argv);

/**
 * @brief Display disk usage command
 * 
 * Usage: du [-h] [-s] [path]
 * Options:
 *   -h: Human readable sizes
 *   -s: Summary only
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_du(int argc, char **argv);

/**
 * @brief Display filesystem information command
 * 
 * Usage: df [-h]
 * Options:
 *   -h: Human readable sizes
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_df(int argc, char **argv);

/* ============================================================================
 * Search and Find Commands
 * ============================================================================ */

/**
 * @brief Find files command
 * 
 * Usage: find <path> [-name pattern] [-type f|d]
 * Options:
 *   -name: File name pattern (supports wildcards)
 *   -type: File type (f=file, d=directory)
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_find(int argc, char **argv);

/* ============================================================================
 * Storage Status Commands
 * ============================================================================ */

/**
 * @brief Display storage status command
 * 
 * Usage: storage-status
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_status(int argc, char **argv);

/**
 * @brief Display storage information command
 * 
 * Usage: storage-info
 * 
 * @param argc Number of arguments
 * @param argv Array of argument strings
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_console_cmd_info(int argc, char **argv);

/**
 * @brief Enter interactive SD card shell mode
 * 
 * @param argc Argument count
 * @param argv Argument values
 * @return esp_err_t Operation result
 */
esp_err_t storage_console_cmd_sdcard(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_CONSOLE_H