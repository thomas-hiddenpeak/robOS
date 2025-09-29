/**
 * @file storage_device.c
 * @brief 存储设备管理实现
 * 
 * 实现TF卡设备的硬件级操作，包括初始化、挂载、卸载、
 * 热插拔检测等底层功能。
 * 
 * @author robOS Team  
 * @date 2025-09-29
 * @version 1.0.0
 */

#include "storage_device.h"
#include "hardware_hal.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <sys/stat.h>

/* ================================ 常量和宏定义 ================================ */

static const char* TAG = "storage_device";

/* ESP32S3的SDMMC引脚配置（参考rm01-esp32s3-bsp项目） */
#define SDMMC_D0_PIN            (4)   // DAT0
#define SDMMC_D1_PIN            (5)   // DAT1
#define SDMMC_D2_PIN            (6)   // DAT2
#define SDMMC_D3_PIN            (7)   // DAT3
#define SDMMC_CMD_PIN           (15)  // CMD
#define SDMMC_CLK_PIN           (16)  // CLK

/* ================================ 内部数据结构 ================================ */

/**
 * @brief 设备上下文
 */
typedef struct {
    bool initialized;                          /**< 初始化状态 */
    bool mounted;                             /**< 挂载状态 */
    storage_device_config_t config;           /**< 设备配置 */
    sdmmc_card_t* card;                       /**< SD卡信息 */
    char mount_point[64];                     /**< 当前挂载点 */
    esp_vfs_fat_mount_config_t mount_config;  /**< 挂载配置 */
} storage_device_context_t;

/* ================================ 全局变量 ================================ */

static storage_device_context_t s_device_ctx = {0};

/* ================================ API 实现 ================================ */

esp_err_t storage_device_init(const storage_device_config_t* config)
{
    if (s_device_ctx.initialized) {
        ESP_LOGW(TAG, "Storage device already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid config parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing storage device...");
    
    // 保存配置
    memcpy(&s_device_ctx.config, config, sizeof(storage_device_config_t));
    
    // 配置SDMMC主机（使用BSP项目验证的配置）
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;  // 40MHz高速模式
    
    // 配置插槽（4位模式）
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // 4位数据线
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    
    // 设置引脚（使用BSP项目验证的引脚配置）
    slot_config.clk = SDMMC_CLK_PIN;   // CLK - GPIO16
    slot_config.cmd = SDMMC_CMD_PIN;   // CMD - GPIO15
    slot_config.d0 = SDMMC_D0_PIN;     // DAT0 - GPIO4
    slot_config.d1 = SDMMC_D1_PIN;     // DAT1 - GPIO5
    slot_config.d2 = SDMMC_D2_PIN;     // DAT2 - GPIO6
    slot_config.d3 = SDMMC_D3_PIN;     // DAT3 - GPIO7
    
    s_device_ctx.config.host_config = host;
    s_device_ctx.config.slot_config = slot_config;
    
    s_device_ctx.initialized = true;
    
    ESP_LOGI(TAG, "Storage device initialized successfully");
    
    return ESP_OK;
}

esp_err_t storage_device_deinit(void)
{
    if (!s_device_ctx.initialized) {
        ESP_LOGW(TAG, "Storage device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing storage device...");
    
    // 如果已挂载，先卸载
    if (s_device_ctx.mounted) {
        storage_device_unmount(s_device_ctx.mount_point);
    }
    
    s_device_ctx.initialized = false;
    memset(&s_device_ctx, 0, sizeof(s_device_ctx));
    
    ESP_LOGI(TAG, "Storage device deinitialized");
    
    return ESP_OK;
}

bool storage_device_is_card_present(void)
{
    // 如果已经挂载成功，说明卡存在
    if (s_device_ctx.mounted && s_device_ctx.card) {
        return true;
    }
    
    // 在未挂载状态下，我们不能轻易检测卡片存在
    // 因为需要初始化SDMMC主机，这可能会产生副作用
    // 最安全的方法是让挂载操作本身来检测卡片
    // 这样可以避免重复的主机初始化和潜在的竞态条件
    return false;
}

esp_err_t storage_device_mount(const char* mount_point, bool format_if_mount_failed)
{
    if (!s_device_ctx.initialized) {
        ESP_LOGE(TAG, "Device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_device_ctx.mounted) {
        ESP_LOGW(TAG, "Device already mounted at %s", s_device_ctx.mount_point);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!mount_point) {
        ESP_LOGE(TAG, "Invalid mount point");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Mounting storage device to %s", mount_point);
    
    // 配置挂载选项（针对文件写入优化的参数）
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = 20,                   // 增加同时打开文件数量以支持更多操作
        .allocation_unit_size = 8 * 1024,  // 8KB分配单元，更适合小文件操作
        .disk_status_check_enable = true   // 启用磁盘状态检查确保文件系统一致性
    };
    
    ESP_LOGI(TAG, "Attempting to mount SD card...");
    
    // 挂载FAT文件系统
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, 
                                            &s_device_ctx.config.host_config,
                                            &s_device_ctx.config.slot_config, 
                                            &mount_config, 
                                            &s_device_ctx.card);
    
    if (ret != ESP_OK) {
        // 根据错误类型提供更友好的错误信息
        switch (ret) {
            case ESP_ERR_TIMEOUT:
                ESP_LOGW(TAG, "SD card removed during mount");
                break;
            case ESP_ERR_INVALID_STATE:
                ESP_LOGW(TAG, "SD card slot already in use");
                break;
            case ESP_ERR_NOT_FOUND:
                ESP_LOGW(TAG, "SD card not responding or unsupported");
                break;
            case ESP_ERR_NOT_SUPPORTED:
                ESP_LOGW(TAG, "SD card format not supported");
                break;
            case ESP_FAIL:
                ESP_LOGW(TAG, "SD card initialization failed");
                break;
            default:
                ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(ret));
                break;
        }
        return ret;
    }
    
    // 保存挂载信息
    strcpy(s_device_ctx.mount_point, mount_point);
    s_device_ctx.mount_config = mount_config;
    s_device_ctx.mounted = true;
    
    // 打印卡片信息
    if (s_device_ctx.card) {
        ESP_LOGI(TAG, "SD card mounted successfully");
        ESP_LOGI(TAG, "Name: %s", s_device_ctx.card->cid.name);
        ESP_LOGI(TAG, "Type: %s", (s_device_ctx.card->ocr & (1<<30)) ? "SDHC/SDXC" : "SDSC"); // Bit 30 is SDHC/SDXC flag
        ESP_LOGI(TAG, "Speed: %s", (s_device_ctx.card->csd.tr_speed > 25000000) ? "high speed" : "default speed");
        ESP_LOGI(TAG, "Size: %lluMB", ((uint64_t) s_device_ctx.card->csd.capacity) * s_device_ctx.card->csd.sector_size / (1024 * 1024));
    }
    
    return ESP_OK;
}

esp_err_t storage_device_unmount(const char* mount_point)
{
    if (!s_device_ctx.initialized || !s_device_ctx.mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Unmounting storage device...");
    
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(mount_point ? mount_point : s_device_ctx.mount_point, s_device_ctx.card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount storage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_device_ctx.mounted = false;
    ESP_LOGI(TAG, "Storage device unmounted successfully");
    return ESP_OK;
}

esp_err_t storage_device_format(void)
{
    if (!s_device_ctx.initialized) {
        ESP_LOGE(TAG, "Device not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Formatting storage device...");
    
    // 如果已挂载，先卸载
    if (s_device_ctx.mounted) {
        esp_err_t ret = storage_device_unmount(s_device_ctx.mount_point);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // 这里应该实现格式化逻辑
    // ESP-IDF没有直接的格式化API，通常通过重新挂载时设置format_if_mount_failed来实现
    ESP_LOGW(TAG, "Format functionality not implemented yet");
    
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_device_get_info(storage_device_info_t* info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_device_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    memset(info, 0, sizeof(storage_device_info_t));
    
    info->card_present = storage_device_is_card_present();
    info->card_initialized = s_device_ctx.initialized;
    info->mounted = s_device_ctx.mounted;
    
    if (s_device_ctx.card) {
        info->card = s_device_ctx.card;
        info->capacity_bytes = ((uint64_t)s_device_ctx.card->csd.capacity) * s_device_ctx.card->csd.sector_size;
        info->sector_size = s_device_ctx.card->csd.sector_size;
        strncpy(info->cid_name, s_device_ctx.card->cid.name, sizeof(info->cid_name) - 1);
        info->serial_number = s_device_ctx.card->cid.serial;
    }
    
    return ESP_OK;
}

esp_err_t storage_device_get_capacity(uint64_t* total_bytes, uint64_t* free_bytes)
{
    if (!total_bytes || !free_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_device_ctx.mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 获取文件系统统计信息
    FATFS* fs;
    DWORD free_clusters;
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    
    if (res != FR_OK) {
        ESP_LOGE(TAG, "Failed to get filesystem info: %d", res);
        return ESP_FAIL;
    }
    
    uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors = free_clusters * fs->csize;
    
    *total_bytes = total_sectors * fs->ssize;
    *free_bytes = free_sectors * fs->ssize;
    
    return ESP_OK;
}

void storage_device_get_default_config(storage_device_config_t* config)
{
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(storage_device_config_t));
    
    config->card_detect_pin = -1; // 不使用卡检测引脚
    config->enable_pullup = true;
    config->max_freq_khz = SDMMC_FREQ_DEFAULT; // 20MHz
}

bool storage_device_is_initialized(void)
{
    return s_device_ctx.initialized;
}

esp_err_t storage_device_enable_hotswap(void)
{
    // TODO: 实现热插拔检测
    ESP_LOGW(TAG, "Hot swap detection not implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_device_disable_hotswap(void)
{
    // TODO: 禁用热插拔检测
    ESP_LOGW(TAG, "Hot swap detection not implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}
