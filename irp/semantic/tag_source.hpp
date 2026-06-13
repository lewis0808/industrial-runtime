#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace irp {

/// 协议层的 Tag 记录（已是 IRP 表示：类型标签 + 值字节）。
/// 由绑定层（增量 2）从 core::TagValue 转换而来；语义层不感知 core 类型。
struct TagRecord {
    std::string name;
    std::string type;    ///< 类型标签（"f64"/"i32"/"str"/"bool"...，见 datatype.md）
    std::int64_t ts_ns{0};
    std::string value;   ///< 值字节（数值小端 / str 为 UTF-8）
};

/// SCAN 结果：自描述游标 + 本批 topic 名。
struct ScanResult {
    std::string nextCursor;  ///< "0" 表示遍历结束
    std::vector<std::string> names;
};

/// 只读 Tag 数据源。语义层经此接口读取，与具体存储（core::TagEngine）解耦。
class TagSource {
public:
    virtual ~TagSource() = default;

    [[nodiscard]] virtual std::optional<TagRecord> read(const std::string& name) const = 0;
    [[nodiscard]] virtual bool exists(const std::string& name) const = 0;
    [[nodiscard]] virtual ScanResult scan(const std::string& cursor,
                                          const std::string& pattern,
                                          std::size_t count) const = 0;
};

}  // namespace irp
