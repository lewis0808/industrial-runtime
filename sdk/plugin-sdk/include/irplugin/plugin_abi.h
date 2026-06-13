#ifndef IRPLUGIN_ABI_H
#define IRPLUGIN_ABI_H

/*
 * 插件 ABI 契约（纯 C）。
 *
 * 这是 Runtime（宿主）与设备插件 DLL 之间唯一的二进制边界，面向设备接入。
 * 与对外访问协议 IRP（Industrial Runtime Protocol）是两个正交的层，互不共用。
 *
 * 设计目标：ABI 稳定，不跨 DLL 传递任何 STL 对象 / 异常 / C++ 类。
 * 所有字符串以 (data, len) 传递，宿主在回调内同步拷贝，调用返回后指针即可失效。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI 版本。插件与宿主必须一致，否则拒绝加载。 */
#define IRPLUGIN_ABI_VERSION 1u

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
    const char* data;
    size_t len;
} IrPluginString;

/* Tag 值变体。type 指示 as 中的有效成员。 */
typedef struct IrPluginVariant {
    IrPluginDataType type;
    union {
        uint8_t  boolean; /* 0 / 1 */
        int8_t   i8;
        int16_t  i16;
        int32_t  i32;
        int64_t  i64;
        uint8_t  u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        float    f32;
        double   f64;
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
    const uint8_t* payload;
    size_t payload_len;
} IrPluginStreamFrame;

/*
 * 宿主 API：由 Runtime 实现并在 createPlugin 时传给插件。
 * 全部为 C 函数指针，返回 1 表示成功 / 已写入，0 表示失败 / 被丢弃。
 * ctx 为宿主内部上下文，插件必须原样回传。
 */
typedef struct IrPluginHostApi {
    void* ctx;
    int (*push_tag)(void* ctx, const IrPluginTagValue* tag);
    int (*push_event)(void* ctx, const IrPluginEvent* event);
    int (*push_stream)(void* ctx, const IrPluginStreamFrame* frame);
} IrPluginHostApi;

/* 插件元信息。字符串为插件内静态常量，生命周期等同 DLL 加载期。 */
typedef struct IrPluginInfo {
    uint32_t abi_version; /* 必须等于 IRPLUGIN_ABI_VERSION */
    const char* id;
    const char* name;
    const char* version;
} IrPluginInfo;

/* 导出符号名（宿主用 dlsym/GetProcAddress 解析）。 */
#define IRPLUGIN_SYM_GET_PLUGIN_INFO "getPluginInfo"
#define IRPLUGIN_SYM_CREATE_PLUGIN   "createPlugin"

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* IRPLUGIN_ABI_H */
