# fan_controller 组件说明

## 组件简介
fan_controller 是 robOS 的风扇控制核心，支持多路 PWM 风扇、手动/自动模式、温度控制、配置持久化。

- 支持多风扇实例
- 支持硬件配置与运行参数分离（fan_X_hw / fan_X_full）
- 配置持久化，重启自动恢复
- 控制台命令集成，支持风扇参数查询/设置/保存/加载
- 兼容 config_manager 统一配置接口

## 主要 API

```c
esp_err_t fan_controller_init(const fan_controller_config_t *config);
esp_err_t fan_controller_deinit(void);
esp_err_t fan_controller_set_speed(uint8_t fan_id, uint8_t percent);
esp_err_t fan_controller_set_mode(uint8_t fan_id, fan_mode_t mode);
esp_err_t fan_controller_save_config(uint8_t fan_id);
esp_err_t fan_controller_load_config(uint8_t fan_id);
```

## 配置结构
- fan_hw_config_t：硬件参数（GPIO、PWM通道等）
- fan_full_config_t：完整参数（硬件+运行状态+版本号）

## 控制台命令
- fan gpio ...
- fan config save/load
- fan config show

## 典型用法
```c
fan_controller_init(NULL);
fan_controller_set_speed(0, 80);
fan_controller_set_mode(0, FAN_MODE_AUTO);
fan_controller_save_config(0);
fan_controller_load_config(0);
fan_controller_deinit();
```

## 持久化机制
- 所有风扇配置均通过 config_manager 存储到 NVS
- 支持硬件配置与完整运行参数分层存储

## 更新记录
- 2025-09-28：完善 config 保存/加载，支持运行参数持久化，命令结构优化
