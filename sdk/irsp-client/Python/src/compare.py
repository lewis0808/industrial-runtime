"""并排对比 irsp1 与 msgpack 两种编码（离线，无需服务端）。

运行： PYTHONPATH=src python examples/compare.py
"""
import struct

from irsp_client import irsp1
from irsp_client import msgpack_codec as mp


def main() -> None:
    # ---- 1) 请求方向：同一条命令两种编码 ----
    cmd = ["GET", "system/heartbeat"]
    a = irsp1.encode_request(cmd)
    b = mp.encode_request(cmd)
    print("== 请求 GET system/heartbeat ==")
    print(f"  irsp1   {len(a):>3} 字节  {a!r}")
    print(f"  msgpack {len(b):>3} 字节  {b.hex(' ')}")

    # ---- 2) 回复方向：一个 TagValue(f64=36.5) 两种编码 + 解码 ----
    ts = 1749800000000000000
    name = "factory1/line1/robot1/temp"

    def irsp1_bulk(s):
        if isinstance(s, str):
            s = s.encode()
        return b"$" + str(len(s)).encode() + b"\r\n" + s + b"\r\n"

    irsp1_reply = (
        b"%4\r\n"
        + irsp1_bulk("name") + irsp1_bulk(name)
        + irsp1_bulk("type") + irsp1_bulk("f64")
        + irsp1_bulk("ts") + b":" + str(ts).encode() + b"\r\n"
        + irsp1_bulk("value") + irsp1_bulk(struct.pack("<d", 36.5))
    )
    mp_reply = mp.encode({"name": name, "type": "f64", "ts": ts, "value": 36.5})

    print("\n== 回复 TagValue(f64=36.5) ==")
    print(f"  irsp1   {len(irsp1_reply):>3} 字节")
    print(f"  msgpack {len(mp_reply):>3} 字节  {mp_reply.hex(' ')}")

    m1 = irsp1.decode(irsp1_reply)
    v1raw = m1["value"]                          # bytes（小端）
    v1 = irsp1.decode_value("f64", v1raw)        # ← 必须按 type 再解一步
    m2 = mp.decode(mp_reply)
    v2 = m2["value"]                             # ← 已是原生 float

    print("\n== value 字段拿到手的差异 ==")
    print(f"  irsp1   {v1raw!r}")
    print(f"          → decode_value('f64', …) → {v1!r}  ({type(v1).__name__})")
    print(f"  msgpack {v2!r}  ({type(v2).__name__})  ← 无需再解，msgpack 库直接给原生值")


if __name__ == "__main__":
    main()
