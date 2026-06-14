# admin —— 本机控制面通道

运行时的**控制面**：列举 / 卸载 / 重载插件等**有副作用**的管理操作走这里，**与 IRSP 数据面
（WebSocket）解耦**，避免控制命令污染数据总线（见根 README §7 设计原则）。

## 传输

**仅本机可达**，不占网络端口：

| 平台 | 端点（默认） |
|------|------|
| Windows | 命名管道 `\\.\pipe\industrial-runtime-admin` |
| POSIX | AF_UNIX socket `/tmp/industrial-runtime-admin.sock` |

每条连接：客户端发**一行命令**（`\n` 结尾），服务端回复文本后**关闭连接**。无握手、无会话状态。
命令名/子命令大小写不敏感，按空白分词（尾随 `\r` 容忍）。

## 命令

| 命令 | 回复 |
|------|------|
| `PLUGIN LIST` | `OK <n>\n` 后跟 n 行 `<id>\t<name>\t<version>\t<0\|1>`（`started`） |
| `PLUGIN UNLOAD <id>` | `OK\n` / `ERR <reason>\n` |
| `PLUGIN RELOAD <id>` | `OK\n` / `ERR <reason>\n` |
| 其它 / 参数错 | `ERR <reason>\n` |

- `id` 为插件 `getPluginInfo` 返回的 `id`。
- `UNLOAD`：撤销该插件写回路由并**排空在途写回调用**后 `stop → destroy → 卸库`（无 use-after-free）。
- `RELOAD`：以原 path/config 卸载后重新加载，原先已 `start` 则重新 `start`；失败时旧实例已卸载、不残留。
- 卸载/reload 期间该连接会**短暂阻塞**（等待在途写回排空），管理操作低频，属预期。

## 示例

```sh
# Linux：用 socat / nc 连 AF_UNIX
printf 'PLUGIN LIST\n'        | socat - UNIX-CONNECT:/tmp/industrial-runtime-admin.sock
# → OK 1
#   s7    S7 Plugin    1.0.0    1

printf 'PLUGIN RELOAD s7\n'   | socat - UNIX-CONNECT:/tmp/industrial-runtime-admin.sock
# → OK

printf 'PLUGIN UNLOAD s7\n'   | socat - UNIX-CONNECT:/tmp/industrial-runtime-admin.sock
# → OK
```

```powershell
# Windows：连命名管道
$p = New-Object IO.Pipes.NamedPipeClientStream('.', 'industrial-runtime-admin', 'InOut')
$p.Connect(1000)
$w = New-Object IO.StreamWriter($p); $w.AutoFlush = $true
$r = New-Object IO.StreamReader($p)
$w.WriteLine('PLUGIN LIST')
$r.ReadToEnd()
```

## 设计与边界

- 命令处理逻辑（`admin_command.*`）是**纯函数** `handleAdminCommand(PluginManager&, line)`，
  无 I/O、可单测（`tests/test_admin_command.cpp`）；传输（`admin_server.*`）只负责收发。
- 线程安全由 `core::PluginManager` 保证（内部 `mutex` 串行化管理操作 + 写回引用计数排空）。
  admin 服务跑在独立可中断 `jthread`。
- **不依赖 irsp / 网络库**，仅依赖 `core_plugin_manager`。
- 鉴权预留：当前仅靠「本机端点」隔离；后续可加 token / 管道 ACL（与 IRSP `AUTH` 各自独立）。
