# 🎨 robOS 色彩校正系统完整指南

robOS 提供了一个功能强大的色彩校正系统，专门为 WS2812 LED 设备设计，支持白点校正、伽马校正、亮度增强和饱和度增强等多种色彩处理功能。

## 📋 系统概览

色彩校正系统包含以下五个主要功能模块：

1. **整体控制** - 启用/禁用整个色彩校正系统
2. **白点校正** - RGB 通道独立调节，修正白平衡
3. **伽马校正** - 非线性亮度校正，提升显示效果
4. **亮度增强** - HSL 空间亮度调节
5. **饱和度增强** - HSL 空间色彩饱和度调节

## 🔧 基本控制命令

### 系统开关控制
```bash
color enable                    # 启用色彩校正系统
color disable                   # 禁用色彩校正系统
color status                    # 显示当前配置状态
```

### 查看系统状态
```bash
color status
```
**输出示例**：
```
Color Correction Status:
  Overall: Enabled
  White Point Correction: Enabled
    R: 0.90, G: 1.00, B: 1.10
  Gamma Correction: Enabled
    Gamma: 2.20
  Brightness Enhancement: Enabled
    Factor: 1.20
  Saturation Enhancement: Enabled
    Factor: 1.10
```

## 🔍 详细功能命令

### 1. 白点校正 (White Point Correction)

**功能**: 独立调节 RGB 三个通道的比例，修正 LED 的色温和白平衡。

```bash
# 设置白点校正 (启用模式)
color whitepoint <r> <g> <b>                    # 默认启用
color whitepoint <r> <g> <b> enable             # 明确启用
color whitepoint <r> <g> <b> disable            # 设置但禁用

# 实用示例
color whitepoint 0.9 1.0 1.1                   # 减少红色，增加蓝色 (暖色温->冷色温)
color whitepoint 1.1 1.0 0.9                   # 增加红色，减少蓝色 (冷色温->暖色温)
color whitepoint 1.0 1.0 1.0                   # 标准白点，无调节
```

**参数范围**: 0.0 - 2.0 (1.0 = 无调节)
- **< 1.0**: 降低该通道强度
- **= 1.0**: 保持原始强度  
- **> 1.0**: 增强该通道强度

### 2. 伽马校正 (Gamma Correction)

**功能**: 应用非线性亮度曲线，改善暗部细节和整体对比度。

```bash
# 设置伽马校正
color gamma <value>                             # 默认启用
color gamma <value> enable                      # 明确启用
color gamma <value> disable                     # 设置但禁用

# 常用伽马值
color gamma 2.2                                 # 标准 sRGB 伽马 (推荐)
color gamma 1.8                                 # Mac 传统伽马
color gamma 1.0                                 # 线性伽马 (无校正)
color gamma 2.8                                 # 高对比度显示
```

**参数范围**: 0.1 - 4.0
- **< 1.0**: 提亮暗部，降低对比度
- **= 1.0**: 线性响应，无校正
- **1.8-2.2**: 标准显示器伽马
- **> 2.2**: 增强对比度，暗部更暗

### 3. 亮度增强 (Brightness Enhancement)

**功能**: 在 HSL 色彩空间调节整体亮度，保持色相不变。

```bash
# 设置亮度增强
color brightness <factor>                       # 默认启用
color brightness <factor> enable                # 明确启用
color brightness <factor> disable               # 设置但禁用

# 实用示例
color brightness 0.5                            # 降低亮度 50%
color brightness 1.0                            # 保持原始亮度
color brightness 1.2                            # 增加亮度 20%
color brightness 1.5                            # 增加亮度 50%
```

**参数范围**: 0.0 - 2.0 (1.0 = 无调节)
- **< 1.0**: 降低亮度
- **= 1.0**: 保持原始亮度
- **> 1.0**: 增加亮度

### 4. 饱和度增强 (Saturation Enhancement)

**功能**: 在 HSL 色彩空间调节色彩饱和度，让颜色更加鲜艳或柔和。

```bash
# 设置饱和度增强
color saturation <factor>                       # 默认启用
color saturation <factor> enable                # 明确启用
color saturation <factor> disable               # 设置但禁用

# 实用示例
color saturation 0.8                            # 降低饱和度 20% (柔和效果)
color saturation 1.0                            # 保持原始饱和度
color saturation 1.2                            # 增加饱和度 20% (鲜艳效果)
color saturation 1.5                            # 大幅增加饱和度 50%
```

**参数范围**: 0.0 - 2.0 (1.0 = 无调节)
- **< 1.0**: 降低饱和度，颜色更柔和
- **= 1.0**: 保持原始饱和度
- **> 1.0**: 增加饱和度，颜色更鲜艳

## 💾 配置管理命令

### 重置配置
```bash
color reset                                     # 重置所有设置为默认值
```

### 自动保存
```bash
color save                                      # 显示保存状态信息
```
**注**: 所有设置更改都会自动保存到 NVS 闪存，无需手动保存。

### 配置文件导入导出

```bash
# 导出配置到 SD 卡
color export <filename>                         # 导出当前配置为 JSON 文件
color export /sdcard/my_color_config.json      # 示例

# 从 SD 卡导入配置
color import <filename>                         # 从 JSON 文件导入配置
color import /sdcard/my_color_config.json      # 示例
```

**JSON 配置文件示例**:
```json
{
  "enabled": true,
  "white_point": {
    "enabled": true,
    "red_scale": 0.9,
    "green_scale": 1.0,
    "blue_scale": 1.1
  },
  "gamma": {
    "enabled": true,
    "gamma": 2.2
  },
  "brightness": {
    "enabled": true,
    "factor": 1.2
  },
  "saturation": {
    "enabled": true,
    "factor": 1.1
  }
}
```

## 📖 实用场景配置指南

### 1. 标准显示优化 (推荐配置)
```bash
color enable
color whitepoint 1.0 1.0 1.0                   # 标准白点
color gamma 2.2                                 # sRGB 伽马
color brightness 1.0                            # 原始亮度
color saturation 1.1                            # 轻微增强饱和度
```

### 2. 温暖色调环境
```bash
color enable
color whitepoint 1.1 1.0 0.9                   # 增加红色，减少蓝色
color gamma 2.0                                 # 稍低伽马，更柔和
color brightness 0.9                            # 降低亮度
color saturation 1.0                            # 保持自然饱和度
```

### 3. 冷色调环境  
```bash
color enable
color whitepoint 0.9 1.0 1.1                   # 减少红色，增加蓝色
color gamma 2.4                                 # 稍高伽马，更锐利
color brightness 1.1                            # 稍微提亮
color saturation 1.2                            # 增强饱和度
```

### 4. 高对比度显示
```bash
color enable  
color whitepoint 1.0 1.0 1.0                   # 标准白点
color gamma 2.8                                 # 高伽马，强对比
color brightness 1.3                            # 提高亮度
color saturation 1.3                            # 提高饱和度
```

### 5. 柔和护眼模式
```bash
color enable
color whitepoint 1.0 0.95 0.85                 # 减少蓝光
color gamma 1.8                                 # 低伽马，更柔和
color brightness 0.7                            # 降低亮度
color saturation 0.9                            # 降低饱和度
```

### 6. 完全禁用色彩校正
```bash
color disable                                   # 使用原始 LED 输出
```

## 🔍 参数调节技巧

### 白点校正调节建议
- **问题：LED 偏冷色** → `color whitepoint 1.1 1.0 0.9`
- **问题：LED 偏暖色** → `color whitepoint 0.9 1.0 1.1`  
- **问题：绿色偏强** → `color whitepoint 1.0 0.9 1.0`
- **问题：某通道暗淡** → 增加对应通道的数值

### 伽马校正选择指南
- **1.0**: 用于线性 LED 应用或特殊效果
- **1.8**: 适合 Mac 系统兼容性
- **2.2**: 标准 PC 显示器，推荐用于一般应用
- **2.4-2.6**: 电视标准，适合视频显示
- **> 3.0**: 高对比度，适合文字显示

### 亮度和饱和度协调
- **明亮环境**: `brightness 1.2-1.5, saturation 1.1-1.3`
- **昏暗环境**: `brightness 0.6-0.9, saturation 0.9-1.1`
- **装饰照明**: `brightness 0.8-1.2, saturation 1.2-1.5`

## ⚡ 快速配置命令

### 一键恢复默认
```bash
color reset
```

### 快速查看当前状态
```bash  
color status
```

### 应急禁用
```bash
color disable
```

### 备份当前配置
```bash
color export /sdcard/backup_$(date +%Y%m%d).json
```

## 🎯 性能说明

- **实时生效**: 所有设置立即应用到新的 LED 输出
- **自动保存**: 配置更改自动保存到 NVS 闪存
- **重启保持**: 系统重启后自动恢复上次配置
- **低开销**: 使用查找表和优化算法，CPU 占用极低
- **全局影响**: 影响所有使用色彩校正的 LED 子系统

## 🛠️ 技术实现细节

### 色彩空间转换
- **RGB ↔ HSL**: 支持 RGB 和 HSL 色彩空间的双向转换
- **查找表优化**: 伽马校正使用预计算查找表，提升性能
- **精度保证**: 浮点运算确保色彩转换精度

### 内存和性能优化
- **懒加载**: 仅在需要时初始化查找表
- **缓存机制**: 避免重复计算相同的色彩转换
- **线程安全**: 支持多线程环境下的安全访问

### 配置持久化
- **NVS 存储**: 使用 ESP32 的 NVS 系统进行配置持久化
- **原子操作**: 确保配置更新的原子性
- **错误恢复**: 配置损坏时自动恢复默认设置

## 🔧 故障排除

### 常见问题

**Q: 色彩校正不生效**
```bash
color status                    # 检查是否启用
color enable                    # 确保系统启用
```

**Q: 颜色显示异常**
```bash
color reset                     # 重置为默认设置
color status                    # 检查当前配置
```

**Q: 配置无法保存**
```bash
# 检查 NVS 空间是否充足
# 重启系统后再次尝试
```

**Q: 导入配置文件失败**
```bash
# 检查 SD 卡是否正常挂载
# 确认 JSON 文件格式正确
```

### 调试模式
```bash
# 启用调试日志 (如果支持)
color enable
color whitepoint 1.0 1.0 1.0
color status                    # 查看详细状态
```

## 📚 相关技术资料

### 色彩理论基础
- **色温**: 以开尔文 (K) 为单位，描述光源的颜色特征
- **伽马**: 描述显示设备的非线性响应特性
- **HSL**: 色相 (Hue)、饱和度 (Saturation)、亮度 (Lightness) 色彩模型

### 标准参考值
- **sRGB 伽马**: 2.2 (PC 显示器标准)
- **Mac 伽马**: 1.8 (Apple 传统标准)
- **电视伽马**: 2.4 (广播电视标准)
- **线性伽马**: 1.0 (无校正)

---

**文档版本**: v1.0  
**更新日期**: 2025年10月5日  
**维护团队**: robOS 开发团队

这个完整的色彩校正指南涵盖了 robOS 系统中所有可用的色彩校正功能。通过合理使用这些命令，您可以大幅改善 LED 显示效果，适应不同的环境和应用需求。