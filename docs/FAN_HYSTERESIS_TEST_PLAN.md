# 风扇滞后控制验证测试计划

## 测试目标
验证温度滞后控制策略是否按预期工作：
- ✅ 3°C温度死区
- ✅ 2秒最小调速间隔
- ✅ 平滑转速过渡

## 测试环境
- 设备: robOS ESP32S3
- 风扇: Fan 0 (GPIO 41)
- 当前配置: 
  - 滞后: 3.0°C
  - 间隔: 2000ms
  - 曲线模式: 已启用

## 测试步骤

### 1. 初始状态检查
```bash
fan status 0
```
**预期结果**: 显示详细状态包括滞后参数

### 2. 温度死区测试 (小幅温度变化)
```bash
# 设置基准温度
temp set 30

# 等待1秒观察转速
# 然后小幅调整温度 (+2°C, 小于3°C死区)
temp set 32

# 观察转速是否保持不变
fan status 0
```
**预期结果**: 
- Target Speed 更新
- Last Applied Speed 保持不变
- 实际PWM输出不变

### 3. 温度死区测试 (显著温度变化)
```bash
# 大幅调整温度 (+5°C, 超过3°C死区)
temp set 35

# 立即检查状态
fan status 0
```
**预期结果**:
- Target Speed 更新
- Last Applied Speed 更新到新值
- 实际PWM输出改变

### 4. 时间间隔测试 (快速连续调整)
```bash
# 第一次大幅调整
temp set 40
fan status 0

# 立即再次大幅调整 (间隔<2秒)
temp set 45
fan status 0
```
**预期结果**:
- Target Speed 立即更新到45°C对应值
- Last Applied Speed 保持40°C对应值
- 需要等待2秒后才会应用新转速

### 5. 时间间隔测试 (等待后调整)
```bash
# 等待3秒后检查
# (等待时间 > 2秒间隔)
fan status 0
```
**预期结果**:
- Last Applied Speed 现在应该更新到45°C对应值

### 6. 曲线插值验证
```bash
# 显示当前曲线
fan config show 0

# 测试不同温度点
temp set 25  # 应该对应较低转速
fan status 0

temp set 50  # 应该对应中等转速  
fan status 0

temp set 70  # 应该对应较高转速
fan status 0
```

### 7. 配置修改测试
```bash
# 修改滞后参数
fan config hysteresis 0 1.0 1000

# 验证新参数生效
fan status 0

# 测试新的死区 (1°C)
temp set 51  # +1°C变化应该触发调速
fan status 0
```

## 验证点检查表

### 温度死区控制
- [ ] 小于3°C变化: Target Speed更新，Last Applied Speed不变
- [ ] 大于3°C变化: 两个速度都更新
- [ ] 死区修改后新阈值生效

### 时间间隔控制  
- [ ] 2秒内连续调整: 只有Target Speed更新
- [ ] 超过2秒后: Last Applied Speed跟上Target Speed
- [ ] 间隔修改后新时间生效

### 状态跟踪准确性
- [ ] Target Speed 实时反映曲线计算结果
- [ ] Last Applied Speed 反映实际PWM输出
- [ ] 时间戳正确记录最后调速时间

### 功能集成测试
- [ ] 配置保存/加载正常
- [ ] 状态显示完整准确
- [ ] 不影响其他风扇控制模式

## 测试结果记录

### 实际测试输出
```
[在此记录实际测试时的控制台输出]
```

### 问题发现
```
[记录任何发现的问题]
```

### 性能评估
- 响应延迟: 
- 噪声改善: 
- 稳定性: