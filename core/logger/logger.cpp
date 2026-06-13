#include "logger/logger.hpp"

#include <mutex>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace core {

namespace {

constexpr const char *LOGGER_NAME = "runtime";

spdlog::level::level_enum toSpdLevel(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    case LogLevel::Off:
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

std::mutex g_initMutex;

std::shared_ptr<spdlog::logger> buildLogger(const LoggerConfig &config) {
    std::vector<spdlog::sink_ptr> sinks;

    if (config.toConsole) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    if (!config.filePath.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            config.filePath, config.maxFileSize, config.maxFiles));
    }

    auto logger = std::make_shared<spdlog::logger>(LOGGER_NAME, sinks.begin(), sinks.end());
    logger->set_pattern(config.pattern);
    logger->set_level(toSpdLevel(config.level));
    logger->flush_on(spdlog::level::warn);
    return logger;
}

} // namespace

void Logger::init(const LoggerConfig &config) {
    std::lock_guard<std::mutex> lock(g_initMutex);
    auto logger = buildLogger(config);
    // 替换默认 logger，使后续 get() 取到最新配置。
    spdlog::set_default_logger(logger);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    if (auto logger = spdlog::default_logger(); logger && logger->name() == LOGGER_NAME) {
        return logger;
    }
    // 惰性初始化：未显式 init 时按默认配置建立。
    std::lock_guard<std::mutex> lock(g_initMutex);
    auto current = spdlog::default_logger();
    if (!current || current->name() != LOGGER_NAME) {
        spdlog::set_default_logger(buildLogger(LoggerConfig{}));
    }
    return spdlog::default_logger();
}

void Logger::setLevel(LogLevel level) { get()->set_level(toSpdLevel(level)); }

void Logger::flush() { get()->flush(); }

} // namespace core