/**
 * @file storage_shell_mode.h
 * @brief 存储管理器交互式Shell模式头文件
 * 
 * 提供进入专门的SD卡Shell环境的API接口。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#ifndef STORAGE_SHELL_MODE_H
#define STORAGE_SHELL_MODE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ API 函数 ================================ */

/**
 * @brief 进入存储Shell模式
 * 
 * 进入交互式的存储操作环境，用户可以使用类似Linux shell的命令
 * 进行文件和目录操作。
 * 
 * @return esp_err_t 
 *         - ESP_OK: 成功进入并退出存储Shell模式
 *         - ESP_ERR_INVALID_STATE: 存储未挂载或已在Shell模式中
 */
esp_err_t storage_shell_mode_enter(void);

/**
 * @brief 退出存储Shell模式
 * 
 * @return esp_err_t 
 *         - ESP_OK: 成功退出
 *         - ESP_ERR_INVALID_STATE: 当前不在Shell模式中
 */
esp_err_t storage_shell_mode_exit(void);

/**
 * @brief 检查是否处于存储Shell模式
 * 
 * @return true 当前处于存储Shell模式
 * @return false 当前不在存储Shell模式
 */
bool storage_shell_mode_is_active(void);

/**
 * @brief 获取当前工作目录
 * 
 * @return const char* 当前工作目录路径
 */
const char* storage_shell_mode_get_current_path(void);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_SHELL_MODE_H