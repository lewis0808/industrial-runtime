#pragma once

#include <optional>
#include <string>

#include "semantic/tag_source.hpp"
#include "tag_engine/tag_engine.hpp"

namespace irp {

/// 把 core::TagEngine 适配为 IRP 语义层的只读 TagSource。
///
/// 负责 core::TagValue -> 协议 TagRecord 的封送（类型标签 + 小端字节，见 datatype.md），
/// 以及无状态游标的 SCAN。仅读取，不改 core。
///
/// 注意：数值以主机字节序写入（目标平台 x64 为小端）。
class CoreTagSource final : public TagSource {
  public:
    explicit CoreTagSource(const core::TagEngine &engine) noexcept : engine_(&engine) {}

    [[nodiscard]] std::optional<TagRecord> read(const std::string &name) const override;
    [[nodiscard]] bool exists(const std::string &name) const override;
    [[nodiscard]] ScanResult scan(const std::string &cursor, const std::string &pattern,
                                  std::size_t count) const override;

  private:
    const core::TagEngine *engine_;
};

} // namespace irp
