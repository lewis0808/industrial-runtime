#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace core {

/// 日志级别（与 spdlog 解耦的对外枚举）。
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off,
};

/// 日志配置。
struct LoggerConfig {
    LogLevel level{LogLevel::Info};
    bool toConsole{true};
    std::string filePath;            ///< 非空则同时写入该文件
    std::size_t maxFileSize{5 * 1024 * 1024};  ///< 单文件最大字节数（滚动）
    std::size_t maxFiles{3};         ///< 滚动文件数量
    std::string pattern{"[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"};
};

/// 运行时统一日志入口。线程安全。
///
/// 内部封装 spdlog；core 其余模块只依赖此处，不直接依赖 spdlog。
class Logger {
public:
    /// 初始化全局日志器。重复调用以最后一次为准。
    static void init(const LoggerConfig& config = {});

    /// 获取底层 logger（已初始化）。未初始化时按默认配置惰性初始化。
    [[nodiscard]] static std::shared_ptr<spdlog::logger> get();

    /// 运行时调整日志级别。
    static void setLevel(LogLevel level);

    /// 刷新缓冲到落盘。
    static void flush();

private:
    Logger() = default;
};

}  // namespace core

// 便捷日志宏。透传 fmt 风格格式化参数。
#define IR_LOG_TRACE(...)    ::core::Logger::get()->trace(__VA_ARGS__)
#define IR_LOG_DEBUG(...)    ::core::Logger::get()->debug(__VA_ARGS__)
#define IR_LOG_INFO(...)     ::core::Logger::get()->info(__VA_ARGS__)
#define IR_LOG_WARN(...)     ::core::Logger::get()->warn(__VA_ARGS__)
#define IR_LOG_ERROR(...)    ::core::Logger::get()->error(__VA_ARGS__)
#define IR_LOG_CRITICAL(...) ::core::Logger::get()->critical(__VA_ARGS__)