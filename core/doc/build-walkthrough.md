# 构建与运行手记（Windows 11）

> 记录从零开始把 `IndustrialRuntime` 跑起来的全过程，包含踩坑与解决方法。
> 适合第一次在本机搭建环境的人参照。
> 平台：Windows 11 + VS 2022 BuildTools + vcpkg 清单模式。

---

## 0. 最终目标

构建并运行 `IndustrialRuntime.exe`，看到：

```
[info] IRSP server 监听 ws://0.0.0.0:9777
[info] admin 通道监听: \\.\pipe\industrial-runtime-admin
[info] 运行时已就绪，按 Ctrl+C 退出
[info] [事件] [Info] state: heartbeat #1
[info] [事件] [Info] state: heartbeat #2
...
```

---

## 1. 前置条件检查

先确认手头有什么。在 bash 里跑：

```bash
ls "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools" 2>&1
# 应该能看到 VC/、Common7/ 等目录 → VS BuildTools 已装

find "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake" \
     -maxdepth 3 -name "cmake.exe" 2>/dev/null
# VS 内置 cmake，版本约 3.31，默认不在 PATH

find "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools" \
     -maxdepth 7 -iname "ninja.exe" 2>/dev/null
# VS 内置 ninja

git --version           # 任意 git 都行
```

**你需要：**

- ✅ VS 2022 BuildTools（或 Community/Professional）含 C++ 工作负载
- ✅ Git（用来 clone vcpkg）
- ✅ 一个能访问 GitHub 的途径（直连或代理或镜像，下面会展开）

**你不需要：**

- 不需要单独装 CMake —— VS 自带
- 不需要单独装 Ninja —— VS 自带
- 不需要 CLion/VSCode 等任何 IDE

---

## 2. 安装 vcpkg

vcpkg 是 Microsoft 的 C++ 包管理器，本项目的 `vcpkg.json` 用清单模式声明三个依赖：`spdlog` / `nlohmann-json` / `libwebsockets`。

```bash
git clone https://github.com/microsoft/vcpkg.git C:/vcpkg
C:/vcpkg/bootstrap-vcpkg.bat
```

验证：

```bash
C:/vcpkg/vcpkg.exe --version
```

> **选 vcpkg 而不是手装依赖的原因**：项目 CMakeLists 直接写 `find_package(spdlog CONFIG REQUIRED)`，默认假设 vcpkg 已把 `.cmake` 配置文件放在它的 toolchain 路径下。手装的话要自己设置 `CMAKE_PREFIX_PATH`，且 libwebsockets 在 Windows 下编译不简单。vcpkg 是最少摩擦的路径。

---

## 3. 构建脚本

不要直接在 Git Bash 里跑 cmake —— MSVC 编译器需要先 `vcvars64.bat` 初始化环境变量（`INCLUDE`/`LIB`/`PATH` 等）。

在 `tools/` 下建一个 PowerShell 脚本：

```powershell
# tools/run-build.ps1
$ErrorActionPreference = "Stop"

$vsRoot = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$vcvars = "$vsRoot\VC\Auxiliary\Build\vcvars64.bat"
$cmake  = "$vsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$repo   = "C:\Users\Dao\Code\changthink\industrial-runtime"
$build  = Join-Path $repo "cmake-build-release"

$cmd = @"
call `"$vcvars`" || exit /b 1
set VCPKG_ROOT=C:\vcpkg
cd /d `"$repo`"
`"$cmake`" -S . -B `"$build`" -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release || exit /b 1
`"$cmake`" --build `"$build`" --config Release --parallel || exit /b 1
"@

& cmd /c $cmd
$rc = $LASTEXITCODE
if ($rc -ne 0) { Write-Error "构建失败 (exit $rc)"; exit $rc }
Write-Host "=== 构建完成 ==="
Write-Host "产物: $build\IndustrialRuntime.exe"
```

**关键参数解释：**

| 参数 | 作用 |
|---|---|
| `-S .` | 源码根 |
| `-B cmake-build-release` | 构建目录（out-of-source） |
| `-G Ninja` | 用 Ninja 生成器（并行编译，比 MSBuild 快） |
| `-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake` | 告诉 CMake 用 vcpkg 解析 `find_package()` |
| `-DCMAKE_BUILD_TYPE=Release` | 优化构建 |

运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/run-build.ps1
```

---

## 4. 国内网络坑（关键章节）

如果你不在 GFW 内或代理很顺，可以跳过本节。

### 4.1 现象

vcpkg 在 CMake configure 阶段会触发 `Running vcpkg install`，期间它需要下载**自己的工具链**（不是项目依赖）：

```
A suitable version of cmake was not found (required v4.3.3).
Downloading https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-windows-x86_64.zip ...
error: curl operation failed with error code 56
```

三个工具会依次卡：

| 工具 | 用途 | URL |
|---|---|---|
| `cmake-4.3.3-windows-x86_64.zip` | vcpkg 内部调用编译 port 时用的 cmake | github.com/Kitware/CMake/... |
| `7z2601-x64.7z.exe` | 解压用 | github.com/ip7z/7zip/... |
| `PowerShell-7.6.2-win-x64.zip` | 运行 port 脚本 | github.com/PowerShell/PowerShell/... |

### 4.2 为什么 `VCPKG_USE_SYSTEM_BINARIES=1` 不行

理论上设这个环境变量可以让 vcpkg 用系统已有的 cmake/ninja/7z，**但**：

- vcvars64.bat 不会把 VS 内置的 cmake 加到 PATH 上
- vcpkg 还是要 PowerShell Core 7.x（系统是 Windows PowerShell 5.1）
- 7zip 系统通常没装

最后还是必须把 vcpkg 要的版本放进它的 `downloads/` 目录。

### 4.3 代理自动检测的陷阱

vcpkg 启动时会扫 Windows IE 代理设置，看到 `127.0.0.1:7897`（Clash/Mihomo 默认混合端口）就自动写：

```
HTTP_PROXY=127.0.0.1:7897
HTTPS_PROXY=127.0.0.1:7897
```

**缺 `http://` 前缀**。curl 不认，下载直接失败。这是 vcpkg 已知问题（仓库 issue 里有讨论）。

### 4.4 解法：手动镜像预下

ghfast.top 是一个 GitHub 文件 CDN 代理。把 vcpkg 要的工具预先下到 `C:/vcpkg/downloads/`，vcpkg 看到文件存在（且 SHA512 匹配）就跳过下载。

```bash
# 1. cmake 4.3.3
curl -sSL -o "C:/vcpkg/downloads/cmake-4.3.3-windows-x86_64.zip" \
     "https://ghfast.top/https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-windows-x86_64.zip"

# 2. 7zip 26.01
curl -sSL -o "C:/vcpkg/downloads/7z2601-x64.7z.exe" \
     "https://ghfast.top/https://github.com/ip7z/7zip/releases/download/26.01/7z2601-x64.exe"

# 3. PowerShell 7.6.2
curl -sSL -o "C:/vcpkg/downloads/PowerShell-7.6.2-win-x64.zip" \
     "https://ghfast.top/https://github.com/PowerShell/PowerShell/releases/download/v7.6.2/PowerShell-7.6.2-win-x64.zip"
```

### 4.5 SHA512 校验

每个文件的期望 hash 记录在 vcpkg 的工具清单里：

```bash
grep -A 6 'cmake-4.3.3-windows-x86_64' C:/vcpkg/scripts/vcpkg-tools.json
grep -A 6 '7z2601-x64' C:/vcpkg/scripts/vcpkg-tools.json
grep -A 6 'PowerShell-7.6.2-win-x64.zip' C:/vcpkg/scripts/vcpkg-tools.json
```

下载后本地校验：

```bash
sha512sum C:/vcpkg/downloads/cmake-4.3.3-windows-x86_64.zip
sha512sum C:/vcpkg/downloads/7z2601-x64.7z.exe
sha512sum C:/vcpkg/downloads/PowerShell-7.6.2-win-x64.zip
```

**两个 hash 必须完全一致**，否则 vcpkg 会重新下载。

### 4.6 项目依赖本身的下载

预下完上面三个工具后，vcpkg 会自己处理 `spdlog`/`nlohmann-json`/`libwebsockets` 的下载与编译。这几个是从 vcpkg 自己的 registry（也在 `C:/vcpkg` 仓库里）拉源码，源码 URL 走 GitHub release 或 sourceforge —— 如果也卡，同样套路：从 `ghfast.top` 预下到 `downloads/`，SHA512 在各 port 的 `vcpkg.json` 里。

实际本次运行，三个项目依赖**都命中了 vcpkg 的二进制 cache**（微软 CI 编译好的预编译包），所以瞬间完成，无需本地编译。

---

## 5. 构建过程

第二次跑 `run-build.ps1`：

```
[1/65] Building CXX object core\CMakeFiles\core_common.dir\...
[2/65] ...
...
[65/65] Linking CXX executable IndustrialRuntime.exe
```

产物部署时，CMake 自动把依赖 DLL 从 `vcpkg_installed/x64-windows/bin/` 复制到 .exe 同级目录：

- `websockets.dll` / `libssl-3-x64.dll` / `libcrypto-3-x64.dll`
- `zlib1.dll` / `uv.dll` / `spdlog.dll` / `fmt.dll`

**构建耗时**：冷启 ~10 分钟（含 vcpkg 依赖），增量 < 30 秒。

---

## 6. 运行时验证

直接跑：

```bash
./cmake-build-release/IndustrialRuntime.exe
```

正常输出：

```
[info] RuntimeEngine 初始化完成
[info] RuntimeEngine 已启动
[info] 插件目录 .../cmake-build-release/plugins 不存在，跳过自动发现
[info] 插件目录 .../plugins，配置目录 .../config，已加载 0 个插件
[info] IRSP server 监听 ws://0.0.0.0:9777
[info] admin 通道监听: \\.\pipe\industrial-runtime-admin
[info] 运行时已就绪，按 Ctrl+C 退出
[info] [事件] [Info] state: heartbeat #1
```

另开一个 shell 验证端口：

```bash
netstat -ano | grep 9777
# TCP    0.0.0.0:9777           0.0.0.0:0              LISTENING  <PID>
# TCP    [::]:9777              [::]:0                 LISTENING  <PID>
```

`Ctrl+C` 优雅退出。

---

## 7. 启动时发生了什么

读 `core/main.cpp` 按顺序梳理：

| 步骤 | 代码 | 作用 |
|---|---|---|
| 1 | `SetConsoleOutputCP(CP_UTF8)` | 控制台按 UTF-8 渲染日志 |
| 2 | `signal(SIGINT/SIGTERM)` | 注册 Ctrl+C 退出钩子 |
| 3 | `Config::loadFile(argv[1])` | 可选，加载主配置 |
| 4 | `RuntimeEngine::init/start` | 启动 TagEngine / EventBus / Scheduler |
| 5 | `events().subscribe(...)` | 订阅事件，打印到日志 |
| 6 | `PluginHost` + `setWriteHandler` | 装配写回出口（IRSP SET → 插件） |
| 7 | `PluginManager::loadDirectory("plugins", "config")` | 扫同级 `plugins/` 自动加载设备插件 |
| 8 | `IrspServer(port=9777)` | 启 WebSocket 数据面 |
| 9 | `AdminServer` | 启 Windows 命名管道控制面 |
| 10 | `scheduler().addPeriodicTask("heartbeat", 1s, ...)` | 每秒推一个心跳 Tag + Event，演示数据流 |
| 11 | `while (g_running) sleep(200ms)` | 主线程阻塞等信号 |

退出时逆序：admin.stop → irsp.stop → pluginManager.stopAll → runtime.stop → Logger.flush。

---

## 8. 常见问题

### Q1：`cmake: command not found`
没把 vcvars64.bat 跑起来，或 VS BuildTools 没装 C++ 工作负载。

### Q2：`vcpkg install failed: Download timed out`
走 §4 的预下路线。先看 `cmake-build-release/vcpkg-manifest-install.log` 具体卡在哪个 URL。

### Q3：`Generator: execution of make failed`
Ninja 没找到。检查 vcvars64 是否成功初始化（看输出里有没有 `[vcvarsall.bat] Environment initialized for: 'x64'`）。

### Q4：日志中文乱码
源码是 UTF-8，但 Windows 控制台默认 GBK。`main.cpp` 已调 `SetConsoleOutputCP(CP_UTF8)`。如果终端还是乱，PowerShell 里先 `chcp 65001`，或在 Windows Terminal 设置里把 profile 默认改成 UTF-8。

### Q5：插件加载 0 个
正常。本仓库不含设备插件，插件是独立工程（参考 `sdk/plugin-sdk/README.md`）。把编好的 `.dll` 丢进 `cmake-build-release/plugins/` 即可自动发现。

### Q6：MSVC 报 "C1083: 无法打开包含文件 'nlohmann/json.hpp'"
vcpkg 没正确安装。检查 `cmake-build-release/vcpkg_installed/x64-windows/include/nlohmann/` 是否存在。

---

## 9. 下一步

- **跑单元测试**：`ctest --test-dir cmake-build-release --output-on-failure`（17 个测试）
- **接浏览器监控页**：`cd sdk/irsp-client/JS && node examples/serve.mjs`，打开 `http://localhost:8080/examples/index.html`，实时看 `system/heartbeat` 推送
- **写个示例插件**：参考 `sdk/plugin-sdk/` 的头文件和示例，编出 `.dll` 丢进 `plugins/`
- **加 admin CLI**：仓库里有 `admin/irpcli`，支持 `plugin list/reload/unload` 热管理

---

## 10. 关键文件索引

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 顶层 CMake，声明 vcpkg 依赖与可执行入口 |
| `vcpkg.json` | vcpkg 清单：spdlog/nlohmann-json/libwebsockets |
| `core/CMakeLists.txt` | 各子模块静态库依赖关系 |
| `core/main.cpp` | 运行时入口，10 步启动流程 |
| `tools/run-build.ps1` | 封装好的构建脚本 |
| `README.md` | 项目速览与 roadmap |
| `core/doc/performance-analysis.md` | 性能分析报告（MemoryStore/TagEngine/EventBus） |

---

*本文档由实际搭建过程整理，最后更新：2026-06-21。*
