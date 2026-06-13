#include "runtime_engine/runtime_engine.hpp"

#include <string>
#include <utility>

#include "logger/logger.hpp"

namespace core {

namespace {

LogLevel parseLogLevel(const std::string& s) {
    if (s == "trace") return LogLevel::Trace;
    if (s == "debug") return LogLevel::Debug;
    if (s == "info") return LogLevel::Info;
    if (s == "warn") return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    if (s == "critical") return LogLevel::Critical;
    if (s == "off") return LogLevel::Off;
    return LogLevel::Info;
}

}  // namespace

RuntimeEngine::RuntimeEngine() = default;

RuntimeEngine::~RuntimeEngine() {
    stop();
}

void RuntimeEngine::init(const Config& config) {
    LoggerConfig logCfg;
    logCfg.level = parseLogLevel(config.get<std::string>("logger.level", "info"));
    logCfg.filePath = config.get<std::string>("logger.file", "");
    logCfg.toConsole = config.get<bool>("logger.console", true);
    Logger::init(logCfg);

    IR_LOG_INFO("RuntimeEngine 初始化完成");
}

void RuntimeEngine::start() {
    if (started_) {
        return;
    }
    started_ = true;
    eventBus_.start();
    scheduler_.start();
    IR_LOG_INFO("RuntimeEngine 已启动");
}

void RuntimeEngine::stop() {
    if (!started_) {
        return;
    }
    started_ = false;
    scheduler_.stop();
    eventBus_.stop();
    IR_LOG_INFO("RuntimeEngine 已停止");
}

bool RuntimeEngine::pushTag(const TagValue& tag) {
    return tagEngine_.write(tag);
}

bool RuntimeEngine::pushEvent(const Event& event) {
    return eventBus_.publish(event);
}

bool RuntimeEngine::pushStream(const StreamFrame& frame) {
    std::lock_guard<std::mutex> lock(streamSinkMutex_);
    if (!streamSink_) {
        return false;  // 无流接收方
    }
    streamSink_(frame);
    return true;
}

void RuntimeEngine::setStreamSink(StreamSink sink) {
    std::lock_guard<std::mutex> lock(streamSinkMutex_);
    streamSink_ = std::move(sink);
}

}  // namespace core
