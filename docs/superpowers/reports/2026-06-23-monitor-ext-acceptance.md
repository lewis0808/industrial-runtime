# IRSP JS Console 验收测试报告

**分支**: `feature/monitor-ext`
**验收日期**: 2026-06-23
**被测构建**: `IndustrialRuntime.exe` (v0.2.0, Release)
**被测 SDK**: `@industrial-runtime/irsp-client@1.0.0` (JS, 28 unit tests)
**测试夹具**: `tools/demo-writeback` (echo plugin, ABI v1)

---

## 1. 测试范围

| 层级 | 范围 |
|---|---|
| L0 单元 | JS Client SDK 编解码、Pipeline、SlidingStats（`node --test`） |
| L1 构建 | 全量 `cmake --build`（含新 `IR_BUILD_DEMO_PLUGINS` 选项） |
| L2 集成 | Runtime 启动 + 插件自动发现 + IRSP WebSocket 监听 |
| L3 端到端 | 浏览器 4-Tab Console 功能全链路（chrome-devtools MCP） |

---

## 2. L0 — JS 单元测试（28/28 PASS）

命令：`cd sdk/irsp-client/JS && npm test`

| 模块 | 用例数 | 覆盖点 |
|---|---|---|
| `encodeValue` / `inferType` | 16 | bool/i32/i64/f64/str/i16/binary 往返；bigint→i64、number→i32|i64、float→f64、Uint8Array→binary 类型推断 |
| `client.set()` | 2 | 显式类型编码、类型自动推断 |
| `perf.js` | 6 | percentile 插值；SlidingStats 记录/汇总/错误计数；Pipeline 并发上限 + 提前停止 + 错误传播 |
| `irsp1.js` decode | 3 | 基本类型、数组/map、按类型小端解码 |
| `encodeRequest` | 1 | bulk 数组结构 |

**结果**: `tests 28 / pass 28 / fail 0`，`duration_ms ≈ 445`

---

## 3. L1 — 构建验证（PASS）

命令：`powershell -File tools/run-build.ps1`

- `IR_BUILD_DEMO_PLUGINS=ON`（默认）
- 全量重构建成功，无警告升级
- 产物：`cmake-build-release/IndustrialRuntime.exe`
- POST_BUILD 钩子执行：`plugins/demo_writeback.dll` 正确落入 runtime 同级 `plugins/`

**迁移后路径核验**（`core/tests/fixtures/` → `tools/`）：
- `core/tests/CMakeLists.txt` 已移除 `add_subdirectory(fixtures/demo-writeback)`
- `CMakeLists.txt` 顶层在 `add_executable(IndustrialRuntime)` **之后**注册 `add_subdirectory(tools/demo-writeback)`，保证 POST_BUILD 生成器表达式能解析 `IndustrialRuntime` 目标

---

## 4. L2 — 运行时集成（PASS）

启动命令：`./IndustrialRuntime.exe`（3 秒采样窗口）

日志关键行：
```
[runtime] 插件已加载: Demo Writeback Plugin (test fixture) (1.0.0)
[runtime] 插件目录 ...\plugins，配置目录 ...\config，已加载 1 个插件
[runtime] [事件] [Info] state: demo-writeback started
```

- 插件自动发现机制正常（扫可执行文件同级 `plugins/`）
- `demo/` 前缀 ownership 注册成功
- IRSP WebSocket 监听 `0.0.0.0:9777`（IPv4 + IPv6）
- admin 通道 `\\.\pipe\industrial-runtime-admin` 就绪
- Scheduler 心跳 Tag `system/heartbeat` 每秒一次

---

## 5. L3 — 浏览器端到端（4 Tab 全部 PASS）

测试载体：`sdk/irsp-client/JS/examples/index.html`（静态服务 + chrome-devtools MCP）
主题：`data-theme="irsp"`（工业仪表盘深色）

### Tab1 实时监控
- **SCAN**：`SCAN demo/` 返回 topic 列表，预填到 SUBSCRIBE 输入框
- **SUBSCRIBE**：订阅 `demo/#` + `system/#`，TAGS 表实时刷新
- **type 徽章**：`i64` 青色、`str` 紫色、`bool` 橙色，按 DataType 分类配色正确
- **变化高亮**：`system/heartbeat` 值变更触发 `▲ changed-up` 绿色 flash（1 秒衰减）
- **SUBEVENT**：事件流带 severity 色条（INFO 灰 / WARN 黄 / CRIT 红辉光）

### Tab2 命令测试台
- 所有 IRSP 命令（PING/SCAN/SUBSCRIBE/GET/SET/WATCH...）按钮化
- GET `system/heartbeat` 返回当前值 + timestamp
- 请求/响应日志含 per-command 耗时（ms）
- error 级别日志（如 SET 写到无 owner 前缀）以红色 `.log-entry.error` 样式呈现
- 内联 SET 文本框 placeholder 明确告知"inline 模式仅支持 GET，SET 需二进制 LE 值"（设计约束，非 bug）

### Tab3 性能压测
- **场景一 pipeline 吞吐**：PING pipeline，测得 `2661 ops/s, p50 1ms, p95 5ms, p99 9ms`
- **场景三 多连接并发**：4 连接 × 各 5000 PING，聚合吞吐与单连接对比
- ECharts 时序图深色主题渲染正确（cyan 主线 + orange 副线 + grid #2a323e）
- LocalStorage 历史对比：第二次运行显示 `▲ X.X% vs 上次` 虚线参考线
- "清除历史"按钮重置 `irsp-bench-history`

### Tab4 写回测试（最复杂链路）
- **探针**：SET `demo/__probe__` 自动检测 demo-writeback 插件 → 状态变绿 `online`
- **单次写回**：SET `demo/foo = 4247`（i64），四宫格 metric 显示
  - RESPONSE 14ms（SET 同步受理）
  - PUSH 13ms（TagEngine 变更回调推送）
  - ROUND TRIP 13ms（端到端）
  - REPLY `OK`
- **批量写回**：SET `demo/batch/0..99`，cyan 进度条推进到 100/100
- **L1 断言**（同步受理）：`reply === 'OK'` → **PASS**
- **L2 断言**（echo 回环）：SUBSCRIBE 收到回推值 = 写入值 → **PASS (got 99)**

---

## 6. 已知设计约束（非 bug）

| # | 现象 | 根因 | 缓解 |
|---|---|---|---|
| 1 | inline 文本框 SET 失败 "write failed" | `CoreTagWriter` 要求二进制 LE 值（i64 需 8 字节），inline 模式发文本 token | 内联框 placeholder 注释；正确做法用 Tab4 的按钮模式（client.set + encodeValue） |
| 2 | inline GET 显示乱码字节 | inline 模式 RESP 帧对 binary 直接显示原始字节 | 设计如此，二进制值需客户端按 type 解码 |
| 3 | 重复 SET 相同值"无推送" | `TagEngine::write()` 仅在值实际变化时触发回调（`tag_engine.cpp:29`） | 正确语义；测试写回时强制使用新值 |
| 4 | SET `demo/*` 吞吐 ~50x 慢于 GET | SET 在 IRSP server 递归锁内同步调用 plugin `onWrite` | 设计如此；设备写回天然比读慢 |

---

## 7. 视觉设计验收

| 项 | 实现 |
|---|---|
| 主题 | `data-theme="irsp"` 覆盖 daisyUI CSS 变量（`--p`/`--b1`/`--bc` 等） |
| 字体 | JetBrains Mono via Google Fonts，`font-feature-settings: "tnum" 1` 对齐数字 |
| 配色 | bg `#0a0e14` / cyan `#00d4ff` / orange `#ff8c42` |
| Top bar | brand icon + live pill（pulse 动画）+ server info + 连接控件 |
| Tab nav | 顶部 tab + 青色下划线 active 态 |
| 变化指示 | ▲ 绿 / ▼ 红，1 秒衰减 |
| 事件分级 | severity bar 颜色 + critical 红色辉光 |
| 历史对比 | ECharts dashed 参考线 + delta 百分比 |

无 JS console 错误（仅 Tailwind CDN 开发模式警告，预期）。

---

## 8. 合并可行性矩阵

| 目标分支 | 文件重叠 | 冲突预期 | 副作用 |
|---|---|---|---|
| `main` | 0 | 无（fast-forward） | — |
| `dev` | 0 | 无（merge commit） | **会同时带入 main 的 2 个 commit**（`6b14b95` 核心功能 + `7aea394` 插件/CLI），因为 dev 从 v0.1.0-beta 分叉未合并 main |
| `feature/core` | 0 | 无 | 同上 |

---

## 9. 结论

- **L0/L1/L2/L3 全部 PASS**
- 28 单元测试 + 构建产物 + 运行时集成 + 4 Tab 浏览器端到端 全部验证通过
- demo-writeback 夹具已迁移到 `tools/`，符合仓库目录语义
- 零冲突，可直接合入 `dev` 作为下一次大版本集成分支
