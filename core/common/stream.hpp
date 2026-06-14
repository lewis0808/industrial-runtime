#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/types.hpp"

namespace core {

/// 流数据类型。图像/视频/点云/雷达/二进制流。
enum class StreamType : std::uint8_t {
    Binary = 0, ///< 通用二进制流（BinaryBlob）
    Frame,      ///< 图像/视频帧
    PointCloud, ///< 点云
};

/// Stream 数据包：core 仅作为路由载体，不解析内容。
///
/// 真正的流处理（解码、点云运算等）由顶层 stream/ 模块负责。
/// 禁止把图像放入 Tag、禁止把点云转 JSON。
struct StreamFrame {
    std::string source; ///< 流来源（插件 id）
    StreamType type{StreamType::Binary};
    Timestamp timestamp{};
    std::vector<std::uint8_t> payload; ///< 原始字节负载

    StreamFrame() = default;

    StreamFrame(std::string src, StreamType t, std::vector<std::uint8_t> data, Timestamp ts = now())
        : source(std::move(src)), type(t), timestamp(ts), payload(std::move(data)) {}

    [[nodiscard]] std::size_t size() const noexcept { return payload.size(); }
};

} // namespace core