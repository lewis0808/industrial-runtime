"""irsp1 编解码单测（无需服务端）。

pytest：  pytest
直跑：    PYTHONPATH=src python tests/test_irsp1.py
"""
import struct

from irsp_client.irsp1 import IrspError, as_str, decode, decode_value, encode_request


def test_encode_request():
    assert encode_request(["GET", "a/b"]) == b"*2\r\n$3\r\nGET\r\n$3\r\na/b\r\n"


def test_decode_basic():
    assert decode(b"+OK\r\n") == "OK"
    assert decode(b":42\r\n") == 42
    assert decode(b"$-1\r\n") is None
    err = decode(b"-WRONG_ARITY too many\r\n")
    assert isinstance(err, IrspError)
    assert err.code == "WRONG_ARITY"
    assert err.message == "too many"


def test_decode_array_map():
    arr = decode(b"*2\r\n$1\r\na\r\n$1\r\nb\r\n")
    assert [as_str(x) for x in arr] == ["a", "b"]
    m = decode(b"%2\r\n$4\r\nname\r\n$3\r\na/b\r\n$4\r\ntype\r\n$3\r\nf64\r\n")
    assert as_str(m["name"]) == "a/b"
    assert as_str(m["type"]) == "f64"


def test_decode_value():
    assert decode_value("f64", struct.pack("<d", 42.5)) == 42.5
    assert decode_value("i32", struct.pack("<i", 5)) == 5
    assert decode_value("bool", b"\x01") is True
    assert decode_value("bool", b"\x00") is False
    assert decode_value("str", b"hi") == "hi"
    assert decode_value("i64", struct.pack("<q", 1749800000000000000)) == 1749800000000000000


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
