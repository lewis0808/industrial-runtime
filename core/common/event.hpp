#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "common/types.hpp"

namespace core {

/// 事件严重级别。
enum class EventSeverity : std::uint8_t {
    Info = 0,
    Warning,
    Alarm,
    Critical,
};

[[nodiscard]] constexpr std::string_view eventSeverityName(EventSeverity s) noexcept {
    switch (s) {
    case EventSeverity::Info:
        return "Info";
    case EventSeverity::Warning:
        return "Warning";
    case EventSeverity::Alarm:
        return "Alarm";
    case EventSeverity::Critical:
        return "Critical";
    }
    return "Unknown";
}

/// Event 数据：报警、状态变化、系统通知。
///
/// 禁止用于存储实时变量（实时变量属于 Tag 体系）。
struct Event {
    std::string source;   ///< 事件来源（插件 id / 模块名）
    std::string category; ///< 事件分类（如 "alarm" / "state" / "system"）
    std::string message;  ///< 人类可读描述
    EventSeverity severity{EventSeverity::Info};
    Timestamp timestamp{};

    Event() = default;

    Event(std::string src, std::string cat, std::string msg,
          EventSeverity sev = EventSeverity::Info, Timestamp ts = now())
        : source(std::move(src)), category(std::move(cat)), message(std::move(msg)), severity(sev),
          timestamp(ts) {}
};

} // namespace core