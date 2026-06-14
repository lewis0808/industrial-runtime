"""irsp1 编解码（IRSP V1 编码层的纯 Python 实现）。

一个 WebSocket 二进制消息 = 一个 IRSP 帧（一个顶层 IrspValue）。详见 irsp/encoding/irsp1.md。
"""
from __future__ import annotations

import struct
from typing import Any, List, Optional, Union


class IrspError(Exception):
    """IRSP 错误回复（irsp1 的 ``-CODE message``）。"""

    def __init__(self, code: str, message: str = "") -> None:
        super().__init__(f"{code} {message}".strip())
        self.code = code
        self.message = message


# 解码后的值：str（simple）/ int（integer）/ bytes（bulk）/ None / IrspError / list / dict（map）。
IrspValue = Union[str, int, bytes, None, IrspError, list, dict]

_CRLF = b"\r\n"


def encode_request(parts: List[Union[str, bytes]]) -> bytes:
    """把命令编码为 irsp1 请求帧（bulk 数组）。字符串按 UTF-8，或直接传原始字节。"""
    out = bytearray()
    out += f"*{len(parts)}\r\n".encode()
    for p in parts:
        b = p.encode("utf-8") if isinstance(p, str) else bytes(p)
        out += f"${len(b)}\r\n".encode()
        out += b
        out += _CRLF
    return bytes(out)


def as_str(v: Any) -> Optional[str]:
    """把 bulk(bytes) 或简单字符串转为 str。"""
    if v is None:
        return None
    if isinstance(v, str):
        return v
    if isinstance(v, (bytes, bytearray)):
        return bytes(v).decode("utf-8")
    return str(v)


def decode(data: bytes) -> IrspValue:
    """解码一个完整 irsp1 帧。"""
    n = len(data)
    i = 0

    def read_line() -> str:
        nonlocal i
        j = i
        while j + 1 < n and not (data[j] == 13 and data[j + 1] == 10):
            j += 1
        line = bytes(data[i:j]).decode("utf-8")
        i = j + 2
        return line

    def parse() -> IrspValue:
        nonlocal i
        if i >= n:
            raise ValueError("irsp1: 帧不完整")
        t = chr(data[i])
        i += 1
        line = read_line()
        if t == "+":
            return line
        if t == "-":
            sp = line.find(" ")
            return IrspError(line, "") if sp < 0 else IrspError(line[:sp], line[sp + 1:])
        if t == ":":
            return int(line)
        if t == "$":
            ln = int(line)
            if ln < 0:
                return None
            b = bytes(data[i:i + ln])
            i += ln + 2
            return b
        if t == "*":
            cnt = int(line)
            if cnt < 0:
                return None
            return [parse() for _ in range(cnt)]
        if t == "%":
            cnt = int(line)
            obj = {}
            for _ in range(cnt):
                k = parse()
                v = parse()
                obj[as_str(k)] = v
            return obj
        raise ValueError(f"irsp1: 未知类型 '{t}'")

    return parse()


_FMT = {
    "i8": "<b", "i16": "<h", "i32": "<i", "i64": "<q",
    "u8": "<B", "u16": "<H", "u32": "<I", "u64": "<Q",
    "f32": "<f", "f64": "<d",
}


def decode_value(type_tag: str, b: Optional[bytes]) -> Any:
    """类型标签 + 原始字节 → Python 值（小端，见 datatype.md）。"""
    if b is None or type_tag == "null":
        return None
    if type_tag == "bool":
        return b[0] != 0
    if type_tag == "str":
        return bytes(b).decode("utf-8")
    fmt = _FMT.get(type_tag)
    if fmt is not None:
        return struct.unpack(fmt, b)[0]
    return b  # 未知类型保留原始字节
