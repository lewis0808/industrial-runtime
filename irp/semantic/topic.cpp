#include "semantic/topic.hpp"

namespace irp {

std::vector<std::string_view> splitTopic(std::string_view topic) {
    std::vector<std::string_view> segments;
    if (topic.empty()) {
        return segments;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = topic.find('/', start);
        if (slash == std::string_view::npos) {
            segments.push_back(topic.substr(start));
            break;
        }
        segments.push_back(topic.substr(start, slash - start));
        start = slash + 1;
    }
    return segments;
}

namespace {

bool segmentHasWildcardChar(std::string_view seg) {
    return seg.find('+') != std::string_view::npos ||
           seg.find('#') != std::string_view::npos;
}

}  // namespace

bool TopicMatcher::isValidPattern(std::string_view pattern) {
    if (pattern.empty()) {
        return false;
    }
    const auto segs = splitTopic(pattern);
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const std::string_view seg = segs[i];
        if (seg.empty()) {
            return false;  // 禁止空段（前导/尾随/连续 '/'）
        }
        if (seg == "#") {
            if (i != segs.size() - 1) {
                return false;  // '#' 必须是末段
            }
            continue;
        }
        if (seg == "+") {
            continue;
        }
        if (segmentHasWildcardChar(seg)) {
            return false;  // 通配符必须独占整段
        }
    }
    return true;
}

bool TopicMatcher::isConcrete(std::string_view topic) {
    if (!isValidPattern(topic)) {
        return false;
    }
    for (const auto seg : splitTopic(topic)) {
        if (seg == "+" || seg == "#") {
            return false;
        }
    }
    return true;
}

bool TopicMatcher::matches(std::string_view pattern, std::string_view topic) {
    if (!isValidPattern(pattern) || !isConcrete(topic)) {
        return false;
    }
    const auto p = splitTopic(pattern);
    const auto t = splitTopic(topic);
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < p.size()) {
        if (p[i] == "#") {
            return true;  // 匹配剩余零或多层
        }
        if (j >= t.size()) {
            return false;
        }
        if (p[i] == "+" || p[i] == t[j]) {
            ++i;
            ++j;
            continue;
        }
        return false;
    }
    return j == t.size();
}

}  // namespace irp
