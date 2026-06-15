"""msgpack 编解码（IRSP V2 编码层的纯 Python 实现）。

对外接口与 ``irsp1.py`` 一致（``encode_request`` / ``decode``），故 client 层切换编码
几乎无差别。关键差异：

- **数值 value 由服务端编为 msgpack 原生 int/float**，客户端 *无需* 再按 ``type``
  解小端字节（对比 irsp1 必须 ``decode_value(type, raw)``）。
- 字符串解码为 ``str``（irsp1 的 bulk 解为 ``bytes``）。
- 错误为 ``{err, msg}`` map；本模块在 *顶层* 自动转成 ``IrspError``，与 irsp1 行为一致。

详见 irsp/doc/encoding/msgpack.md。一个 WS 二进制消息 = 一个 IRSP 帧（一个顶层值）。
"""
from __future__ import annotations

import struct
from typing import Any, List, Tuple, Union

from .irsp1 import IrspError  # 复用同一异常类型，client 层对两种编码无差别处理

# 解码后的值：str / int / float / bytes / None / IrspError / list / dict。
IrspValue = Union[str, int, float, bytes, None, IrspError, list, dict]


# ============================ 编码 ============================

def _enc_int(v: int, out: bytearray) -> None:
    if 0 <= v <= 0x7f:
        out.append(v)  # positive fixint
    elif -32 <= v < 0:
        out.append(v & 0xff)  # negative fixint
    elif 0 <= v <= 0xff:
        out += bytes((0xcc, v))
    elif 0 <= v <= 0xffff:
        out.append(0xcd); out += struct.pack(">H", v)
    elif 0 <= v <= 0xffffffff:
        out.append(0xce); out += struct.pack(">I", v)
    elif 0 <= v <= 0xffffffffffffffff:
        out.append(0xcf); out += struct.pack(">Q", v)
    elif -0x80 <= v:
        out.append(0xd0); out += struct.pack(">b", v)
    elif -0x8000 <= v:
        out.append(0xd1); out += struct.pack(">h", v)
    elif -0x80000000 <= v:
        out.append(0xd2); out += struct.pack(">i", v)
    else:
        out.append(0xd3); out += struct.pack(">q", v)


def _enc_str(b: bytes, out: bytearray) -> None:
    n = len(b)
    if n <= 31:
        out.append(0xa0 | n)
    elif n <= 0xff:
        out.append(0xd9); out.append(n)
    elif n <= 0xffff:
        out.append(0xda); out += struct.pack(">H", n)
    else:
        out.append(0xdb); out += struct.pack(">I", n)
    out += b


def _enc_bin(b: bytes, out: bytearray) -> None:
    n = len(b)
    if n <= 0xff:
        out.append(0xc4); out.append(n)
    elif n <= 0xffff:
        out.append(0xc5); out += struct.pack(">H", n)
    else:
        out.append(0xc6); out += struct.pack(">I", n)
    out += b


def _enc(obj: Any, out: bytearray) -> None:
    if obj is None:
        out.append(0xc0)
    elif obj is True:
        out.append(0xc3)
    elif obj is False:
        out.append(0xc2)
    elif isinstance(obj, int):
        _enc_int(obj, out)
    elif isinstance(obj, float):
        out.append(0xcb); out += struct.pack(">d", obj)
    elif isinstance(obj, str):
        _enc_str(obj.encode("utf-8"), out)
    elif isinstance(obj, (bytes, bytearray)):
        _enc_bin(bytes(obj), out)
    elif isinstance(obj, (list, tuple)):
        _enc_arr_hdr(len(obj), out)
        for it in obj:
            _enc(it, out)
    elif isinstance(obj, dict):
        _enc_map_hdr(len(obj), out)
        for k, v in obj.items():
            _enc(k, out)
            _enc(v, out)
    else:
        raise TypeError(f"msgpack: 不支持的类型 {type(obj).__name__}")


def _enc_arr_hdr(n: int, out: bytearray) -> None:
    if n <= 15:
        out.append(0x90 | n)
    elif n <= 0xffff:
        out.append(0xdc); out += struct.pack(">H", n)
    else:
        out.append(0xdd); out += struct.pack(">I", n)


def _enc_map_hdr(n: int, out: bytearray) -> None:
    if n <= 15:
        out.append(0x80 | n)
    elif n <= 0xffff:
        out.append(0xde); out += struct.pack(">H", n)
    else:
        out.append(0xdf); out += struct.pack(">I", n)


def encode(obj: Any) -> bytes:
    """把任意值编码为 msgpack 字节（含 map/array，便于离线构造样例帧）。"""
    out = bytearray()
    _enc(obj, out)
    return bytes(out)


def encode_request(parts: List[Union[str, bytes]]) -> bytes:
    """把命令编码为 msgpack 请求帧（数组）。字符串→str，原始字节→bin。"""
    out = bytearray()
    _enc_arr_hdr(len(parts), out)
    for p in parts:
        if isinstance(p, str):
            _enc_str(p.encode("utf-8"), out)
        elif isinstance(p, (bytes, bytearray)):
            _enc_bin(bytes(p), out)
        else:
            _enc_str(str(p).encode("utf-8"), out)
    return bytes(out)


# ============================ 解码 ============================

def _take_str(data: bytes, i: int, n: int) -> Tuple[str, int]:
    return data[i:i + n].decode("utf-8"), i + n


def _take_bin(data: bytes, i: int, n: int) -> Tuple[bytes, int]:
    return bytes(data[i:i + n]), i + n


def _parse_arr(data: bytes, i: int, n: int) -> Tuple[list, int]:
    out: list = []
    for _ in range(n):
        v, i = _parse(data, i)
        out.append(v)
    return out, i


def _parse_map(data: bytes, i: int, n: int) -> Tuple[dict, int]:
    obj: dict = {}
    for _ in range(n):
        k, i = _parse(data, i)
        v, i = _parse(data, i)
        if isinstance(k, (bytes, bytearray)):
            k = bytes(k).decode("utf-8")
        obj[k] = v
    return obj, i


def _parse(data: bytes, i: int) -> Tuple[Any, int]:
    c = data[i]
    i += 1
    if c <= 0x7f:
        return c, i  # positive fixint
    if c >= 0xe0:
        return c - 0x100, i  # negative fixint
    if 0x80 <= c <= 0x8f:
        return _parse_map(data, i, c & 0x0f)
    if 0x90 <= c <= 0x9f:
        return _parse_arr(data, i, c & 0x0f)
    if 0xa0 <= c <= 0xbf:
        return _take_str(data, i, c & 0x1f)
    if c == 0xc0:
        return None, i
    if c == 0xc2:
        return False, i
    if c == 0xc3:
        return True, i
    if c == 0xcc:
        return data[i], i + 1
    if c == 0xcd:
        return struct.unpack_from(">H", data, i)[0], i + 2
    if c == 0xce:
        return struct.unpack_from(">I", data, i)[0], i + 4
    if c == 0xcf:
        return struct.unpack_from(">Q", data, i)[0], i + 8
    if c == 0xd0:
        return struct.unpack_from(">b", data, i)[0], i + 1
    if c == 0xd1:
        return struct.unpack_from(">h", data, i)[0], i + 2
    if c == 0xd2:
        return struct.unpack_from(">i", data, i)[0], i + 4
    if c == 0xd3:
        return struct.unpack_from(">q", data, i)[0], i + 8
    if c == 0xca:
        return struct.unpack_from(">f", data, i)[0], i + 4
    if c == 0xcb:
        return struct.unpack_from(">d", data, i)[0], i + 8
    if c == 0xc4:
        return _take_bin(data, i + 1, data[i])
    if c == 0xc5:
        return _take_bin(data, i + 2, struct.unpack_from(">H", data, i)[0])
    if c == 0xc6:
        return _take_bin(data, i + 4, struct.unpack_from(">I", data, i)[0])
    if c == 0xd9:
        return _take_str(data, i + 1, data[i])
    if c == 0xda:
        return _take_str(data, i + 2, struct.unpack_from(">H", data, i)[0])
    if c == 0xdb:
        return _take_str(data, i + 4, struct.unpack_from(">I", data, i)[0])
    if c == 0xdc:
        return _parse_arr(data, i + 2, struct.unpack_from(">H", data, i)[0])
    if c == 0xdd:
        return _parse_arr(data, i + 4, struct.unpack_from(">I", data, i)[0])
    if c == 0xde:
        return _parse_map(data, i + 2, struct.unpack_from(">H", data, i)[0])
    if c == 0xdf:
        return _parse_map(data, i + 4, struct.unpack_from(">I", data, i)[0])
    raise ValueError(f"msgpack: 未知首字节 0x{c:02x}")


def decode(data: bytes) -> IrspValue:
    """解码一个完整 msgpack 帧。顶层 ``{err,msg}`` map 自动转为 IrspError。"""
    value, _ = _parse(data, 0)
    if isinstance(value, dict) and "err" in value:
        code = value.get("err")
        msg = value.get("msg", "")
        return IrspError(str(code), str(msg))
    return value
