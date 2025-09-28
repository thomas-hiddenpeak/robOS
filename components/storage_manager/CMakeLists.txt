# Storage Manager Component CMakeLists.txt
# 存储管理组件构建配置

idf_component_register(
    SRCS
        "src/storage_manager.c"
        "src/storage_device.c"
        "src/storage_fs.c"
        "src/storage_shell.c"
        "src/storage_shell_mode.c"
        "storage_console.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        "esp_common"
        "sdmmc"
        "fatfs"
        "vfs"
        "driver"
        "event_manager"
        "hardware_hal"
        "console_core"
    PRIV_REQUIRES
        "esp_timer"
        "freertos"
)

# 组件级别的编译选项
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)

# 定义组件版本
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    STORAGE_MANAGER_VERSION_MAJOR=1
    STORAGE_MANAGER_VERSION_MINOR=0
    STORAGE_MANAGER_VERSION_PATCH=0
)