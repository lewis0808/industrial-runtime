#pragma once

#include <string>

namespace irsp {

/// 写回结果。
enum class WriteResult {
    Accepted,   ///< 已受理（某插件接受，已写出/排队）
    NotHandled, ///< 无人负责该 topic（无匹配前缀）
    Error,      ///< 处理出错
};

/// 写回出口。语义层经此把应用 SET 路由到设备侧，与具体实现（core/插件）解耦。
class TagWriter {
  public:
    virtual ~TagWriter() = default;

    /// 写一个 Tag。
    /// @param name  目标 topic
    /// @param type  类型标签（"f64"/"i32"/"str"/"bool"... 见 datatype.md）
    /// @param value 值字节（按 type 小端 / str 为 UTF-8）
    [[nodiscard]] virtual WriteResult write(const std::string &name, const std::string &type,
                                            const std::string &value) = 0;
};

} // namespace irsp
