# LPMU Auto-Start Feature

## 功能概述

本功能实现了LPMU设备的开机自启动功能，包括：

1. **配置存储**：使用NVS存储LPMU自启动配置
2. **自动启动**：系统启动时根据配置自动启动LPMU
3. **状态同步**：确保系统显示的LPMU状态与实际硬件状态同步
4. **命令行接口**：提供命令行接口管理LPMU自启动配置

## 实现细节

### 1. 配置管理
- 使用`config_manager`组件在NVS中存储配置
- 配置命名空间：`device`
- 配置键：`auto_start_lpmu`
- 默认值：`false`（不自动启动）

### 2. 状态管理
- 使用`power_state_t`枚举管理LPMU状态：
  - `POWER_STATE_OFF`：设备关闭
  - `POWER_STATE_ON`：设备开启
  - `POWER_STATE_UNKNOWN`：状态未知（首次启动或配置了自启动）

### 3. 自启动逻辑
- 在`device_controller_init()`中加载配置
- 如果`auto_start_lpmu`为`true`，则自动执行`toggle`命令启动LPMU
- 更新状态为`POWER_STATE_ON`（假设toggle成功）

## 使用方法

### 查看当前LPMU状态和配置
```bash
lpmu status
```
输出示例：
```
LPMU 设备状态: 开机
开机自启动: 开启
```

### 启用开机自启动
```bash
lpmu config auto-start on
```

### 禁用开机自启动
```bash
lpmu config auto-start off
```

### 查看自启动配置
```bash
lpmu config auto-start
```

### 手动启动/关闭LPMU
```bash
lpmu toggle
```

## 状态同步机制

1. **初始状态**：
   - 如果配置了自启动：状态设为`UNKNOWN`，执行toggle后变为`ON`
   - 如果未配置自启动：状态设为`OFF`

2. **Toggle逻辑**：
   - `UNKNOWN` → `ON`（首次toggle假设启动成功）
   - `ON` → `OFF`
   - `OFF` → `ON`

3. **状态显示**：
   - `lpmu status`命令会显示当前状态和自启动配置
   - `UNKNOWN`状态会提示用户使用toggle命令启动

## 实现的文件修改

### 1. device_controller.h
- 添加`device_config_t`结构体
- 添加配置管理函数声明

### 2. device_controller.c
- 添加配置管理实现
- 修改初始化逻辑支持自启动
- 改进状态管理逻辑

### 3. hardware_commands.c
- 扩展`lpmu`命令支持`config`子命令
- 改进`status`命令显示更多信息

### 4. CMakeLists.txt
- 添加`config_manager`依赖

## 测试建议

1. **基本功能测试**：
   ```bash
   # 查看初始状态
   lpmu status
   
   # 启用自启动
   lpmu config auto-start on
   
   # 重启系统验证自启动
   reboot
   
   # 启动后检查状态
   lpmu status
   ```

2. **配置持久化测试**：
   ```bash
   # 设置自启动
   lpmu config auto-start on
   
   # 重启多次验证配置保持
   reboot
   lpmu config auto-start  # 应该显示"开启"
   ```

3. **状态同步测试**：
   ```bash
   # 测试状态切换
   lpmu toggle
   lpmu status
   lpmu toggle
   lpmu status
   ```

## 注意事项

1. **硬件状态假设**：当前实现基于软件状态管理，假设GPIO控制成功时硬件状态会相应改变
2. **首次启动**：如果配置了自启动，首次toggle会假设设备启动成功
3. **状态同步**：实际硬件状态可能与软件状态不同，未来可考虑添加硬件状态检测机制

## 未来改进

1. **硬件状态检测**：添加GPIO读取或其他方式检测LPMU实际电源状态
2. **启动超时**：为自启动过程添加超时和重试机制
3. **状态验证**：在toggle后验证硬件实际状态变化