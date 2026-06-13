#pragma once

#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace core {

/// 运行时配置。基于 nlohmann/json，支持点号分隔的层级键访问。
///
/// 读操作线程安全（共享锁）；加载操作独占锁。
/// 示例： cfg.get<int>("runtime.scheduler.threads", 4)
class Config {
public:
    Config() = default;

    /// 从文件加载 JSON。失败返回 false（不抛出）。
    [[nodiscard]] bool loadFile(const std::string& path);

    /// 从 JSON 字符串加载。失败返回 false（不抛出）。
    [[nodiscard]] bool loadString(const std::string& text);

    /// 按点号路径读取值，类型不匹配或缺失时返回 defaultValue。
    template <typename T>
    [[nodiscard]] T get(std::string_view dottedKey, T defaultValue) const {
        std::shared_lock lock(mutex_);
        const nlohmann::json* node = resolve(dottedKey);
        if (node == nullptr) {
            return defaultValue;
        }
        try {
            return node->get<T>();
        } catch (const nlohmann::json::exception&) {
            return defaultValue;
        }
    }

    /// 按点号路径读取值，缺失或类型不匹配返回 std::nullopt。
    template <typename T>
    [[nodiscard]] std::optional<T> tryGet(std::string_view dottedKey) const {
        std::shared_lock lock(mutex_);
        const nlohmann::json* node = resolve(dottedKey);
        if (node == nullptr) {
            return std::nullopt;
        }
        try {
            return node->get<T>();
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        }
    }

    /// 判断某路径是否存在。
    [[nodiscard]] bool has(std::string_view dottedKey) const;

    /// 返回整个配置的副本（线程安全）。
    [[nodiscard]] nlohmann::json snapshot() const;

private:
    /// 按点号路径定位节点；调用方需持有锁。未找到返回 nullptr。
    const nlohmann::json* resolve(std::string_view dottedKey) const;

    mutable std::shared_mutex mutex_;
    nlohmann::json root_{nlohmann::json::object()};
};

}  // namespace core