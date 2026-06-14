# Logger — spdlog 封装

> `logger/logger.{hpp,cpp}` · 库 `core_logger` · 依赖 `spdlog`

运行时统一日志入口。core 其余模块**只依赖此处**，不直接 `#include` spdlog——把第三方
日志库隔离在一层之内，便于将来替换。

## 公开 API

```cpp
enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical, Off };  // 与 spdlog 解耦
struct LoggerConfig { LogLevel level; bool toConsole; std::string filePath;
                      std::size_t maxFileSize{5MB}; std::size_t maxFiles{3};
                      std::string pattern{"[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"}; };

static void Logger::init(const LoggerConfig& = {});   // 重复调用以最后一次为准
static std::shared_ptr<spdlog::logger> get();         // 未初始化则按默认惰性初始化
static void setLevel(LogLevel);
static void flush();

// 便捷宏（透传 fmt 风格参数）
IR_LOG_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL(...)
```

## 设计要点

- **双 sink**：控制台 + 可选滚动文件（`filePath` 非空时启用，按 `maxFileSize`×`maxFiles` 滚动）。
- **对外枚举 `LogLevel`**：与 spdlog 的级别解耦，调用方不感知底层库。
- **宏经 `Logger::get()`**：未初始化时惰性按默认配置创建，保证任何早期日志都不崩。
- `RuntimeEngine::init` 从 Config 读 `logger.level/file/console` 装配 `LoggerConfig` 并 `init`。

## 待改善

| 项 | 说明 | 方向 |
|----|------|------|
| **全局单例** | 全进程一个 logger，无法按子系统分流。 | 如需分模块日志/独立级别，提供命名 logger 注册。 |
| **滚动仅按大小** | 无按日期/时间滚动。 | 工业现场常按天归档，可加 daily sink。 |
| **init 竞态** | 多线程并发 init/get 行为依赖 spdlog 内部；本封装未额外加锁。 | 约定 init 在 `main` 早期单线程完成（当前即如此）。 |
| **无结构化日志** | 纯文本行。 | 若接 ELK/Loki 等可选 JSON sink。 |
