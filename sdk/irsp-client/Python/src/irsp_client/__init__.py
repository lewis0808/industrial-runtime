"""IRSP (Industrial Runtime Protocol) 客户端 SDK（纯 Python，asyncio）。

惰性加载：`irsp1`（编解码）零依赖，可单独使用；只有访问 `IrspClient` 等时
才会导入 `client` 模块（依赖 websockets）。
"""
from __future__ import annotations

from typing import Any

__version__ = "1.0.0"
__all__ = [
    "IrspClient",
    "TagValue",
    "IrspEvent",
    "IrspError",
    "encode_request",
    "decode",
    "decode_value",
    "as_str",
    "__version__",
]

_IRSP1 = {"IrspError", "encode_request", "decode", "decode_value", "as_str"}
_CLIENT = {"IrspClient", "TagValue", "IrspEvent"}


def __getattr__(name: str) -> Any:  # PEP 562
    if name in _IRSP1:
        from . import irsp1
        return getattr(irsp1, name)
    if name in _CLIENT:
        from . import client
        return getattr(client, name)
    raise AttributeError(f"module 'irsp_client' has no attribute {name!r}")
