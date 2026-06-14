#pragma once

#include <string_view>
#include <vector>

namespace irsp {

/// 按 '/' 分割 topic 为段。空串返回空向量。
[[nodiscard]] std::vector<std::string_view> splitTopic(std::string_view topic);

/// Topic 匹配原语（MQTT 语义）。详见 irsp/protocol/datatype.md。
///
/// 分隔符 `/`；通配符 `+`（单层）、`#`（多层，必须末段且匹配零或多层）。
class TopicMatcher {
  public:
    /// pattern 是否合法：段非空；`#` 仅末段且独占一段；`+` 独占一段；
    /// 字面段不含 `+`/`#`。
    [[nodiscard]] static bool isValidPattern(std::string_view pattern);

    /// 是否为具体 topic（合法且不含任何通配符）。
    [[nodiscard]] static bool isConcrete(std::string_view topic);

    /// pattern 是否匹配具体 topic。pattern 须合法、topic 须具体（否则返回 false）。
    [[nodiscard]] static bool matches(std::string_view pattern, std::string_view topic);
};

} // namespace irsp
