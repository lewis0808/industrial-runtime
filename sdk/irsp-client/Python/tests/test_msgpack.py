"""msgpack 编解码单测（无需服务端）。

pytest：  pytest
直跑：    PYTHONPATH=src python tests/test_msgpack.py
"""
import struct

from irsp_client.irsp1 import IrspError
from irsp_client.msgpack_codec import decode, encode, encode_request


def test_encode_request():
    # array2 + fixstr3 "GET" + fixstr3 "a/b"
    assert encode_request(["GET", "a/b"]) == b"\x92\xa3GET\xa3a/b"


def test_encode_scalars():
    assert encode(None) == b"\xc0"
    assert encode(5) == b"\x05"          # positive fixint
    assert encode(-1) == b"\xff"         # negative fixint
    assert encode(200) == b"\xcc\xc8"    # uint8
    assert encode(True) == b"\xc3"
    assert encode(False) == b"\xc2"
    assert encode("hi") == b"\xa2hi"     # fixstr
    assert encode(36.5) == b"\xcb" + struct.pack(">d", 36.5)


def test_roundtrip_request():
    assert decode(encode_request(["HELLO", "1"])) == ["HELLO", "1"]


def test_decode_native_value():
    # 服务端把 TagValue.value 编为原生 f64 —— 客户端直接拿到 float，无需再解。
    m = decode(encode({"name": "a/b", "type": "f64", "ts": 123, "value": 36.5}))
    assert m["name"] == "a/b"            # 字符串直接是 str
    assert m["ts"] == 123                # 整数原生
    assert m["value"] == 36.5           # ← 原生 float
    assert isinstance(m["value"], float)


def test_decode_big_int():
    assert decode(encode(1749800000000000000)) == 1749800000000000000


def test_error_map_to_exception():
    # 顶层 {err,msg} 自动转 IrspError，与 irsp1 行为一致。
    err = decode(encode({"err": "NOT_READY", "msg": "HELLO required first"}))
    assert isinstance(err, IrspError)
    assert err.code == "NOT_READY"
    assert err.message == "HELLO required first"


if __name__ == "__main__":
    failures = 0
    for _name, _fn in list(globals().items()):
        if _name.startswith("test_") and callable(_fn):
            try:
                _fn()
                print("ok", _name)
            except AssertionError as e:
                failures += 1
                print("FAIL", _name, e)
    print("all passed" if failures == 0 else f"{failures} failed")
    raise SystemExit(1 if failures else 0)
