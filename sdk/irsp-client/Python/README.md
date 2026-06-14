# irsp-client (Python)

IRSP（Industrial Runtime Serialization Protocol）的 Python 客户端 SDK —— **纯 Python，asyncio**，
依赖仅 `websockets`。打包为 `py3-none-any` wheel，**`pip install` 即用**。

位置：`sdk/irsp-client/Python/`。协议规格见仓库 `irsp/`。

## 安装

直接装预构建 wheel（推荐）：

```bash
pip install dist/irsp_client-1.0.0-py3-none-any.whl
```

或从源码目录安装：

```bash
pip install ./sdk/irsp-client/Python
```

## 用法

```python
import asyncio
from irsp_client import IrspClient

async def main():
    client = IrspClient("ws://127.0.0.1:9777")
    client.on_tag(lambda t: print(t.name, "=", t.value))
    client.on_event(lambda e: print(e.severity, e.message))

    await client.connect()                       # 自动 HELLO 握手
    print(client.server)                         # {'server':..., 'encoding':'irsp1', ...}

    tag = await client.get("plant/line1/temp")   # TagValue | None
    await client.subscribe("plant/#")            # 子树订阅，变化经 on_tag 回调
    await client.subevent("warning")             # 订阅 warning 级以上事件
    await asyncio.sleep(10)
    await client.bye()

asyncio.run(main())
```

也支持异步上下文：`async with IrspClient(url) as client: ...`。
回调可为同步函数或协程函数（协程会被自动调度）。

## API

| 方法（均为 async） | 返回 |
|------|------|
| `connect()` / `bye()` / `close()` | — |
| `get(name)` | `TagValue \| None` |
| `mget(names)` | `list[TagValue \| None]` |
| `exists(name)` | `bool` |
| `scan(cursor='0', pattern='#', count=0)` | `{'next_cursor', 'names'}` |
| `watch(*names)` / `subscribe(*patterns)` | `int` |
| `unwatch(...)` / `unsubscribe(...)` | `int` |
| `subevent(min_severity=None, category=None)` / `unsubevent()` | `int` |
| `ping(payload=None)` | `str \| None` |

推送回调：`on_tag(cb)`、`on_event(cb)`。

```python
@dataclass
class TagValue: name: str; type: str; ts: int; value: Any; quality: str | None
@dataclass
class IrspEvent: source: str; category: str; severity: str; ts: int; message: str
```

`value` 已按 `type`（`bool/i8../u64/f32/f64/str/null`）解码；`ts` 为纳秒（Python int 无精度问题）。

## 构建 wheel

```bash
cd sdk/irsp-client/Python
pip wheel . --no-deps -w dist        # 产出 dist/irsp_client-1.0.0-py3-none-any.whl
# 或： python -m build --wheel
```

## 测试

```bash
PYTHONPATH=src python tests/test_irsp1.py     # 纯 codec，无需服务端 / 无需 pytest
# 或： pip install pytest && pytest
```

## 设计要点

- 请求/回复在连接上 **FIFO 顺序**对应（RESP 风格无 id）；推送帧带 `push`(`tag`/`event`) 区分。
- 编码 irsp1（`src/irsp_client/irsp1.py`），数值小端二进制。
- 仅 V1：`SET`/Stream 未提供（协议预留）。
