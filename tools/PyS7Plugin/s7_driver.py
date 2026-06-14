"""Python S7 驱动 —— 设备逻辑全部在这里（纯 Python）。

它由原生桥 `py_s7_plugin.dll`（嵌 CPython）加载并驱动：
  - 桥调用本模块的 init(host_ptr, config_path) / start() / stop() / destroy()；
  - host_ptr 是宿主 `IrPluginHostApi*` 的整数地址，我们用 ctypes 贴着 plugin_abi.h 的
    **纯 C 结构** 直接调 push_tag / push_event —— 这正是 C-ABI v3 让"任意语言"成立的方式。

采集对象：tools/S7ServerMock（snap7 S7 服务，:102，DB1 里有心跳/计数器/正弦等动态值）。
"""
from __future__ import annotations

import ctypes as C
import json
import struct
import threading
import time

import snap7.client  # 显式导入子模块，确保 snap7.client.Client 可用

# ===== 1. 镜像 core/irplugin/plugin_abi.h 的 C 结构（必须逐字段对齐）=====
# 数据类型枚举（与 IrPluginDataType 同序）
T_BOOL, T_INT16, T_INT32, T_INT64 = 1, 3, 4, 5
T_UINT16, T_UINT32 = 7, 8
T_FLOAT, T_DOUBLE, T_STRING = 10, 11, 12
SEV_INFO, SEV_WARNING = 0, 1


class IrPluginString(C.Structure):
    _fields_ = [("data", C.c_char_p), ("len", C.c_size_t)]


class _VariantUnion(C.Union):
    _fields_ = [
        ("boolean", C.c_uint8), ("i8", C.c_int8), ("i16", C.c_int16),
        ("i32", C.c_int32), ("i64", C.c_int64), ("u8", C.c_uint8),
        ("u16", C.c_uint16), ("u32", C.c_uint32), ("u64", C.c_uint64),
        ("f32", C.c_float), ("f64", C.c_double), ("strv", IrPluginString),
    ]


class IrPluginVariant(C.Structure):
    _fields_ = [("type", C.c_int), ("as_", _VariantUnion)]


class IrPluginTagValue(C.Structure):
    _fields_ = [("name", IrPluginString), ("timestamp_ns", C.c_int64), ("value", IrPluginVariant)]


class IrPluginEvent(C.Structure):
    _fields_ = [
        ("source", IrPluginString), ("category", IrPluginString),
        ("message", IrPluginString), ("severity", C.c_int32), ("timestamp_ns", C.c_int64),
    ]


# C 函数指针签名（与 IrPluginHostApi 一致）
_PUSH_TAG = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(IrPluginTagValue))
_PUSH_EVENT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.POINTER(IrPluginEvent))
_PUSH_STREAM = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
_REGISTER_WRITER = C.CFUNCTYPE(None, C.c_void_p, C.c_char_p, C.c_void_p, C.c_void_p)


class IrPluginHostApi(C.Structure):
    _fields_ = [
        ("ctx", C.c_void_p),
        ("push_tag", _PUSH_TAG),
        ("push_event", _PUSH_EVENT),
        ("push_stream", _PUSH_STREAM),
        ("register_writer", _REGISTER_WRITER),
    ]


# ===== 2. 模块级状态（桥按 init→start→stop→destroy 顺序调用，单实例）=====
_host = None            # type: C.POINTER(IrPluginHostApi)
_cfg: dict = {}
_client = None          # snap7 client
_stop = threading.Event()
_worker = None          # type: threading.Thread


def _push_tag(name: str, dtype: int, setter) -> None:
    """构造 IrPluginTagValue 并调宿主 push_tag。setter(variant) 负责填 union。"""
    tag = IrPluginTagValue()
    nb = name.encode("utf-8")           # 调用期间保持引用（栈上 nb 即可）
    tag.name.data = nb
    tag.name.len = len(nb)
    tag.timestamp_ns = 0                # 0 = 宿主取当前时间
    tag.value.type = dtype
    setter(tag.value.as_)
    _host.contents.push_tag(_host.contents.ctx, C.byref(tag))


def _push_event(source: str, category: str, message: str, severity: int = SEV_INFO) -> None:
    ev = IrPluginEvent()
    sb, cb, mb = source.encode(), category.encode(), message.encode("utf-8")
    ev.source.data, ev.source.len = sb, len(sb)
    ev.category.data, ev.category.len = cb, len(cb)
    ev.message.data, ev.message.len = mb, len(mb)
    ev.severity = severity
    ev.timestamp_ns = 0
    _host.contents.push_event(_host.contents.ctx, C.byref(ev))


# kind -> (DataType, 从 big-endian 缓冲解析, 填 union)
def _decode(kind: str, buf: bytes, item: dict):
    off = item["offset"]
    if kind == "bool":
        v = bool(buf[off] & (1 << item.get("bit", 0)))
        return T_BOOL, (lambda u: setattr(u, "boolean", 1 if v else 0))
    if kind == "uint16":
        v = struct.unpack_from(">H", buf, off)[0]
        return T_UINT16, (lambda u: setattr(u, "u16", v))
    if kind == "int16":
        v = struct.unpack_from(">h", buf, off)[0]
        return T_INT16, (lambda u: setattr(u, "i16", v))
    if kind == "int32":
        v = struct.unpack_from(">i", buf, off)[0]
        return T_INT32, (lambda u: setattr(u, "i32", v))
    if kind == "real":
        v = struct.unpack_from(">f", buf, off)[0]
        return T_FLOAT, (lambda u: setattr(u, "f32", v))
    raise ValueError(f"未支持的 kind: {kind}")


def _ensure_connected() -> bool:
    """惰性连接：设备可能晚于运行时启动，断线也在此自愈。"""
    if _client.get_connected():
        return True
    s7 = _cfg["s7"]
    try:
        _client.connect(s7["ip"], int(s7.get("rack", 0)), int(s7.get("slot", 1)),
                        int(s7.get("port", 102)))
        if _client.get_connected():
            _push_event("py-s7", "state", f"connected to {s7['ip']}:{s7.get('port', 102)}")
            return True
    except Exception as e:
        _push_event("py-s7", "error", f"connect failed: {e}", SEV_WARNING)
    return False


def _loop() -> None:
    """采集线程：连上则定时读 DB 逐 tag push；连不上/读失败则下轮重连。异常不静默退出线程。"""
    s7 = _cfg["s7"]
    tags = _cfg["tags"]
    prefix = tags.get("prefix", "")
    db = int(s7.get("db", 1))
    size = int(s7.get("read_size", 64))
    poll = float(s7.get("poll_ms", 500)) / 1000.0
    while not _stop.is_set():
        if not _ensure_connected():
            _stop.wait(poll)
            continue
        try:
            buf = bytes(_client.db_read(db, 0, size))
            for item in tags["items"]:
                dtype, setter = _decode(item["kind"], buf, item)
                _push_tag(prefix + item["name"], dtype, setter)
        except Exception as e:  # 读失败：报事件并断开，下轮重连
            _push_event("py-s7", "error", f"read failed: {e}", SEV_WARNING)
            try:
                _client.disconnect()
            except Exception:
                pass
        _stop.wait(poll)


# ===== 3. 桥调用的生命周期入口 =====
def init(host_ptr: int, config_path: str) -> bool:
    global _host, _cfg, _client
    _host = C.cast(host_ptr, C.POINTER(IrPluginHostApi))
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            _cfg = json.load(f)
    except OSError:
        # 配置缺失则用内置默认（对准 S7ServerMock）
        _cfg = {
            "s7": {"ip": "127.0.0.1", "rack": 0, "slot": 1, "db": 1, "read_size": 64, "poll_ms": 500},
            "tags": {"prefix": "s7/db1/", "items": [
                {"name": "heartbeat", "kind": "bool", "offset": 0, "bit": 0},
                {"name": "counter", "kind": "uint16", "offset": 8},
                {"name": "sine", "kind": "real", "offset": 46},
            ]},
        }
    _client = snap7.client.Client()
    # 不在 init 里连接：设备可能晚于运行时启动。连接交给采集循环（_ensure_connected），
    # 断线自动重连。init 只要环境就绪即成功，插件总能 start。
    return True


def start() -> bool:
    global _worker
    _stop.clear()
    _worker = threading.Thread(target=_loop, name="py-s7-poll", daemon=True)
    _worker.start()
    _push_event("py-s7", "state", "python s7 driver started")
    return True


def stop() -> bool:
    _stop.set()
    if _worker is not None:
        _worker.join(timeout=2.0)
    _push_event("py-s7", "state", "python s7 driver stopped")
    return True


def destroy() -> bool:
    if _client is not None:
        try:
            _client.disconnect()
        except Exception:
            pass
    return True
