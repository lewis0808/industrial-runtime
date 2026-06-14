"""IRSP WebSocket 客户端（asyncio，纯 Python，依赖 websockets）。

语义：请求-回复在连接上按 FIFO 顺序对应（RESP 风格，无请求 id）；
服务端主动推送的帧带 ``push`` 字段（"tag" / "event"），据此与回复区分。
"""
from __future__ import annotations

import asyncio
import collections
from dataclasses import dataclass
from typing import Any, Callable, Deque, Dict, List, Optional

import websockets

from .irsp1 import IrspError, as_str, decode, decode_value, encode_request


@dataclass
class TagValue:
    name: str
    type: str
    ts: int          # Unix 纪元纳秒（Python int 无精度问题）
    value: Any       # 已按 type 解码：bool/int/float/str/bytes/None
    quality: Optional[str] = None


@dataclass
class IrspEvent:
    source: str
    category: str
    severity: str
    ts: int
    message: str


def _is_push(v: Any) -> bool:
    return isinstance(v, dict) and "push" in v


class IrspClient:
    """异步 IRSP 客户端。

    用法::

        async with IrspClient("ws://127.0.0.1:9777") as c:
            c.on_tag(lambda t: print(t))
            await c.subscribe("plant/#")
            ...
    """

    def __init__(self, url: str) -> None:
        self.url = url
        self.server: Dict[str, Optional[str]] = {}
        self._ws: Any = None
        self._pending: Deque["asyncio.Future"] = collections.deque()
        self._reader: Optional[asyncio.Task] = None
        self._closing = False
        self._tag_cbs: List[Callable[[TagValue], Any]] = []
        self._event_cbs: List[Callable[[IrspEvent], Any]] = []

    # ---- 推送回调（可注册同步或协程函数） ----
    def on_tag(self, cb: Callable[[TagValue], Any]) -> "IrspClient":
        self._tag_cbs.append(cb)
        return self

    def on_event(self, cb: Callable[[IrspEvent], Any]) -> "IrspClient":
        self._event_cbs.append(cb)
        return self

    # ---- 连接 ----
    async def connect(self) -> "IrspClient":
        self._ws = await websockets.connect(self.url, subprotocols=["irsp"])
        self._reader = asyncio.create_task(self._read_loop())
        hello = await self._send(["HELLO", "1"])
        self.server = {k: as_str(v) for k, v in hello.items()}
        return self

    async def __aenter__(self) -> "IrspClient":
        return await self.connect()

    async def __aexit__(self, *exc: Any) -> None:
        await self.close()

    async def _read_loop(self) -> None:
        try:
            async for msg in self._ws:
                data = msg if isinstance(msg, (bytes, bytearray)) else msg.encode("utf-8")
                self._on_frame(bytes(data))
        except Exception:
            pass
        finally:
            while self._pending:
                fut = self._pending.popleft()
                if not fut.done():
                    fut.set_exception(IrspError("CLOSED", "connection closed"))

    def _on_frame(self, data: bytes) -> None:
        try:
            value = decode(data)
        except Exception:
            return
        if _is_push(value):
            kind = as_str(value.get("push"))
            if kind == "tag":
                self._dispatch(self._tag_cbs, self._decode_tag(value))
            elif kind == "event":
                self._dispatch(self._event_cbs, self._decode_event(value))
            return
        if self._pending:
            fut = self._pending.popleft()
            if not fut.done():
                if isinstance(value, IrspError):
                    fut.set_exception(value)
                else:
                    fut.set_result(value)

    @staticmethod
    def _dispatch(cbs: List[Callable], arg: Any) -> None:
        for cb in cbs:
            res = cb(arg)
            if asyncio.iscoroutine(res):
                asyncio.create_task(res)

    async def _send(self, parts: List[Any]) -> Any:
        if self._ws is None:
            raise IrspError("CLOSED", "未连接")
        fut: "asyncio.Future" = asyncio.get_running_loop().create_future()
        self._pending.append(fut)
        await self._ws.send(encode_request(parts))
        return await fut

    def _decode_tag(self, m: dict) -> TagValue:
        type_tag = as_str(m.get("type")) or "null"
        raw = m.get("value")
        tag = TagValue(
            name=as_str(m.get("name")) or "",
            type=type_tag,
            ts=int(m.get("ts") or 0),
            value=decode_value(type_tag, raw if isinstance(raw, (bytes, bytearray)) else None),
        )
        q = as_str(m.get("quality"))
        if q is not None:
            tag.quality = q
        return tag

    def _decode_event(self, m: dict) -> IrspEvent:
        return IrspEvent(
            source=as_str(m.get("source")) or "",
            category=as_str(m.get("category")) or "",
            severity=as_str(m.get("severity")) or "info",
            ts=int(m.get("ts") or 0),
            message=as_str(m.get("message")) or "",
        )

    # ---- Tag 读取 ----
    async def get(self, name: str) -> Optional[TagValue]:
        r = await self._send(["GET", name])
        return None if r is None else self._decode_tag(r)

    async def mget(self, names: List[str]) -> List[Optional[TagValue]]:
        r = await self._send(["MGET", *names])
        return [None if x is None else self._decode_tag(x) for x in r]

    async def exists(self, name: str) -> bool:
        return int(await self._send(["EXISTS", name])) != 0

    async def scan(self, cursor: str = "0", pattern: str = "#", count: int = 0) -> Dict[str, Any]:
        parts = ["SCAN", str(cursor), pattern]
        if count > 0:
            parts += ["COUNT", str(count)]
        r = await self._send(parts)
        return {"next_cursor": as_str(r[0]), "names": [as_str(x) for x in r[1]]}

    # ---- 订阅 ----
    async def watch(self, *names: str) -> int:
        return int(await self._send(["WATCH", *names]))

    async def unwatch(self, *names: str) -> int:
        return int(await self._send(["UNWATCH", *names]))

    async def subscribe(self, *patterns: str) -> int:
        return int(await self._send(["SUBSCRIBE", *patterns]))

    async def unsubscribe(self, *patterns: str) -> int:
        return int(await self._send(["UNSUBSCRIBE", *patterns]))

    async def subevent(self, min_severity: Optional[str] = None, category: Optional[str] = None) -> int:
        parts = ["SUBEVENT"]
        if min_severity:
            parts.append(min_severity)
        if category:
            parts.append(category)
        return int(await self._send(parts))

    async def unsubevent(self) -> int:
        return int(await self._send(["UNSUBEVENT"]))

    # ---- 连接管理 ----
    async def ping(self, payload: Optional[str] = None) -> Optional[str]:
        r = await self._send(["PING", payload] if payload else ["PING"])
        return as_str(r)

    async def bye(self) -> None:
        try:
            await self._send(["BYE"])
        except Exception:
            pass
        await self.close()

    async def close(self) -> None:
        self._closing = True
        if self._ws is not None:
            await self._ws.close()
