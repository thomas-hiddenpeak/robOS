# Config Manager Unit Tests

这个目录包含了`config_manager`组件的完整单元测试套件。

## 测试覆盖

单元测试涵盖以下功能：

### 基础功能测试
- 初始化和反初始化
- 默认配置和自定义配置
- 双重初始化处理
- 未初始化状态下的操作处理

### 数据类型测试
- `uint8_t`, `uint16_t`, `uint32_t` 类型
- `int8_t`, `int16_t`, `int32_t` 类型
- `float` 浮点数类型
- `bool` 布尔类型
- `string` 字符串类型
- `blob` 二进制数据类型

### 错误处理和边界条件测试
- 无效参数处理
- 键不存在的情况
- 类型不匹配处理
- 缓冲区大小边界条件

### 命名空间和键管理测试
- 键存在性检查
- 键删除操作
- 命名空间清理
- 多命名空间隔离

### 批量操作测试
- 批量保存操作
- 批量加载操作

### 持久化和提交测试
- 手动提交操作
- 配置持久化验证

### 统计信息测试
- NVS使用统计

### 线程安全测试
- 多线程并发操作
- 竞态条件处理

## 如何运行测试

### 1. 构建测试

```bash
cd /path/to/robOS/tests/test_config_manager
./run_tests.sh
```

### 2. 在硬件上运行测试

```bash
# 方法1：使用脚本（自动烧录和监控）
./run_tests.sh /dev/cu.usbmodem01234567891

# 方法2：手动运行
idf.py -p /dev/cu.usbmodem01234567891 flash monitor
```

### 3. 仅构建（不运行）

```bash
# 设置ESP-IDF环境
source ~/esp/v5.5.1/esp-idf/export.sh

# 构建测试
idf.py build
```

## 测试输出

测试运行时会显示详细的测试结果：

```
=== Config Manager Unit Tests ===

TEST(test_config_manager_init_default) PASS
TEST(test_config_manager_uint8) PASS
TEST(test_config_manager_string) PASS
...

=== All Config Manager Tests Completed ===
```

## 测试环境要求

- ESP32S3 开发板
- ESP-IDF v5.5.1
- Unity 测试框架（ESP-IDF内置）

## 项目结构

```
test_config_manager/
├── CMakeLists.txt              # 主项目配置
├── sdkconfig.defaults          # 默认配置
├── run_tests.sh               # 测试运行脚本
├── README.md                  # 本文档
└── main/
    ├── CMakeLists.txt         # 主组件配置
    └── test_config_manager.c  # 测试源码
```

## 故障排除

### 编译错误
- 确保ESP-IDF环境已正确设置
- 检查组件路径是否正确
- 验证config_manager组件是否在../../components目录中

### 运行时错误
- 确保设备连接正常
- 检查串口路径是否正确
- 验证NVS分区是否正确初始化

### 测试失败
- 检查设备的NVS存储是否有足够空间
- 确保没有其他程序占用NVS分区
- 验证设备重启后配置是否正确清理

## 添加新测试

要添加新的测试用例：

1. 在`test_config_manager.c`中添加测试函数
2. 在`app_main()`中使用`RUN_TEST()`添加测试
3. 重新构建和运行测试

例如：

```c
void test_new_feature(void)
{
    // 测试代码
    TEST_ASSERT_EQUAL(ESP_OK, some_function());
}

// 在app_main()中添加：
RUN_TEST(test_new_feature);
```