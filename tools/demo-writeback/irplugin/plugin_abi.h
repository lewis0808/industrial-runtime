#ifndef IRPLUGIN_ABI_H
#define IRPLUGIN_ABI_H

/*
 * 插件 ABI 契约（纯 C）。
 *
 * 这是 Runtime（宿主）与设备插件 DLL 之间唯一的二进制边界，面向设备接入。
 * 与对外访问协议 IRSP（Industrial Runtime Serialization Protocol）是两个正交的层，互不共用。
 *
 * 设计目标：ABI 稳定，不跨 DLL 传递任何 STL 对象 / 异常 / C++ 类。
 * 所有字符串以 (data, len) 传递，宿主在回调内同步拷贝，调用返回后指针即可失效。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI 版本。宿主接受 [IRPLUGIN_ABI_MIN_VERSION, IRPLUGIN_ABI_VERSION] 区间内的插件。
 * 结构体（IrPluginHostApi 等）仅末尾追加字段，旧插件只读已知前缀，向后兼容。
 * v2 起 IrPluginHostApi 追加 register_writer（写回）。
 * v3 起生命周期改为 C 函数指针 vtable（IrPluginInstance）：createPlugin 不再返回 C++
 * 对象，而是填充 out 参数。这是破坏性变更，故下方设最低支持版本拒绝更早的插件。 */
#define IRPLUGIN_ABI_VERSION 3u

/* 宿主可加载的最低插件 ABI 版本。低于此版本的 createPlugin 签名不兼容（v2 返回 C++
 * 对象指针），强行调用即 UB，故直接拒绝。 */
#define IRPLUGIN_ABI_MIN_VERSION 3u

/* 数据类型。取值顺序与 core::DataType 严格一致，便于 1:1 映射。 */
typedef enum IrPluginDataType {
    IRPLUGIN_TYPE_NULL = 0,
    IRPLUGIN_TYPE_BOOL,
    IRPLUGIN_TYPE_INT8,
    IRPLUGIN_TYPE_INT16,
    IRPLUGIN_TYPE_INT32,
    IRPLUGIN_TYPE_INT64,
    IRPLUGIN_TYPE_UINT8,
    IRPLUGIN_TYPE_UINT16,
    IRPLUGIN_TYPE_UINT32,
    IRPLUGIN_TYPE_UINT64,
    IRPLUGIN_TYPE_FLOAT,
    IRPLUGIN_TYPE_DOUBLE,
    IRPLUGIN_TYPE_STRING
} IrPluginDataType;

/* 事件严重级别。与 core::EventSeverity 一致。 */
typedef enum IrPluginSeverity {
    IRPLUGIN_SEV_INFO = 0,
    IRPLUGIN_SEV_WARNING,
    IRPLUGIN_SEV_ALARM,
    IRPLUGIN_SEV_CRITICAL
} IrPluginSeverity;

/* 流类型。与 core::StreamType 一致。 */
typedef enum IrPluginStreamType {
    IRPLUGIN_STREAM_BINARY = 0,
    IRPLUGIN_STREAM_FRAME,
    IRPLUGIN_STREAM_POINTCLOUD
} IrPluginStreamType;

/* 非拥有的字符串视图：不要求以 '\0' 结尾。 */
typedef struct IrPluginString {
    const char *data;
    size_t len;
} IrPluginString;

/* Tag 值变体。type 指示 as 中的有效成员。 */
typedef struct IrPluginVariant {
    IrPluginDataType type;
    union {
        uint8_t boolean; /* 0 / 1 */
        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        float f32;
        double f64;
        IrPluginString str;
    } as;
} IrPluginVariant;

/* Tag 数据。timestamp_ns 为 0 时由宿主填入当前时间。 */
typedef struct IrPluginTagValue {
    IrPluginString name;
    int64_t timestamp_ns; /* Unix 纪元以来的纳秒；0 = 宿主取当前时间 */
    IrPluginVariant value;
} IrPluginTagValue;

/* Event 数据。 */
typedef struct IrPluginEvent {
    IrPluginString source;
    IrPluginString category;
    IrPluginString message;
    int32_t severity;     /* IrPluginSeverity */
    int64_t timestamp_ns; /* 0 = 宿主取当前时间 */
} IrPluginEvent;

/* Stream 数据包。payload 由宿主在回调内同步拷贝。 */
typedef struct IrPluginStreamFrame {
    IrPluginString source;
    int32_t type;         /* IrPluginStreamType */
    int64_t timestamp_ns; /* 0 = 宿主取当前时间 */
    const uint8_t *payload;
    size_t payload_len;
} IrPluginStreamFrame;

/*
 * 写回处理器：宿主在收到应用 SET 时回调插件，让其把值写到设备。
 * plugin_ctx 为插件注册时给定的上下文，宿主原样回传。
 * 返回 1 表示已受理（已写出/已排队），0 表示未处理，<0 表示错误。
 */
typedef int (*IrPluginWriteFn)(void *plugin_ctx, const IrPluginTagValue *tag);

/*
 * 宿主 API：由 Runtime 实现并在 createPlugin 时传给插件。
 * 全部为 C 函数指针，返回 1 表示成功 / 已写入，0 表示失败 / 被丢弃。
 * ctx 为宿主内部上下文，插件必须原样回传。
 *
 * 注意：本结构体仅允许在末尾追加字段（宿主总是填满，旧插件只读其已知前缀，向后兼容）。
 */
typedef struct IrPluginHostApi {
    void *ctx;
    int (*push_tag)(void *ctx, const IrPluginTagValue *tag);
    int (*push_event)(void *ctx, const IrPluginEvent *event);
    int (*push_stream)(void *ctx, const IrPluginStreamFrame *frame);
    /* ABI v2 追加：注册写回处理器。插件声明它负责 prefix 前缀下 Tag 的写。 */
    void (*register_writer)(void *ctx, const char *prefix, void *plugin_ctx,
                            IrPluginWriteFn handler);
} IrPluginHostApi;

/* 插件元信息。字符串为插件内静态常量，生命周期等同 DLL 加载期。 */
typedef struct IrPluginInfo {
    uint32_t abi_version; /* 必须等于 IRPLUGIN_ABI_VERSION */
    const char *id;
    const char *name;
    const char *version;
} IrPluginInfo;

/*
 * 插件实例：C 函数指针 vtable + 不透明实例指针 self。
 *
 * 这是插件「生命周期」的唯一边界。全部为 C 函数指针，不依赖任何 C++ ABI 或 vtable 布局，
 * 故插件可用任意语言/编译器实现——只需按 C 调用约定填好下列指针即可。
 *
 * 所有权：宿主持有本结构体（POD，按值保存）；self 由插件分配，宿主在 destroy 时令插件释放。
 *         本结构体的函数指针指向插件 DLL 内的代码，在 destroy + 卸载库前始终有效。
 * 调用约定：宿主以 self 为首参调用各函数；init/start/stop/destroy 返回 1=成功 0=失败。
 * 顺序：createPlugin 填充本结构 -> init -> start -> ... -> stop -> destroy。
 *       destroy 之后宿主不再触碰 self，并随即卸载动态库。
 * 健壮性：插件必须填满全部四个函数指针；宿主发现任一为空即视为加载失败。
 */
typedef struct IrPluginInstance {
    void *self;
    int (*init)(void *self);
    int (*start)(void *self);
    int (*stop)(void *self);
    int (*destroy)(void *self);
} IrPluginInstance;

/*
 * 生命周期入口函数类型（即导出符号 getPluginInfo / createPlugin 的签名）。
 * 任意语言实现插件时，只需导出符合下列两个签名的 C 函数：
 *   - getPluginInfo(): 返回插件元信息（含 abi_version）。
 *   - createPlugin(host, config_path, out): 成功返回 1 并填充 *out，失败返回 0（*out 不变）。
 *     host 在插件存活期间有效；config_path 为该插件配置文件的完整路径（宿主透传，不读取内容）。
 */
typedef IrPluginInfo (*IrPluginGetInfoFn)(void);
typedef int (*IrPluginCreateFn)(const IrPluginHostApi *host, const char *config_path,
                                IrPluginInstance *out);

/* 导出符号名（宿主用 dlsym/GetProcAddress 解析）。 */
#define IRPLUGIN_SYM_GET_PLUGIN_INFO "getPluginInfo"
#define IRPLUGIN_SYM_CREATE_PLUGIN "createPlugin"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IRPLUGIN_ABI_H */
