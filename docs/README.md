# robOS 项目文档

本目录包含robOS项目的完整技术文档。

## 文档目录

### 📊 项目管理文档
- **[PROJECT_PROGRESS.md](PROJECT_PROGRESS.md)** - 项目进度记录和完成状态
  - 详细记录已完成的工作成果
  - 测试结果汇总和验证状态
  - 技术问题解决记录
  - 下一步开发计划

### 🏗️ 技术设计文档
- **[TECHNICAL_ARCHITECTURE.md](TECHNICAL_ARCHITECTURE.md)** - 技术架构设计
  - 系统架构分层设计
  - 组件间通信机制
  - 内存管理和并发控制策略
  - 性能优化和可扩展性设计

### 📖 API参考文档
- **[API_REFERENCE.md](API_REFERENCE.md)** - 完整的API接口文档
  - 事件管理器API详细说明
  - 硬件抽象层API完整参考
  - 代码示例和使用指南
  - 错误代码和常量定义

### 📝 开发规范文档
- **[CODING_STANDARDS.md](CODING_STANDARDS.md)** - 代码规范和开发标准
  - 代码风格和命名规范
  - 目录结构和文件组织
  - 注释和文档编写要求
  - 测试驱动开发流程

## 文档使用指南

### 对于新开发者
1. 先阅读 `PROJECT_PROGRESS.md` 了解项目现状
2. 学习 `TECHNICAL_ARCHITECTURE.md` 理解系统设计
3. 参考 `CODING_STANDARDS.md` 遵循开发规范
4. 使用 `API_REFERENCE.md` 查阅具体API

### 对于组件开发
1. 查看 `TECHNICAL_ARCHITECTURE.md` 中的组件设计模式
2. 遵循 `CODING_STANDARDS.md` 的编码规范
3. 参考已完成组件的API设计模式
4. 编写对应的单元测试

### 对于系统集成
1. 理解 `TECHNICAL_ARCHITECTURE.md` 中的通信机制
2. 使用 `API_REFERENCE.md` 中的事件系统API
3. 参考项目进度中的集成测试方法

## 文档维护

- 所有文档都应该与代码保持同步
- 新增组件时必须更新对应的API文档
- 重要的技术决策应该记录在架构文档中
- 项目里程碑达成时更新进度文档

---

*文档最后更新: 2025年9月28日*  
*维护者: robOS开发团队*