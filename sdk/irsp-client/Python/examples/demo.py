"""
IRSP 客户端演示。先启动 IndustrialRuntime（9777），再执行： python examples/demo.py
（需先安装：pip install dist/irsp_client-*.whl）
"""
import asyncio
import os

from irsp_client import IrspClient, IrspEvent, TagValue


async def main() -> None:
    url = os.environ.get("IRSP_URL", "ws://127.0.0.1:9777")
    client = IrspClient(url)

    await client.connect()
    # print("已连接，服务端能力:", client.server)
    print(await client.get("system/heartbeat"))
    # print("exists =>", await client.exists("system/heartbeat"))
    # print("scan   =>", await client.scan("0", "#"))
    # print("subscribe system/# =>", await client.subscribe("system/#"))
    # print("subevent info =>", await client.subevent("info"))

    # await asyncio.sleep(5)
    await client.bye()
    print("已断开。")


if __name__ == "__main__":
    asyncio.run(main())
