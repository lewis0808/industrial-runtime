# PyS7Plugin — 用 Python 写运行时插件（C-ABI v3 示例）

演示**解释型语言如何写 Industrial Runtime 插件**：snap7 读西门子 S7 的 DB 区，经
`push_tag` 把值灌进运行时。设备逻辑 **100% 在 `s7_driver.py`**，原生桥只做解释器宿主。

## 为什么需要一个原生桥

宿主只能加载原生 DLL（`LoadLibraryEx`/`dlopen`），而 Python 是解释型的，产不出这种 DLL。
所以 Python 插件 = **一个通用原生桥（嵌 CPython）+ 纯 Python 设备逻辑**。

这正是 **C-ABI v3 的价值**：

- 桥导出 v3 的 `getPluginInfo`/`createPlugin`，只需填 `IrPluginInstance` 的 **4 个 C 函数指针**；
- Python 侧用 `ctypes` 直接贴着 `plugin_abi.h` 的 **纯 C 结构** 调 `push_tag`。

v2（C++ vtable）时代，这个桥得用 C 模拟 C++ 的 vtable 布局 —— 是 UB，根本写不干净。
**桥不是绕过 v3 的妥协，是 v3 的直接产物。**

## 组成

```
PyS7Plugin/
├── s7_driver.py          # 纯 Python：ctypes 镜像 C 结构 + snap7 读 DB1 + 定时 push_tag
├── bridge/
│   ├── pyhost_bridge.cpp # ~120 行：嵌 CPython，转发 init/start/stop/destroy，交出 host 指针
│   └── CMakeLists.txt     # 编出 py_s7_plugin.dll（link Python3::Python）
└── py_s7_plugin.json      # S7 连接参数 + tag 映射（文件名 = dll basename）
```

数据流：`S7ServerMock(:102)` → `py_s7_plugin.dll` → `s7_driver.py`(snap7 读 DB1) → `push_tag` →
运行时 → IRSP `SUBSCRIBE s7/#` 可见。

## 构建（在 CLion / 你的工具链里，本仓库不替你跑 cmake）

**直接复用 `tools/S7ServerMock/.venv`，不走 vcpkg。** CMakeLists 会：

1. 从 `.venv/pyvenv.cfg` 的 `home` 读出创建该 venv 的**基础 Python 安装**（如 `D:\Anaconda3`），
   用它的 `include/` + `libs/python39.lib` 来 `find_package(Python3)` 并链接（venv 本身不含开发文件）。
2. 把基础 Python 目录与 venv 的 `site-packages` 烤进二进制：前者供嵌入解释器定位标准库，
   后者供 `import snap7`（snap7 只装在 venv 里）。

```bash
cd tools/PyS7Plugin/bridge
cmake -B build -S .          # 无需任何 -DPython3_*，自动从 .venv 推导
cmake --build build --config Release
```

> 前提：`tools/S7ServerMock/.venv` 已存在且装了 `python-snap7`（S7ServerMock 用的就是它）。
> 若把 venv 迁到别处或换了基础 Python，重跑 cmake 即自动跟随 `pyvenv.cfg`。

## 部署 & 测试

1. 把构建产物与脚本一起丢进运行时 exe 同级的 `plugins/`，配置进 `config/`：
   ```
   <runtime-exe>/
   ├── plugins/
   │   ├── py_s7_plugin.dll     # 桥（自动发现加载）
   │   ├── s7_driver.py         # 与 dll 同目录（桥把此目录加进 sys.path）
   │   └── python39.dll         # 见下：除非 D:\Anaconda3 在 PATH，否则拷一份到这里
   └── config/
       └── py_s7_plugin.json    # 文件名 = dll basename，运行时透传路径给插件
   ```
   - `python39.dll` 是桥的链接依赖，加载 `py_s7_plugin.dll` 时 OS 须能找到它。运行时用
     `LOAD_WITH_ALTERED_SEARCH_PATH` 加载插件，会先在**插件同目录**找依赖——所以把
     `D:\Anaconda3\python39.dll` 拷到 `plugins/` 旁即可（或把 `D:\Anaconda3` 加进 PATH）。
     标准库由桥设的 `home` 指回 `D:\Anaconda3` 定位，不受这份 dll 落点影响。
2. 先起被采集对象：`cd tools/S7ServerMock && .venv/Scripts/python main.py`（S7 服务在 :102）。
3. 起运行时，日志应出现 `插件已加载: Python S7 Driver (PyHost bridge)`。
4. 用 IRSP 客户端订阅看值在变：
   ```python
   async with IrspClient("ws://127.0.0.1:9777") as c:
       c.on_tag(print)
       await c.subscribe("s7/#")
   ```
   能看到 `s7/db1/heartbeat`(0/1 翻转)、`s7/db1/counter`(自增)、`s7/db1/sine`(0~100 正弦)。

## 已知坑（嵌入式 Python 的代价）

- **解释器与宿主同进程**：Python 段错误/崩溃会拖垮运行时；GIL 下 CPU 密集型 Python 插件会串行
  （本例是 I/O 密集的 S7 轮询，无碍）。
- **依赖耦合**：snap7 由 `.venv` 自动接好（CMake 把其 site-packages 烤进二进制）；但 `python39.dll`
  须运行期可达且为 x64，基础 Python（`D:\Anaconda3`）路径被烤进 `home`，**挪动它需重新 build**。
- **生产演进**：若要崩溃隔离 / 解耦 Python 版本，应走**进程外**——Python sidecar 经 IPC 把数据交给
  一个"收 IPC 的原生桥插件"。前提是先给运行时补一条入站协议（当前 IRSP 只读、无 push/SET-入库）。
  本示例选进程内嵌入，因为最简单、延迟最低、最能直观展示 v3 的 C-ABI 边界。
