import math
import struct
import threading
import time
from ctypes import c_uint8

import snap7
from snap7.server import Server
from snap7.type import SrvArea

server = Server()

# 创建DB1，1024字节（register_area 需要 ctypes 数组）
db1 = (c_uint8 * 1024)()

# ---- 写入 S7 全部基本数据类型（big-endian）----
# BCD 编码助手：DATE_AND_TIME / S5TIME 用得到
def bcd(v: int) -> int:
    return ((v // 10) << 4) | (v % 10)


# --- 位 / 字节 ---
db1[0] = 0b00000001                          # DB1.DBX0.0  BOOL   True
db1[1] = 0xAB                                # DB1.DBB1    BYTE   0xAB
struct.pack_into(">b", db1, 2, -42)          # DB1.DBB2    SINT   -42  (有符号8)
struct.pack_into(">B", db1, 3, 200)          # DB1.DBB3    USINT  200  (无符号8)

# --- 整数 ---
struct.pack_into(">H", db1, 4, 0x1234)       # DB1.DBW4    WORD   (无符号16)
struct.pack_into(">h", db1, 6, -1234)        # DB1.DBW6    INT    (有符号16)
struct.pack_into(">H", db1, 8, 60000)        # DB1.DBW8    UINT   (无符号16)
struct.pack_into(">I", db1, 10, 0x12345678)  # DB1.DBD10   DWORD  (无符号32)
struct.pack_into(">i", db1, 14, -123456)     # DB1.DBD14   DINT   (有符号32)
struct.pack_into(">I", db1, 18, 4000000000)  # DB1.DBD18   UDINT  (无符号32)
struct.pack_into(">Q", db1, 22, 0x1122334455667788)  # DB1.DBX22  LWORD (无符号64)
struct.pack_into(">q", db1, 30, -9000000000)         # DB1.DBX30  LINT  (有符号64)
struct.pack_into(">Q", db1, 38, 18000000000)         # DB1.DBX38  ULINT (无符号64)

# --- 浮点 ---
struct.pack_into(">f", db1, 46, 3.14)        # DB1.DBD46   REAL   (32位浮点)
struct.pack_into(">d", db1, 50, 2.718281828) # DB1.DBX50   LREAL  (64位双精度)

# --- 字符 ---
db1[58] = ord("A")                           # DB1.DBB58   CHAR   'A'  (1字节ASCII)
struct.pack_into(">H", db1, 60, ord("好"))    # DB1.DBW60   WCHAR  '好' (2字节UTF-16BE)

# --- 时间 ---
struct.pack_into(">i", db1, 62, 1234567)     # DB1.DBD62   TIME   毫秒 (有符号32)
struct.pack_into(">q", db1, 66, 12345678900) # DB1.DBX66   LTIME  纳秒 (有符号64)
struct.pack_into(">H", db1, 74, 0x0A0F)      # DB1.DBW74   S5TIME BCD

# --- 日期 ---
struct.pack_into(">H", db1, 76, 13314)       # DB1.DBW76   DATE   自1990-01-01的天数 -> 2026-06-14
struct.pack_into(">I", db1, 78, 45296000)    # DB1.DBD78   TOD    自0点的毫秒 -> 12:34:56

# DATE_AND_TIME (DT): 8字节BCD = 年(2位) 月 日 时 分 秒 毫秒高2位+毫秒低1位/星期
dt = [bcd(26), bcd(6), bcd(14), bcd(12), bcd(34), bcd(56), bcd(0), 0x00]
for i, b in enumerate(dt):                   # DB1.DBX82   DATE_AND_TIME -> 2026-06-14 12:34:56
    db1[82 + i] = b

# DTL: 12字节 = 年(UINT) 月 日 星期 时 分 秒(各USINT) 纳秒(UDINT)
struct.pack_into(">HBBBBBB I", db1, 90, 2026, 6, 14, 7, 12, 34, 56, 123456789)  # DB1.DBX90 DTL

# --- 字符串 ---
s = b"Hello"                                 # DB1.DBB104  STRING  [max][len][chars...]
db1[104] = 254
db1[105] = len(s)
struct.pack_into(f">{len(s)}s", db1, 106, s)

ws = "你好".encode("utf-16-be")               # DB1.DBX362  WSTRING [max][len][wchars...]
struct.pack_into(">H", db1, 362, 254)        # 最大长度(字符数)
struct.pack_into(">H", db1, 364, len(ws) // 2)
struct.pack_into(f">{len(ws)}s", db1, 366, ws)

# 注册DB1
server.register_area(
    SrvArea.DB,
    1,
    db1
)

server.start(tcp_port=102)

print("S7 Server started")


# ---- 后台线程：定时刷新动态值，便于测试订阅刷新 ----
def updater(stop: threading.Event) -> None:
    n = 0
    while not stop.is_set():
        db1[0] = n & 1                              # DB1.DBX0.0 BOOL  心跳，0/1翻转
        struct.pack_into(">H", db1, 8, n % 65536)   # DB1.DBW8   UINT  计数器自增
        struct.pack_into(">f", db1, 46,             # DB1.DBD46  REAL  正弦 0~100
                         50.0 + 50.0 * math.sin(n * 0.1))
        n += 1
        stop.wait(0.5)                              # 500ms 刷新一次


stop = threading.Event()
worker = threading.Thread(target=updater, args=(stop,), daemon=True)
worker.start()

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\nstopping...")
finally:
    stop.set()
    worker.join()
    server.stop()
    server.destroy()