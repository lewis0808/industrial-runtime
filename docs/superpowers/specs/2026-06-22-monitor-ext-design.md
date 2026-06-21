# IRSP Monitor 页面扩展设计

> **日期**：2026-06-22
> **分支**：`feat/monitor-ext`
> **目标**：扩展现有 `sdk/irsp-client/JS/examples/index.html`，使其能对 runtime 做功能覆盖测试与性能压测
> **范围**：JS SDK 扩展 + 页面重构 + demo 写回插件（测试夹具）

---

## 1. 背景与动机

现有 `index.html`（154 行单文件）只用了 `IrspClient` 的 6 个 API（connect / scan / mget / subscribe / subevent / bye），仅能"看"，不能"测"：

- **未覆盖的读命令**：`get / mget / exists / watch / unwatch / unsubscribe / unsubevent / ping`
- **完全缺失**：`SET` 写回——JS SDK 的 `client.js` 根本没有 `set()` 方法
- **未覆盖的能力**：性能压测、写回链路端到端验证

**目标分两层**：
1. **功能覆盖**：把 IRSP 所有命令做成可视化按钮，逐个验证
2. **性能压测**：测吞吐、延迟百分位、并发连接数等硬指标

不在本设计范围：
- 场景模拟（多设备、报警风暴）—— 留给未来
- 自动化回归 runner —— 留给未来

---

## 2. 架构与文件布局

### 2.1 改动面（4 个）

```
sdk/irsp-client/JS/
├── src/
│   ├── client.js          [改] 加 set(name, value, type?) 方法
│   ├── irsp1.js           [改] 补 encodeValue(type, value)，与 decodeValue 对称
│   └── perf.js            [新] 压测工具（滑动窗口、百分位、pipeline）
├── examples/
│   ├── index.html         [改] 重构成 tab 布局，引 daisyUI + ECharts
│   ├── tabs/
│   │   ├── monitor.js     [新] Tab1 实时监控（搬现有逻辑）
│   │   ├── command.js     [新] Tab2 命令测试台
│   │   ├── benchmark.js   [新] Tab3 性能压测
│   │   └── writeback.js   [新] Tab4 SET 写回测试
│   └── serve.mjs          [不动]
└── test/
    ├── set.test.js            [新] set() 编码测试
    ├── encode-value.test.js   [新] encodeValue round-trip
    └── perf.test.js           [新] SlidingStats / Pipeline

core/tests/fixtures/demo-writeback/   [新] 测试夹具
├── CMakeLists.txt
├── irplugin/              [SDK 头副本，照 sdk/plugin-sdk/example 模式]
└── src/
    └── demo_writeback.cpp  注册 "demo/" 前缀 ownership，
                             onWrite 时 pushTag 原样 echo
```

### 2.2 demo 插件定位

**不是** `sdk/plugin-sdk/` 下的教学示例（读者是插件开发者）。**是** `core/tests/fixtures/` 下的测试夹具（读者是 runtime 自身的端到端验证）。

### 2.3 build 集成

- `core/tests/fixtures/demo-writeback/CMakeLists.txt` 编出 `demo_writebook.dll`，`RUNTIME_OUTPUT_DIRECTORY` 指向 `${CMAKE_BINARY_DIR}/plugins/`，runtime 自动发现
- 仅当 `IR_BUILD_TESTS=ON` 时 `add_subdirectory()`
- 不进主构建路径

### 2.4 规模估算

约 1200 行代码，分阶段实施：

| §  | 内容 | 规模 |
|---|---|---|
| §2 | Tab 布局 + daisyUI | HTML ~100 行 |
| §3 | Tab1 实时监控 | monitor.js ~80 行（搬现有） |
| §4 | Tab2 命令测试台 | command.js ~150 行 |
| §5 | Tab3 性能压测（场景二延期） | benchmark.js ~250 行 + perf.js ~100 行 |
| §6 | Tab4 SET 写回 | writeback.js ~150 行 |
| §7 | SDK 扩展 | client.js +15 / irsp1.js +40 / perf.js ~100 |
| §8 | demo 插件 | demo_writeback.cpp ~100 + CMake |
| §9 | 错误处理 / 测试 / 验收 | SDK 单测 ~100 行 |

---

## 3. Tab 布局与共享状态

### 3.1 顶栏（常驻）

```
┌──────────────────────────────────────────────────────────────┐
│ ● IRSP Live Monitor    [ws://127.0.0.1:9777] [连接] [断开]   │
│                        server: ir/1 · json · ws              │
└──────────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────────┐
│ [实时监控]  [命令测试]  [性能压测]  [写回测试]               │
└──────────────────────────────────────────────────────────────┘
```

- 连接控制（URL 输入、连接/断开按钮、状态指示）留在顶栏，**全局唯一**
- 4 个 tab 共享同一个 `IrspClient` 实例（由 `index.html` 持有，传给各 tab 模块）
- 压测用**独立连接**（不复用 monitor 的 client），避免 monitor 的请求-响应被挤住

### 3.2 Tab 模块接口

每个 tab 是独立 ES module，导出：

```js
export default {
  init(client, sharedState) { /* 拿到 client 引用、注册 DOM */ },
  onConnect()  { /* 连接成功：启用按钮、刷新 */ },
  onDisconnect() { /* 清状态、禁用按钮 */ },
  onShow()     { /* 切到本 tab 时回调 */ },
  onHide()     { /* 切走时回调（如暂停压测）*/ },
};
```

### 3.3 事件路由

- `index.html` 统一管 `client.on('tag'/'event'/'close'/'error')` 事件分发
- `tag` / `event` 事件按 tab 当前可见性路由：只 monitor tab 订阅渲染；benchmark tab 不消费 monitor 推送
- 连接断开时调所有 tab 的 `onDisconnect()`

### 3.4 样式方案

Tailwind Play CDN + daisyUI 插件，纯 CDN，无构建步骤（保留"零依赖静态服务器"精神）：

```html
<script src="https://cdn.tailwindcss.com?plugins=forms,typography"></script>
<link href="https://cdn.jsdelivr.net/npm/daisyui@5/dist/full.css" rel="stylesheet">
```

用 daisyUI 主题（`business` 或 `dark`）替代现有手写 CSS 变量。现有 154 行自定义 CSS 基本可丢，靠 `btn` / `card` / `badge` / `tabs` / `table` / `input` 等组件类拼。

图表用 ECharts（CDN）。

### 3.5 Tab 切换

纯 CSS（`display: none` / `block`），无路由无 iframe。

---

## 4. Tab 1：实时监控

现有逻辑原样搬进 `tabs/monitor.js`，功能不变：

```
┌─────────────────────────────┐
│ 订阅模式: [#______] [应用]  │
├──────────────┬──────────────┤
│ Tags 表格    │ Events 流    │
│ topic/type/  │ sev/category │
│ val/time     │ :msg/time    │
│ (按名排序)    │ (最多100条)  │
└──────────────┴──────────────┘
```

**唯一增强**：Tags 表格支持按 topic 前缀过滤输入框（比如只看 `demo/*`），客户端过滤，不重新订阅。

连接后流程（保持不变）：`scan` 预填 → `subscribe(pattern)` → `subevent('info')`。

---

## 5. Tab 2：命令测试台

把 IRSP 所有命令做成可视化按钮 + 自由编辑参数，逐个验证。覆盖 SDK 所有方法。

### 5.1 布局

左边命令网格，右边日志：

```
┌──────────────────────────────┬──────────────────────────────┐
│ 连接命令                     │  请求/响应 日志              │
│ [HELLO] [PING] [BYE]         │  → PING                      │
│                              │  ← pong (0.8ms)              │
│ 读命令                       │                              │
│ GET [name____] [执行]        │  → GET demo/foo              │
│ MGET [names___] [执行]       │  ← {name,type,ts,value}      │
│ EXISTS [name__] [执行]       │                              │
│ SCAN [cursor] [pattern] [执行]│  → SET demo/foo 42          │
│                              │  ← ok (1.2ms)                │
│ 订阅命令                     │                              │
│ WATCH [names__] [执行]       │  → SUBSCRIBE demo/#          │
│ SUBSCRIBE [patterns] [执行]  │  ← 1                         │
│ SUBEVENT [sev] [执行]        │                              │
│ [UNWATCH] [UNSUBSCRIBE]      │  [清空]                      │
│ [UNSUBEVENT]                 │                              │
│                              │                              │
│ 写命令                       │                              │
│ SET [name__] [type▼] [val_]  │                              │
│     [执行]                   │                              │
└──────────────────────────────┴──────────────────────────────┘
```

### 5.2 设计点

1. **每个命令一行**：参数用 `<input>` / `<select>`，点按钮执行。SDK 方法名映射到按钮 label（`GET → client.get(name)`）。
2. **`type` 下拉**：`i64 / f64 / bool / string / binary`（对应 IRSP DataType）。SET 时必选；GET 不需要。
3. **请求/响应日志**：右侧滚动列表，每条：
   ```
   → <命令> <参数>            <timestamp>
   ← <响应 JSON / error>      <耗时 ms>
   ```
   推送（tag/event）不算请求响应，不在这里显示（Tab1 负责）。错误（`IrspError`）红色高亮。
4. **耗时**：每条调用记录 round-trip ms。
5. **保留最近 200 条**，可清空，可复制全部为文本。
6. **批量场景**：底部"自由命令"textarea，每行一条，Ctrl+Enter 顺序执行。

### 5.3 SDK 边界

命令测试台只调 SDK 已有方法，不直接拼底层帧。SET 走 §7 新加的 `client.set()`。

---

## 6. Tab 3：性能压测

测三个维度：**请求-回复吞吐/延迟**、**推送扇出（延期）**、**并发连接**。

### 6.1 布局

```
┌────────────────────────────────────────────────────────────┐
│ 场景一：请求-回复吞吐                                       │
│ 命令 [GET▼] name [demo/foo____] 并发 [4_] 时长 [10s]       │
│ 指标：ops/s 12345 | p50 0.12ms | p95 0.31ms | p99 1.8ms   │
│                                            [开始] [停止]   │
├────────────────────────────────────────────────────────────┤
│ 场景二：订阅推送扇出  [延期 — Phase 2]                     │
│ 依赖 demo 插件 + 双连接（writer 灌 + subscriber 统计）     │
│ 完整设计保留但本期不实现                                    │
├────────────────────────────────────────────────────────────┤
│ 场景三：多连接并发                                          │
│ 连接数 [16_] 每连接 ops [100/s] 时长 [10s]                 │
│ 指标：总 ops/s | 平均延迟 | 错误数                         │
│                                            [开始] [停止]   │
├────────────────────────────────────────────────────────────┤
│ ECharts 时序图：ops/s、延迟百分位、错误数                  │
└────────────────────────────────────────────────────────────┘
```

### 6.2 场景一：请求-回复吞吐/延迟

- 命令选择（GET/MGET/EXISTS/SCAN/SET/PING）+ 参数 + 并发深度 N + 时长
- **Pipeline 模式**：单个 WebSocket 连接的请求是 FIFO 串行回复（SDK `_pending` 队列）。"并发"只能靠 pipeline（一个连接维持 N 个在途请求）或多连接（场景三）。本场景默认 pipeline。
- 跑法：开 1 个 worker，在时长内维持 N 个在途请求，用 `perf.js` 的滑动窗口（1s 桶）统计：
  - `ops/s`：每秒回复数
  - `p50 / p95 / p99`：round-trip 延迟百分位
  - `errors`：错误计数

### 6.3 场景二：订阅推送扇出 **[延期 — Phase 2]**

完整设计保留，本期不实现。

未来实现要点：
- 订阅一个 pattern（默认 `#`），runtime 端心跳每秒 1 个**没法压**。要测大流量需要**双连接**：writer 连接疯狂 SET `demo/bench/<i>`，subscriber 连接收推送统计
- 前置依赖：`demo-writeback` 插件已加载（否则 SET `demo/*` 没主）
- 指标：`pushes/s`、`丢包率`（payload 序号检测 gap）、`端到端延迟 p50/p95/p99`（payload 带 sendTs，接收端算 delta）

### 6.4 场景三：多连接并发

- 开 M 个 `IrspClient`，每个每秒发 R 个 GET，统计总和
- 测 runtime WebSocket 服务的并发承载力
- 指标：`总 ops/s`、`平均延迟`、`失败数`

### 6.5 ECharts 图表

三个场景共用一个时序图区域：
- X 轴：时间（秒）
- Y 轴 1：ops/s（柱状或折线）
- Y 轴 2：p50/p95/p99 延迟（折线）
- 错误数叠加

### 6.6 注意点

- 压测期间请求量大，会污染 Tab1 monitor（订阅会收到 `demo/bench/*`）。压测前自动 UNSUBSCRIBE `demo/bench/#`，或压测用独立前缀并让 monitor 自动过滤
- 压测用**独立连接**（不复用 monitor 的 client）
- 停止条件：时长到自动停 / 切 tab 触发 `onHide()` / 连接断开

---

## 7. Tab 4：SET 写回测试

验证写回链路 `应用 → runtime → 插件 → 设备`。前置依赖 `demo-writeback` 插件已加载。

### 7.1 布局

```
┌──────────────────────────────────────────────────────────┐
│ 写回链路状态检测                                          │
│ 插件 demo-writeback：[✓ 已加载 / ✗ 未加载]  [重新检测]   │
│ (检测方法：SET demo/__probe__ = 1，500ms 内              │
│  demo/__probe__ tag 推回，视为已加载)                    │
├──────────────────────────────────────────────────────────┤
│ 单次写回测试                                              │
│ topic [demo/foo______]  type [i64▼]  value [42_]         │
│ [发送 SET]                                                │
│                                                          │
│ ECharts 时间轴：                                         │
│   t=0      发出 SET demo/foo = 42                        │
│   t=+0.8ms 收到回复 'ok'                                  │
│   t=+1.5ms 推送到达 demo/foo (值 42)                      │
│   总往返：1.5ms ✓                                         │
├──────────────────────────────────────────────────────────┤
│ 批量写回                                                  │
│ 前缀 [demo/batch/]  数量 [100_]  类型 [i64▼]             │
│ 起始值 [0_]  模式 [顺序▼ / 随机]                          │
│ [开始]                                                   │
│                                                          │
│ 进度：[###########   ] 78/100                             │
│ ECharts：每条 SET 的往返延迟分布（散点图）                │
├──────────────────────────────────────────────────────────┤
│ 写回断言                                                  │
│ 断言：SET 之后的 N ms内，该 topic 的 tag 推送值           │
│       与 SET 值一致                                       │
│ [运行断言]  → ✓ PASS / ✗ FAIL (不一致 / 超时)             │
└──────────────────────────────────────────────────────────┘
```

### 7.2 两层断言模型

SET 语义是"runtime 受理"，不是"设备已执行"。断言分两层：

- **L1（同步受理）**：SET 命令返回 `ok`（runtime 收下）
- **L2（echo 回环）**：demo 插件把值原样 pushTag 回来，N ms内推送值 == SET 值

L2 只在 demo 插件下成立。真实插件可能不 echo（设备有自己的语义），但作为**测试夹具**，demo 插件故意 echo 才能闭环。

两层断言都做，独立报告。

### 7.3 插件检测

用"探针 SET"判断插件在不在：SET `demo/__probe__=1`，订阅 `demo/__probe__`，500ms 内收到推送即视为已加载。

**不接 admin 通道**（保持纯数据面），代价是检测的是"插件可响应 SET"而非"插件出现在 PLUGIN LIST"。

### 7.4 demo 插件协议约定

- `demo/__probe__` 保留 topic，任何值都 echo
- `demo/batch/<n>` 用于批量
- `demo/foo` 等任意 topic 都 echo
- 插件对任何 `demo/*` 的 SET 都：① pushTag 原样写回 ② pushEvent('writeback', 'info', '<topic> <= <value>')

---

## 8. SDK 扩展

### 8.1 `client.js` 新增 `set()` 方法

```js
/**
 * @param {string} name     topic 名（如 'demo/foo'）
 * @param {any}    value    值（number / bigint / boolean / string / Uint8Array）
 * @param {string} [type]   'i64' | 'f64' | 'bool' | 'string' | 'binary'，省略时由值推断
 * @returns {Promise<string>}  'ok' | 'accepted' | 'not_owner' | ...
 */
async set(name, value, type) {
  const t = type ?? inferType(value);
  const r = await this._send(['SET', name, t, encodeValue(t, value)]);
  return asStr(r);
}
```

### 8.2 `irsp1.js` 补 `encodeValue(type, value)`

现有 `decodeValue` 的对称函数。对照 IRSP1 二进制编码规则补齐。这是 SDK 的功能缺口（没有它 SET 没法发）。

### 8.3 `perf.js` 新增

- `inferType(value)` / `encodeValue(type, value)` —— 从 `irsp1.js` 抽出来复用
- `class SlidingStats` —— 1 秒桶滚动窗口
  - `record(latencyMs)` 记一条
  - `summary()` 返回 `{ opsPerSec, p50, p95, p99, errors }`
- `percentile(sortedSamples, p)` —— 简单线性插值，样本量 < 10000 够用
- `class Pipeline` —— 在单连接上维持 N 个在途请求
  - `run(sendFn, concurrency, durationMs, onSample)` —— 压测主循环
- ECharts helper：`makeTimeSeriesChart(dom, series)` 简单封装

---

## 9. demo-writeback 插件

### 9.1 文件

```
core/tests/fixtures/demo-writeback/
├── CMakeLists.txt
├── irplugin/              [复制 sdk/plugin-sdk/irplugin/ 头副本]
└── src/
    └── demo_writeback.cpp
```

### 9.2 行为

纯测试夹具，最小实现，不连任何真实设备：

```cpp
class DemoWritebackPlugin : public irplugin::IPlugin {
    Host* host_;
public:
    bool init() override { return true; }
    bool start() override {
        host_->registerWriter("demo/");   // 声明 ownership
        return true;
    }
    bool stop() override { return true; }
    bool destroy() override { return true; }

    // SET 写回回调
    void onWrite(const TagValue& tag) override {
        host_->pushTag(tag);              // echo 回 TagEngine
        host_->pushEvent({
            "demo-writeback", "writeback",
            std::format("{} <= {}", tag.name, tag.value),
            EventSeverity::Info
        });
    }
};
```

### 9.3 CMake

```cmake
add_library(demo_writeback SHARED src/demo_writeback.cpp)
target_link_libraries(demo_writeback PRIVATE irplugin)
set_target_properties(demo_writeback PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
)
```

仅当 `IR_BUILD_TESTS=ON` 时 `add_subdirectory()`。

### 9.4 ABI 风格

按当前 plugin SDK v2 ABI——`createPlugin(host, configPath, out)` 填 `IrPluginInstance` C vtable。照 `sdk/plugin-sdk/example/` 的写法复用 `irplugin::makeInstance`。

### 9.5 安全检查

- topic 前缀严格匹配 `demo/`（不接受 `demo` 不带斜杠，避免误匹配）
- `onWrite` 中 pushTag 用 `std::move` 避免拷贝
- 不读配置文件（configPath 透传忽略）

---

## 10. 错误处理矩阵

| 场景 | 行为 |
|---|---|
| 连接失败 / 中途断开 | 所有 tab 的 `onDisconnect()` 被调用；按钮禁用；压测停止；探针超时 |
| 命令执行抛 `IrspError` | Tab2 日志红色高亮；Tab4 断言标 FAIL；Tab3 计入 errors |
| 插件未加载时跑写回 | 探针检测阶段就 fail-fast 提示，不进入后续测试 |
| 压测切 tab | `onHide()` 停止；切回保留结果，不自动续跑 |
| ECharts/CDN 拉不到 | 降级：数字照常显示，图表区域显示"图表加载失败" |
| 多 tab 同时跑压测 + 写回 | 允许；Tab1 订阅 `#` 会被吵到 → monitor tab 提供前缀过滤（§4） |
| push 推送序号 gap（场景二延期） | 未来实现时标"丢包"，计入 `dropped` |

---

## 11. 测试范围

SDK 改动加单测（`sdk/irsp-client/JS/test/`）：

- `set.test.js` —— `client.set()` 编码正确（mock WebSocket）
- `encode-value.test.js` —— `encodeValue` 对 5 种类型的 round-trip（`encode → decode`）
- `perf.test.js` —— `SlidingStats` 百分位、桶滚动；`Pipeline` 并发度控制

跑：`node --test`（package.json 已配）

demo 插件无单测（纯夹具，端到端验证即可）。

页面无自动化测试，验收靠手工清单（§12）。

---

## 12. 验收清单（手工）

1. 启动 runtime + `serve.mjs`，页面连接成功
2. Tab1 看到心跳 `system/heartbeat` 每秒更新
3. Tab2 点 PING → 日志显示 `pong` + 耗时
4. Tab2 GET `system/heartbeat` → 返回当前值
5. Tab2 SET `demo/foo = 42` → 返回 `ok`
6. Tab3 场景一跑 10s GET `system/heartbeat` 并发 4 → 出 ops/s + 延迟百分位 + ECharts 曲线
7. Tab3 场景三开 16 连接各 100 ops/s → 总 ops/s 数字合理
8. Tab4 探针检测 demo 插件 ✓ 加载
9. Tab4 单次写回 SET `demo/foo=42` → ECharts 时间轴显示三事件
10. Tab4 断言运行 → PASS
11. Ctrl+C runtime → 页面显示断开，可重连

---

## 13. 实施阶段

**Phase 1（本次实施）**：
- §2-§4：Tab 布局 + Tab1
- §5：Tab2 命令测试台
- §6 场景一、场景三：请求-回复压测 + 多连接并发
- §7：Tab4 写回测试
- §8：SDK 扩展（set / encodeValue / perf.js）
- §9：demo 插件
- §11：SDK 单测

**Phase 2（延期）**：
- §6 场景二：推送扇出（依赖 demo 插件 + 双连接）

---

*本文档由 brainstorming 流程生成，待用户复核后转入 writing-plans。*
