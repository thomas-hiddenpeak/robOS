/**
 * @file storage_device.h
 * @brief 存储设备管理 - TF卡硬件操作抽象层
 * 
 * 提供TF卡设备的硬件级操作，包括初始化、挂载、卸载、
 * 热插拔检测等底层功能。
 * 
 * @author robOS Team
 * @date 2025-09-29
 * @version 1.0.0
 */

#ifndef STORAGE_DEVICE_H
#define STORAGE_DEVICE_H

#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "storage_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ 常量定义 ================================ */

#define STORAGE_DEVICE_MAX_RETRY_COUNT      (3)
#define STORAGE_DEVICE_TIMEOUT_MS           (5000)
#define STORAGE_DEVICE_HOTSWAP_DEBOUNCE_MS  (500)

/* ================================ 数据结构 ================================ */

/**
 * @brief 设备配置结构
 */
typedef struct {
    sdmmc_host_t host_config;          /**< SDMMC主机配置 */
    sdmmc_slot_config_t slot_config;   /**< 插槽配置 */
    int card_detect_pin;               /**< 卡检测引脚 (-1表示不使用) */
    bool enable_pullup;                /**< 是否启用上拉电阻 */
    uint32_t max_freq_khz;             /**< 最大频率(kHz) */
} storage_device_config_t;

/**
 * @brief 设备状态信息
 */
typedef struct {
    bool card_present;                 /**< 卡片是否存在 */
    bool card_initialized;             /**< 卡片是否已初始化 */
    bool mounted;                      /**< 是否已挂载 */
    sdmmc_card_t* card;                /**< 卡片信息 */
    uint64_t capacity_bytes;           /**< 容量(字节) */
    uint32_t sector_size;              /**< 扇区大小 */
    char cid_name[8];                  /**< 卡片名称 */
    uint32_t serial_number;            /**< 序列号 */
} storage_device_info_t;

/* ================================ API 函数 ================================ */

/**
 * @brief 初始化存储设备
 * 
 * @param config 设备配置
 * @return esp_err_t 
 *         - ESP_OK: 初始化成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_NO_MEM: 内存不足
 *         - ESP_FAIL: 初始化失败
 */
esp_err_t storage_device_init(const storage_device_config_t* config);

/**
 * @brief 反初始化存储设备
 * 
 * @return esp_err_t 
 *         - ESP_OK: 反初始化成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 */
esp_err_t storage_device_deinit(void);

/**
 * @brief 检测卡片是否存在
 * 
 * @return true 卡片存在
 * @return false 卡片不存在
 */
bool storage_device_is_card_present(void);

/**
 * @brief 挂载TF卡
 * 
 * @param mount_point 挂载点路径
 * @param format_if_mount_failed 挂载失败时是否格式化
 * @return esp_err_t 
 *         - ESP_OK: 挂载成功
 *         - ESP_ERR_NOT_FOUND: 未找到卡片
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_FAIL: 挂载失败
 */
esp_err_t storage_device_mount(const char* mount_point, bool format_if_mount_failed);

/**
 * @brief 卸载TF卡
 * 
 * @param mount_point 挂载点路径
 * @return esp_err_t 
 *         - ESP_OK: 卸载成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_FAIL: 卸载失败
 */
esp_err_t storage_device_unmount(const char* mount_point);

/**
 * @brief 格式化TF卡
 * 
 * @return esp_err_t 
 *         - ESP_OK: 格式化成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_FAIL: 格式化失败
 */
esp_err_t storage_device_format(void);

/**
 * @brief 获取设备信息
 * 
 * @param info 输出的设备信息
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_INVALID_STATE: 设备未初始化
 */
esp_err_t storage_device_get_info(storage_device_info_t* info);

/**
 * @brief 启用热插拔检测
 * 
 * @return esp_err_t 
 *         - ESP_OK: 启用成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 */
esp_err_t storage_device_enable_hotswap(void);

/**
 * @brief 禁用热插拔检测
 * 
 * @return esp_err_t 
 *         - ESP_OK: 禁用成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 */
esp_err_t storage_device_disable_hotswap(void);

/**
 * @brief 获取默认设备配置
 * 
 * @param config 输出的配置结构
 */
void storage_device_get_default_config(storage_device_config_t* config);

/**
 * @brief 检查设备是否已初始化
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
bool storage_device_is_initialized(void);

/**
 * @brief 获取卡片容量信息
 * 
 * @param total_bytes 总容量输出
 * @param free_bytes 可用容量输出
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_INVALID_ARG: 无效参数
 *         - ESP_ERR_INVALID_STATE: 设备未挂载
 */
esp_err_t storage_device_get_capacity(uint64_t* total_bytes, uint64_t* free_bytes);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_DEVICE_H