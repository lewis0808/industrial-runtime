#pragma once

#include <string>
#include <utility>

#include "common/types.hpp"

namespace core {

/// Tag 数据：PLC 变量、状态量、报警量、数值量的统一载体。
///
/// 字段严格对应项目规范，禁止用于存储图像/点云等流数据。
struct TagValue {
    std::string name;
    DataType type{DataType::Null};
    Timestamp timestamp{};
    Variant value{};

    TagValue() = default;

    /// 由名称与值构造，type 自动从 value 推导，timestamp 默认取当前时间。
    TagValue(std::string tagName, Variant tagValue, Timestamp ts = now())
        : name(std::move(tagName)), type(dataTypeOf(tagValue)), timestamp(ts),
          value(std::move(tagValue)) {}
};

} // namespace core