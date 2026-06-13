#include "config/config.hpp"

#include <fstream>

namespace core {

bool Config::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    try {
        nlohmann::json parsed = nlohmann::json::parse(in, nullptr, true, true);
        std::unique_lock lock(mutex_);
        root_ = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool Config::loadString(const std::string& text) {
    try {
        nlohmann::json parsed = nlohmann::json::parse(text, nullptr, true, true);
        std::unique_lock lock(mutex_);
        root_ = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool Config::has(std::string_view dottedKey) const {
    std::shared_lock lock(mutex_);
    return resolve(dottedKey) != nullptr;
}

nlohmann::json Config::snapshot() const {
    std::shared_lock lock(mutex_);
    return root_;
}

const nlohmann::json* Config::resolve(std::string_view dottedKey) const {
    const nlohmann::json* node = &root_;
    std::size_t start = 0;
    while (start <= dottedKey.size()) {
        const std::size_t dot = dottedKey.find('.', start);
        const std::string_view segment =
            dottedKey.substr(start, dot == std::string_view::npos
                                         ? std::string_view::npos
                                         : dot - start);
        if (segment.empty() || !node->is_object()) {
            return nullptr;
        }
        const auto it = node->find(std::string(segment));
        if (it == node->end()) {
            return nullptr;
        }
        node = &(*it);
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    return node;
}

}  // namespace core