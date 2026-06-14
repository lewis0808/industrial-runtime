# 数据模型（common/）

`common/` 是**仅头文件、零依赖**的类型根，为各模块提供中立的共享类型，避免模块互相
`#include` 产生环。三套数据体系严格分离：**Tag**（实时变量）、**Event**（报警/状态/通知）、
**Stream**（图像/点云/二进制）。

## 1. 基础类型（types.hpp）

```cpp
using Timestamp = std::chrono::system_clock::time_point;   // 墙钟，可序列化
Timestamp now();

enum class DataType : uint8_t { Null, Bool, Int8..Int64, UInt8..UInt64, Float, Double, String };

using Variant = std::variant<std::monostate, bool,
        int8_t,int16_t,int32_t,int64_t, uint8_t,uint16_t,uint32_t,uint64_t,
        float, double, std::string>;
```

### ⭐ 关键不变量：DataType 顺序 == Variant 备选顺序

`DataType` 的枚举值与 `Variant` 的备选类型**一一对应**，因此可直接互推：

```cpp
DataType dataTypeOf(const Variant& v) { return static_cast<DataType>(v.index()); }
```

这是个**隐式契约**：任何对其中一者重排序的改动都会静默错位。
→ 见 [roadmap](roadmap.md)：建议加 `static_assert` 锁死对应关系。

- **禁止 `void*`**；二进制/图像/点云属于 Stream，不得塞进 `Variant`。
- `dataTypeName(DataType)` 返回可读名（日志/序列化用）。

## 2. TagValue（tag_value.hpp）

```cpp
struct TagValue { std::string name; DataType type; Timestamp timestamp; Variant value; };
```

- **用途**：PLC 变量、状态量、报警量、数值量。
- 便利构造 `TagValue(name, value, ts=now())`：`type` 由 `value` **自动推导**，时间戳默认当前。
- `name` 即 IRSP **Topic**（`/` 分层，如 `factory1/line1/temp`）；命名规范见根 `CLAUDE.md`。
- **禁止**承载图像/点云。

## 3. Event（event.hpp）

```cpp
enum class EventSeverity : uint8_t { Info, Warning, Alarm, Critical };
struct Event { std::string source, category, message; EventSeverity severity; Timestamp timestamp; };
```

- **用途**：报警、状态变化、系统通知。`source`=插件 id/模块名，`category`=`alarm`/`state`/`system` 等。
- **禁止**用于存储实时变量（那是 Tag 的职责）。
- EventBus 的 `Filter` 按 `severity` 下限 + 可选 `category` 精确匹配过滤。

## 4. StreamFrame（stream.hpp）

```cpp
enum class StreamType : uint8_t { Binary, Frame, PointCloud };
struct StreamFrame { std::string source; StreamType type; Timestamp timestamp; std::vector<uint8_t> payload; };
```

- **用途**：图像/视频帧、点云、通用二进制流。
- **Core 不解析流内容**，仅作路由载体；真正的解码/点云运算由顶层 `stream/` 模块负责。
- **禁止**把图像放进 Tag、把点云转 JSON。

## 5. 与插件 C-ABI 的映射

`common/` 的 C++ 类型与 `irplugin/plugin_abi.h` 的纯 C 结构一一对应（枚举取值刻意保持一致，
便于 1:1 映射）。封送在 `PluginHost` 完成：

| C++（core 内部） | C ABI（跨 DLL） |
|------------------|------------------|
| `Variant` | `IrPluginVariant`（tagged union） |
| `DataType` | `IrPluginDataType`（同序枚举） |
| `TagValue` | `IrPluginTagValue` |
| `Event` | `IrPluginEvent` |
| `StreamFrame` | `IrPluginStreamFrame` |
| `Timestamp` | `int64_t timestamp_ns`（0 = 宿主取当前时间） |

字符串跨边界一律 `IrPluginString{data,len}`（非拥有、不要求 `\0` 结尾），宿主在回调内同步拷贝。

## 待改善

- **DataType↔Variant 对应无编译期校验**：加 `static_assert` 把每个枚举值与
  `Variant` 对应 alternative 的 `index()` 锁死，防止重排序静默错位。
- **无 quality/质量戳**：工业场景常需 OPC 风格的质量字段（good/bad/uncertain）；
  当前 TagValue 无此维度（IRSP 数据模型已预留 map 扩展，core 侧未落地）。
- **Timestamp 为 system_clock**：墙钟受 NTP 跳变影响；高精度场景或需单调时钟 + 墙钟双戳。
