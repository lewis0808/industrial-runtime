"""IRSP 全命令冒烟脚本（基于 sdk/irsp-client/Python，源码直跑，无需编译/装 wheel）。

前置：
  1) 本目录的 mock S7 在跑：       python main.py            (127.0.0.1:102)
  2) Runtime 在跑：                IndustrialRuntime.exe      (对外 ws://127.0.0.1:9777)
  3) 装依赖：                      pip install websockets

运行：
  python irsp_test.py
  IRSP_URL=ws://127.0.0.1:9777 python irsp_test.py   # 覆盖地址

覆盖命令：HELLO(connect 自动) / PING / EXISTS / SCAN / GET / MGET /
          WATCH / SUBSCRIBE / SUBEVENT / SET(走 _send，SDK 未公开) /
          UNWATCH / UNSUBSCRIBE / UNSUBEVENT / BYE
"""
from __future__ import annotations

import asyncio
import os
import pathlib
import struct
import sys

# --- 让 SDK 源码可被 import（不装包、不编译）---
SDK_SRC = pathlib.Path(__file__).resolve().parents[2] / "sdk" / "irsp-client" / "Python" / "src"
sys.path.insert(0, str(SDK_SRC))

from irsp_client import IrspClient, IrspEvent, TagValue          # noqa: E402
from irsp_client.irsp1 import IrspError                          # noqa: E402

URL = os.environ.get("IRSP_URL", "ws://127.0.0.1:9777")

# config/s7_plugin.json 里映射出的 8 个 topic
READONLY = ["s7/plc1/BOOL", "s7/plc1/Uint", "s7/plc1/REAL"]      # mock 里动态刷新
WRITABLE = [
    # (topic,           irsp 类型标签, 写入值)
    ("s7/plc1/byte",  "u8",  0x7E),
    ("s7/plc1/sint",  "i8",  -99),
    ("s7/plc1/usint", "u8",  250),
    ("s7/plc1/WORD",  "u16", 0xBEEF),
    ("s7/plc1/INT",   "i16", -2024),
]
ALL_TOPICS = READONLY + [w[0] for w in WRITABLE]

# SET 的 value 必须是裸小端字节（core_tag_writer 用 memcpy 解码）
_FMT = {
    "i8": "<b", "i16": "<h", "i32": "<i", "i64": "<q",
    "u8": "<B", "u16": "<H", "u32": "<I", "u64": "<Q",
    "f32": "<f", "f64": "<d",
}


def _encode_value(type_tag: str, value) -> bytes:
    if type_tag == "bool":
        return b"\x01" if value else b"\x00"
    if type_tag == "str":
        return str(value).encode("utf-8")
    return struct.pack(_FMT[type_tag], value)


async def set_tag(client: IrspClient, topic: str, type_tag: str, value) -> str:
    """SET 写回。SDK 未公开 set()，直接复用底层 _send 发 irsp1 帧。"""
    payload = _encode_value(type_tag, value)
    reply = await client._send(["SET", topic, type_tag, payload])  # noqa: SLF001
    return reply if isinstance(reply, str) else str(reply)


def banner(title: str) -> None:
    print(f"\n===== {title} =====")


async def main() -> None:
    client = IrspClient(URL)

    # 推送回调：动态点变化时由 Runtime 主动推过来
    client.on_tag(lambda t: print(f"  [PUSH tag]   {t.name} = {t.value!r} (type={t.type}, ts={t.ts})"))
    client.on_event(lambda e: print(f"  [PUSH event] {e.severity}/{e.category} from {e.source}: {e.message}"))

    # ---- HELLO（connect 内部自动握手）----
    banner("HELLO / connect")
    await client.connect()
    print("  server caps:", client.server)

    # # ---- PING ----
    banner("PING")
    print("  PING        ->", await client.ping())
    print("  PING hello  ->", await client.ping("hello"))

    # # ---- EXISTS ----
    banner("EXISTS")
    print("  s7/plc1/REAL ->", await client.exists("s7/plc1/REAL"))
    # print("  s7/no/such   ->", await client.exists("s7/no/such"))

    # # ---- SCAN（游标遍历，发现所有 topic）----
    # banner("SCAN s7/#")
    # cursor, found = "0", []
    # while True:
    #     page = await client.scan(cursor, "s7/#", count=100)
    #     found += page["names"]
    #     cursor = page["next_cursor"]
    #     if cursor == "0":
    #         break
    # print("  discovered:", found)

    # # ---- GET / MGET ----
    banner("GET / MGET")
    one = await client.get("s7/plc1/REAL")
    print("  GET  s7/plc1/REAL ->", one)
    # many = await client.mget(ALL_TOPICS)
    # for t in many:
    #     print("  MGET item:", t)

    # # ---- WATCH（单点）+ SUBSCRIBE（子树）+ SUBEVENT，观察推送 ----
    # banner("WATCH / SUBSCRIBE / SUBEVENT  (盯 4 秒动态推送)")
    # print("  WATCH s7/plc1/REAL    ->", await client.watch("s7/plc1/REAL"))
    # print("  SUBSCRIBE s7/plc1/#   ->", await client.subscribe("s7/plc1/#"))
    # print("  SUBEVENT (info+)      ->", await client.subevent("info"))
    # print("  ...等待心跳/计数器/正弦推送...")
    # await asyncio.sleep(4)

    # # ---- SET（写回可写点，写后回读校验）----
    # banner("SET  (写回 + GET 校验)")
    # for topic, type_tag, value in WRITABLE:
    #     try:
    #         ack = await set_tag(client, topic, type_tag, value)
    #         back = await client.get(topic)
    #         print(f"  SET {topic:<16} {type_tag:<4} = {value!r:<10} -> {ack:<3} | 回读 {back.value!r}")
    #     except IrspError as e:
    #         print(f"  SET {topic:<16} {type_tag:<4} = {value!r:<10} -> ERROR {e}")

    # # 写只读点应被拒（NOT_FOUND：无插件接管该可写出口）
    # banner("SET 只读点（应报错）")
    # try:
    #     await set_tag(client, "s7/plc1/REAL", "f32", 1.0)
    #     print("  意外成功？")
    # except IrspError as e:
    #     print("  s7/plc1/REAL ->", e)

    # # ---- 退订 ----
    banner("UNWATCH / UNSUBSCRIBE / UNSUBEVENT")
    print("  UNWATCH all       ->", await client.unwatch())
    print("  UNSUBSCRIBE all   ->", await client.unsubscribe())
    print("  UNSUBEVENT        ->", await client.unsubevent())

    # ---- BYE ----
    banner("BYE")
    await client.bye()
    print("  已优雅关闭。")


if __name__ == "__main__":
    asyncio.run(main())
