#include <string>

#include "common/tag_value.hpp"
#include "common/types.hpp"
#include "tests/test_util.hpp"

int main() {
    using namespace core;

    // Variant 索引与 DataType 顺序一致。
    IR_CHECK_EQ(dataTypeOf(Variant{}), DataType::Null);
    IR_CHECK_EQ(dataTypeOf(Variant{true}), DataType::Bool);
    IR_CHECK_EQ(dataTypeOf(Variant{std::int32_t{1}}), DataType::Int32);
    IR_CHECK_EQ(dataTypeOf(Variant{std::int64_t{1}}), DataType::Int64);
    IR_CHECK_EQ(dataTypeOf(Variant{double{1.0}}), DataType::Double);
    IR_CHECK_EQ(dataTypeOf(Variant{std::string{"x"}}), DataType::String);

    // TagValue 构造自动推导类型。
    TagValue t{"temp", 3.14};
    IR_CHECK_EQ(t.name, std::string{"temp"});
    IR_CHECK_EQ(t.type, DataType::Double);

    IR_CHECK(dataTypeName(DataType::Float) == std::string{"Float"});

    IR_TEST_REPORT();
}
