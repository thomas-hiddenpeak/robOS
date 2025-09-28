# config_manager 组件说明

## 组件简介
config_manager 是 robOS 的统一配置管理组件，负责所有系统参数的 NVS 存储、读取、类型转换和自动提交。

- 支持多命名空间隔离
- 支持多种数据类型（int/float/bool/string/blob）
- 自动提交和批量操作
- 线程安全，支持多任务访问
- 错误码与 ESP-IDF 标准一致

## 主要 API

```c
esp_err_t config_manager_init(const config_manager_config_t *config);
esp_err_t config_manager_deinit(void);
bool config_manager_is_initialized(void);
esp_err_t config_manager_set(const char *namespace, const char *key, config_type_t type, const void *value, size_t size);
esp_err_t config_manager_get(const char *namespace, const char *key, config_type_t type, void *value, size_t size);
esp_err_t config_manager_delete(const char *namespace, const char *key);
bool config_manager_exists(const char *namespace, const char *key);
esp_err_t config_manager_commit(void);
esp_err_t config_manager_clear_namespace(const char *namespace);
```

## 典型用法

```c
config_manager_init(NULL);
uint32_t val = 123;
config_manager_set("sys", "param1", CONFIG_TYPE_UINT32, &val, sizeof(val));
config_manager_get("sys", "param1", CONFIG_TYPE_UINT32, &val, sizeof(val));
config_manager_delete("sys", "param1");
config_manager_deinit();
```

## 版本与兼容性
- 支持配置结构体版本号字段，便于未来升级
- 推荐所有配置结构体加 version 字段

## 自动提交机制
- 支持后台自动提交任务，防止频繁写入 flash
- deinit 时自动安全退出任务

## 单元测试
- 详见 tests/test_config_manager 目录，所有核心功能均有覆盖

## 更新记录
- 2025-09-28：修复任务删除崩溃，完善 exists 检查所有类型，单元测试全部通过
