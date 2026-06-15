# admin —— 本机控制面通道

运行时的**控制面**：列举 / 卸载 / 重载插件等**有副作用**的管理操作走这里，**与 IRSP 数据面
（WebSocket）解耦**，避免控制命令污染数据总线（见根 README §7 设计原则）。**仅本机可达**。

## 用法：`irpcli`（推荐）

随运行时一起编出的控制面 CLI（industrial runtime process cli）。运行时在跑时直接用：

```sh
irpcli plugin list             # 列出已加载插件
irpcli plugin reload <id>      # 重载插件（从磁盘拾取新 DLL）
irpcli plugin unload <id>      # 卸载插件
irpcli plugin scan             # 重新扫描 plugins/，加载尚未加载的插件
irpcli --endpoint <name|path> plugin list   # 覆盖默认端点
```

`<id>` 是插件 `getPluginInfo()` 返回的 id（不是 DLL 文件名），先 `irpcli plugin list` 看准。
退出码：`0` 成功 / `1` 服务端 `ERR` / `2` 用法错 / `3` 连接失败（运行时未运行？）。

`list` 渲染为对齐表格：

```
ID     NAME                              VERSION  STARTED
py-s7  Python S7 Driver (PyHost bridge)  0.1.0    yes

1 plugin(s)
```

（底层线协议返回 `OK <n>` + 制表符分隔行；`irpcli` 负责渲染，见下「线协议」。）

**典型热更新**：编出新版插件 DLL → 覆盖到运行时同级 `plugins/<x>.dll` → `irpcli plugin reload <id>`
（旧实例 `stop→destroy→卸库`，按原 path/config 重载并重新 `start`，从磁盘拾取新 DLL）。

## 传输与线协议（直接脚本化时参考）

`irpcli` 只是薄客户端；如需自行脚本化，可直接连端点发命令：

| 平台 | 端点（默认） |
|------|------|
| Windows | 命名管道 `\\.\pipe\industrial-runtime-admin` |
| POSIX | AF_UNIX socket `/tmp/industrial-runtime-admin.sock` |

每条连接：客户端发**一行命令**（`\n` 结尾），服务端回复文本后**关闭连接**。无握手、无会话状态。
命令名/子命令大小写不敏感，按空白分词（尾随 `\r` 容忍）。

| 命令 | 回复 |
|------|------|
| `PLUGIN LIST` | `OK <n>\n` 后跟 n 行 `<id>\t<name>\t<version>\t<0\|1>` |
| `PLUGIN UNLOAD <id>` | `OK\n` / `ERR <reason>\n` |
| `PLUGIN RELOAD <id>` | `OK\n` / `ERR <reason>\n` |
| `PLUGIN SCAN` | `OK <n>\n`（n = 本次新加载数量） |
| 其它 / 参数错 | `ERR <reason>\n` |

- `UNLOAD`：撤销该插件写回路由并**排空在途写回调用**后 `stop → destroy → 卸库`（无 use-after-free）。
- `RELOAD`：以原 path/config 卸载后重载；原先已 `start` 则重新 `start`；失败时旧实例已卸载、不残留。
  注意 `RELOAD` 只作用于**当前已加载**的插件；已 `UNLOAD` 的需用 `SCAN` 装回。
- `SCAN`：重新扫描 `plugins/`（+`config/`），加载并 `start` **尚未加载**的插件（按路径去重，
  已加载的跳过）。用于把新放入的插件、或 `UNLOAD` 后想装回的插件接入，无需重启运行时。
- 卸载/reload 期间该连接会**短暂阻塞**（等待在途写回排空），管理操作低频，属预期。

直接连接示例（不经 irpcli）：

```sh
# Linux：socat 连 AF_UNIX
printf 'PLUGIN LIST\n' | socat - UNIX-CONNECT:/tmp/industrial-runtime-admin.sock
```

```powershell
# Windows：连命名管道
$p = New-Object IO.Pipes.NamedPipeClientStream('.', 'industrial-runtime-admin', 'InOut')
$p.Connect(1000)
$w = New-Object IO.StreamWriter($p); $w.AutoFlush = $true
$w.WriteLine('PLUGIN LIST'); (New-Object IO.StreamReader($p)).ReadToEnd()
```

## 模块构成与边界

- `admin_command.*`：纯函数 `handleAdminCommand(PluginManager&, line)`——命令解析/执行，无 I/O、
  可单测（`tests/test_admin_command.cpp`）。
- `admin_server.*`：服务端传输（命名管道 / AF_UNIX），跑在可中断 `jthread`，内嵌于运行时进程。
- `admin_endpoint.hpp`：服务端与 `irpcli` 共享的默认端点（避免漂移）。
- `cli/irpcli.cpp`：独立 CLI 客户端，**无 core 依赖**，薄转发命令行到 admin 端点。
- 线程安全由 `core::PluginManager` 保证（内部 `mutex` + 写回引用计数排空）。
- **不依赖 irsp / 网络库**。鉴权预留：当前仅靠「本机端点」隔离；后续可加 token / 管道 ACL。
