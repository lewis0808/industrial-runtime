"""IRP (Industrial Runtime Protocol) 客户端 SDK（纯 Python，asyncio）。

惰性加载：`resp1`（编解码）零依赖，可单独使用；只有访问 `IrpClient` 等时
才会导入 `client` 模块（依赖 websockets）。
"""
from __future__ import annotations

from typing import Any

__version__ = "1.0.0"
__all__ = [
    "IrpClient",
    "TagValue",
    "IrpEvent",
    "IrpError",
    "encode_request",
    "decode",
    "decode_value",
    "as_str",
    "__version__",
]

_RESP1 = {"IrpError", "encode_request", "decode", "decode_value", "as_str"}
_CLIENT = {"IrpClient", "TagValue", "IrpEvent"}


def __getattr__(name: str) -> Any:  # PEP 562
    if name in _RESP1:
        from . import resp1
        return getattr(resp1, name)
    if name in _CLIENT:
        from . import client
        return getattr(client, name)
    raise AttributeError(f"module 'irp_client' has no attribute {name!r}")
