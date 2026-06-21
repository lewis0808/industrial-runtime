# IRSP Monitor Extension Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `sdk/irsp-client/JS/examples/index.html` 扩展成 4 tab 测试台（实时监控 / 命令测试 / 性能压测 / SET 写回），覆盖 IRSP 所有命令、能压吞吐与延迟、验证写回链路。

**Architecture:** Tab 布局单页应用，daisyUI + ECharts CDN。4 个 tab 各为独立 ES module，共享顶栏的 `IrspClient`。压测用独立连接避免污染 monitor。SDK 补 `set()` / `encodeValue()` / `perf.js`。测试夹具 `demo-writeback` 插件作为 SET echo 回路。

**Tech Stack:** Vanilla JS（ES module）、daisyUI 5 + Tailwind Play CDN、ECharts CDN、IRSP1 二进制协议、C++20 plugin SDK v2 ABI。

**Spec:** `docs/superpowers/specs/2026-06-22-monitor-ext-design.md`

---

## 文件结构

```
sdk/irsp-client/JS/
├── src/
│   ├── client.js          [改] +set() 方法
│   ├── irsp1.js           [改] +encodeValue() +inferType()
│   └── perf.js            [新] SlidingStats + Pipeline + ECharts helper
├── examples/
│   ├── index.html         [改] 重构为 tab 布局
│   └── tabs/              [新目录]
│       ├── monitor.js
│       ├── command.js
│       ├── benchmark.js
│       └── writeback.js
└── test/
    ├── encode-value.test.js   [新]
    ├── set.test.js            [新]
    └── perf.test.js           [新]

core/tests/fixtures/demo-writeback/   [新目录]
├── CMakeLists.txt
├── irplugin/                      [复制 sdk/plugin-sdk/example/irplugin/]
└── src/
    └── demo_writeback.cpp
```

**责任边界**：
- `irsp1.js`：纯协议编解码，无状态
- `client.js`：连接生命周期 + 所有 IRSP 命令的 Promise 化包装
- `perf.js`：压测工具，不依赖 DOM（除 ECharts helper）
- `tabs/*.js`：DOM + 业务逻辑，每个 tab 一个 module，导出统一接口
- `demo_writeback.cpp`：独立 C++ 工程式插件，零 runtime 依赖

---

## Task 1: 在 irsp1.js 加 encodeValue（TDD）

**Files:**
- Modify: `sdk/irsp-client/JS/src/irsp1.js`
- Create: `sdk/irsp-client/JS/test/encode-value.test.js`

### Steps

- [ ] **Step 1: 写失败测试**

Create `sdk/irsp-client/JS/test/encode-value.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { encodeValue, decodeValue } from '../src/irsp1.js';

test('encodeValue bool round-trip', () => {
  const bytes = encodeValue('bool', true);
  assert.deepEqual(Array.from(bytes), [1]);
  assert.equal(decodeValue('bool', bytes), true);
});

test('encodeValue i32 round-trip', () => {
  const bytes = encodeValue('i32', 123456);
  assert.equal(bytes.length, 4);
  assert.equal(decodeValue('i32', bytes), 123456);
});

test('encodeValue i64 round-trip', () => {
  const bytes = encodeValue('i64', 9007199254740993n);
  assert.equal(bytes.length, 8);
  assert.equal(decodeValue('i64', bytes), 9007199254740993n);
});

test('encodeValue f64 round-trip', () => {
  const bytes = encodeValue('f64', 3.141592653589793);
  assert.equal(bytes.length, 8);
  assert.equal(decodeValue('f64', bytes), 3.141592653589793);
});

test('encodeValue str round-trip', () => {
  const bytes = encodeValue('str', 'hello 世界');
  assert.equal(decodeValue('str', bytes), 'hello 世界');
});

test('encodeValue i16 round-trip', () => {
  const bytes = encodeValue('i16', -32000);
  assert.equal(decodeValue('i16', bytes), -32000);
});

test('encodeValue throws on unknown type', () => {
  assert.throws(() => encodeValue('unknown', 1), /unknown type/);
});
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd sdk/irsp-client/JS && node --test test/encode-value.test.js
```

Expected: FAIL（`encodeValue is not a function` 或 import 错误）

- [ ] **Step 3: 实现 encodeValue**

在 `sdk/irsp-client/JS/src/irsp1.js` 末尾追加（与 `decodeValue` 对称）：

```js
/** JS 值 → 类型标签对应的小端字节（与 decodeValue 对称，见 datatype.md）。 */
export function encodeValue(type, value) {
  switch (type) {
    case 'null': return new Uint8Array(0);
    case 'bool': return new Uint8Array([value ? 1 : 0]);
    case 'i8': {
      const b = new Uint8Array(1);
      new DataView(b.buffer).setInt8(0, value);
      return b;
    }
    case 'i16': {
      const b = new Uint8Array(2);
      new DataView(b.buffer).setInt16(0, value, true);
      return b;
    }
    case 'i32': {
      const b = new Uint8Array(4);
      new DataView(b.buffer).setInt32(0, value, true);
      return b;
    }
    case 'i64': {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setBigInt64(0, BigInt(value), true);
      return b;
    }
    case 'u8': {
      const b = new Uint8Array(1);
      new DataView(b.buffer).setUint8(0, value);
      return b;
    }
    case 'u16': {
      const b = new Uint8Array(2);
      new DataView(b.buffer).setUint16(0, value, true);
      return b;
    }
    case 'u32': {
      const b = new Uint8Array(4);
      new DataView(b.buffer).setUint32(0, value, true);
      return b;
    }
    case 'u64': {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setBigUint64(0, BigInt(value), true);
      return b;
    }
    case 'f32': {
      const b = new Uint8Array(4);
      new DataView(b.buffer).setFloat32(0, value, true);
      return b;
    }
    case 'f64': {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setFloat64(0, value, true);
      return b;
    }
    case 'str': return new TextEncoder().encode(value);
    default: throw new Error(`irsp1: unknown type "${type}"`);
  }
}
```

- [ ] **Step 4: 运行测试，确认通过**

```bash
cd sdk/irsp-client/JS && node --test test/encode-value.test.js
```

Expected: PASS（7/7）

- [ ] **Step 5: 提交**

```bash
git add sdk/irsp-client/JS/src/irsp1.js sdk/irsp-client/JS/test/encode-value.test.js
git commit -m "feat(irsp-client): 加 encodeValue 对称函数"
```

---

## Task 2: 在 irsp1.js 加 inferType（TDD）

**Files:**
- Modify: `sdk/irsp-client/JS/src/irsp1.js`
- Modify: `sdk/irsp-client/JS/test/encode-value.test.js`

### Steps

- [ ] **Step 1: 写失败测试**

追加到 `sdk/irsp-client/JS/test/encode-value.test.js`：

```js
import { inferType } from '../src/irsp1.js';

test('inferType bigint -> i64', () => {
  assert.equal(inferType(123n), 'i64');
});

test('inferType number int-range -> i32 or i64', () => {
  assert.equal(inferType(42), 'i32');
  assert.equal(inferType(Number.MAX_SAFE_INTEGER + 1), 'f64'); // 超过安全整数范围
});

test('inferType number float -> f64', () => {
  assert.equal(inferType(3.14), 'f64');
});

test('inferType boolean -> bool', () => {
  assert.equal(inferType(true), 'bool');
});

test('inferType string -> str', () => {
  assert.equal(inferType('hello'), 'str');
});

test('inferType Uint8Array -> binary', () => {
  assert.equal(inferType(new Uint8Array([1, 2, 3])), 'binary');
});
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd sdk/irsp-client/JS && node --test test/encode-value.test.js
```

Expected: FAIL（`inferType is not a function`）

- [ ] **Step 3: 实现 inferType**

在 `sdk/irsp-client/JS/src/irsp1.js` 末尾追加：

```js
/** 由 JS 值推断 IRSP 类型标签（用于 SET 无显式 type 时）。 */
export function inferType(value) {
  if (typeof value === 'bigint') return 'i64';
  if (typeof value === 'boolean') return 'bool';
  if (typeof value === 'number') {
    if (Number.isInteger(value) && Math.abs(value) <= 0x7fffffff) return 'i32';
    return 'f64';
  }
  if (typeof value === 'string') return 'str';
  if (value instanceof Uint8Array) return 'binary';
  throw new Error(`irsp1: cannot infer type for ${typeof value}`);
}
```

注意：`binary` 类型 runtime 不一定原生支持，但 SET 时按用户传入的 raw bytes 编码（对应 irsp1 bulk）。`encodeValue('binary', x)` 应当原样返回。

- [ ] **Step 4: 处理 binary 类型**

修改 `encodeValue` 的 default 分支之前加：

```js
case 'binary': {
  if (!(value instanceof Uint8Array)) throw new Error('irsp1: binary expects Uint8Array');
  return value;
}
```

- [ ] **Step 5: 运行测试，确认通过**

```bash
cd sdk/irsp-client/JS && node --test test/encode-value.test.js
```

Expected: PASS（13/13）

- [ ] **Step 6: 提交**

```bash
git add sdk/irsp-client/JS/src/irsp1.js sdk/irsp-client/JS/test/encode-value.test.js
git commit -m "feat(irsp-client): 加 inferType 类型推断"
```

---

## Task 3: client.set() 方法（TDD）

**Files:**
- Modify: `sdk/irsp-client/JS/src/client.js`
- Create: `sdk/irsp-client/JS/test/set.test.js`

### Steps

- [ ] **Step 1: 写失败测试（mock WebSocket）**

Create `sdk/irsp-client/JS/test/set.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { IrspClient } from '../src/client.js';

// Mock WebSocket for Node. 捕获发出去的帧，可手动注入回复。
class MockWebSocket {
  constructor() {
    this.readyState = 1; // OPEN
    this.sent = [];
    this._handlers = {};
  }
  send(data) { this.sent.push(new Uint8Array(data)); }
  close() { this.readyState = 3; }
  emit(message) { this._handlers.message({ data: message.buffer }); }
  set onmessage(fn) { this._handlers.message = fn; }
  set onopen(fn) { this._open = fn; }
  set onclose(fn) {}
  set onerror(fn) {}
}

test('set() encodes SET name type value', async () => {
  const client = new IrspClient('ws://x');
  const mock = new MockWebSocket();
  client.ws = mock;

  const promise = client.set('demo/foo', 42n, 'i64');
  // 让 microtask 跑完 _send 入队
  await Promise.resolve();

  // 验证发出的帧是 irsp1 bulk array
  const sent = mock.sent[0];
  const dec = new TextDecoder();
  const text = dec.decode(sent);
  assert.ok(text.startsWith('*4\r\n'), `array of 4, got: ${text}`);
  assert.ok(text.includes('$3\r\nSET\r\n'));
  assert.ok(text.includes('$8\r\ndemo/foo\r\n'));
  assert.ok(text.includes('$3\r\ni64\r\n'));
  // value 是 8 字节小端 bulk（42 = 0x2A）
  assert.ok(text.includes('$8\r\n'));

  // 回复 '+ok'
  const ok = new TextEncoder().encode('+ok\r\n');
  mock.emit(ok);
  const r = await promise;
  assert.equal(r, 'ok');
});

test('set() infers type when not provided', async () => {
  const client = new IrspClient('ws://x');
  const mock = new MockWebSocket();
  client.ws = mock;

  const promise = client.set('demo/bar', 3.14);
  await Promise.resolve();

  const sent = mock.sent[0];
  const text = new TextDecoder().decode(sent);
  // 应推断为 f64
  assert.ok(text.includes('$3\r\nf64\r\n'), text);

  const ok = new TextEncoder().encode('+ok\r\n');
  mock.emit(ok);
  await promise;
});
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd sdk/irsp-client/JS && node --test test/set.test.js
```

Expected: FAIL（`client.set is not a function`）

- [ ] **Step 3: 实现 set()**

修改 `sdk/irsp-client/JS/src/client.js`：

1. 改第 6 行 import：
```js
import { encodeRequest, decode, decodeValue, asStr, IrspError, encodeValue, inferType } from './irsp1.js';
```

2. 在 `exists()` 方法后面（第 132 行附近，"// ---- 订阅 ----" 之前）插入：

```js
  /**
   * 写回（SET）。runtime 收下后路由给拥有该 topic 前缀的插件。
   * @param {string} name
   * @param {any} value
   * @param {string} [type] 'i64'|'f64'|'bool'|'str'|'i32'|...，省略则按 JS 值推断
   * @returns {Promise<string>} 'ok' | 'accepted' | 'not_owner' | ...
   */
  async set(name, value, type) {
    const t = type ?? inferType(value);
    const payload = encodeValue(t, value);
    const r = await this._send(['SET', name, t, payload]);
    return asStr(r);
  }
```

- [ ] **Step 4: 运行测试，确认通过**

```bash
cd sdk/irsp-client/JS && node --test test/set.test.js
```

Expected: PASS（2/2）

- [ ] **Step 5: 跑所有 JS 测试确认无回归**

```bash
cd sdk/irsp-client/JS && node --test
```

Expected: 既有测试全 PASS

- [ ] **Step 6: 提交**

```bash
git add sdk/irsp-client/JS/src/client.js sdk/irsp-client/JS/test/set.test.js
git commit -m "feat(irsp-client): client.set() 写回方法"
```

---

## Task 4: perf.js — SlidingStats + percentile（TDD）

**Files:**
- Create: `sdk/irsp-client/JS/src/perf.js`
- Create: `sdk/irsp-client/JS/test/perf.test.js`

### Steps

- [ ] **Step 1: 写失败测试**

Create `sdk/irsp-client/JS/test/perf.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SlidingStats, percentile } from '../src/perf.js';

test('percentile of simple array', () => {
  assert.equal(percentile([1, 2, 3, 4, 5], 50), 3);
  assert.equal(percentile([1, 2, 3, 4, 5], 100), 5);
  assert.equal(percentile([1, 2, 3, 4, 5], 0), 1);
});

test('percentile interpolates', () => {
  // p90 of [10,20,30,40,50,60,70,80,90,100]
  // rank = 0.9 * (10-1) = 8.1 -> interpolate between index 8 (90) and 9 (100)
  const v = percentile([10,20,30,40,50,60,70,80,90,100], 90);
  assert.ok(v >= 90 && v <= 100, `got ${v}`);
});

test('SlidingStats records and summarizes', () => {
  const s = new SlidingStats(1); // 1 秒窗口
  s.record(1);
  s.record(2);
  s.record(3);
  const sum = s.summary();
  assert.equal(sum.count, 3);
  assert.equal(sum.opsPerSec, 3); // 假设窗口内 1 秒内全部
  assert.equal(sum.p50, 2);
  assert.equal(sum.p100, 3);
  assert.equal(sum.errors, 0);
});

test('SlidingStats tracks errors', () => {
  const s = new SlidingStats(1);
  s.record(1);
  s.recordError();
  s.record(2);
  const sum = s.summary();
  assert.equal(sum.count, 2);
  assert.equal(sum.errors, 1);
});
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd sdk/irsp-client/JS && node --test test/perf.test.js
```

Expected: FAIL（`Cannot find module '../src/perf.js'`）

- [ ] **Step 3: 实现 percentile + SlidingStats**

Create `sdk/irsp-client/JS/src/perf.js`:

```js
// 压测工具：滑动窗口统计、百分位、Pipeline 控制器。

/** 简单线性插值百分位。samples 不需要预排序（内部排序）。 */
export function percentile(samples, p) {
  if (samples.length === 0) return NaN;
  if (p <= 0) return Math.min(...samples);
  if (p >= 100) return Math.max(...samples);
  const sorted = [...samples].sort((a, b) => a - b);
  const rank = (p / 100) * (sorted.length - 1);
  const lo = Math.floor(rank);
  const hi = Math.ceil(rank);
  if (lo === hi) return sorted[lo];
  return sorted[lo] + (sorted[hi] - sorted[lo]) * (rank - lo);
}

/**
 * 滑动窗口统计（默认 1 秒桶）。
 * 维护两份：
 *   - 当前桶（仍在滚进来的）
 *   - 已结束的样本累积数组（用于百分位计算）
 * 桶切换由调用方通过 tick(nowMs) 触发，或 record 时内部检查时间。
 */
export class SlidingStats {
  /**
   * @param {number} windowSec 窗口大小（秒），统计最近 windowSec 秒
   */
  constructor(windowSec = 1) {
    this.windowMs = windowSec * 1000;
    this._samples = [];      // 所有样本（带 ts）
    this._errors = 0;
  }

  /** 记一条 latency（ms）。 */
  record(latencyMs, nowMs = Date.now()) {
    this._samples.push({ ts: nowMs, lat: latencyMs });
    this._gc(nowMs);
  }

  /** 记一条错误（不计 latency）。 */
  recordError(nowMs = Date.now()) {
    this._errors++;
    this._gc(nowMs);
  }

  _gc(nowMs) {
    const cutoff = nowMs - this.windowMs;
    while (this._samples.length > 0 && this._samples[0].ts < cutoff) {
      this._samples.shift();
    }
  }

  /** 返回当前窗口的统计摘要。 */
  summary(nowMs = Date.now()) {
    this._gc(nowMs);
    const lats = this._samples.map(s => s.lat);
    lats.sort((a, b) => a - b);
    return {
      count: lats.length,
      opsPerSec: lats.length / (this.windowMs / 1000),
      p50: percentile(lats, 50),
      p95: percentile(lats, 95),
      p99: percentile(lats, 99),
      p100: percentile(lats, 100),
      errors: this._errors,
    };
  }

  reset() { this._samples = []; this._errors = 0; }
}
```

- [ ] **Step 4: 运行测试，确认通过**

```bash
cd sdk/irsp-client/JS && node --test test/perf.test.js
```

Expected: PASS（4/4）

- [ ] **Step 5: 提交**

```bash
git add sdk/irsp-client/JS/src/perf.js sdk/irsp-client/JS/test/perf.test.js
git commit -m "feat(irsp-client): perf.js 滑动窗口统计与百分位"
```

---

## Task 5: perf.js — Pipeline 控制器（TDD）

**Files:**
- Modify: `sdk/irsp-client/JS/src/perf.js`
- Modify: `sdk/irsp-client/JS/test/perf.test.js`

### Steps

- [ ] **Step 1: 写失败测试**

追加到 `sdk/irsp-client/JS/test/perf.test.js`:

```js
import { Pipeline } from '../src/perf.js';

test('Pipeline maintains concurrency and stops after duration', async () => {
  const p = new Pipeline();
  let started = 0;
  let resolved = 0;
  const sendFn = () => {
    started++;
    return new Promise(resolve => {
      setTimeout(() => { resolved++; resolve('ok'); }, 5);
    });
  };

  const samples = [];
  await p.run({ sendFn, concurrency: 4, durationMs: 100, onSample: (s) => samples.push(s) });

  // 100ms / 5ms per op = 单 worker 20 次，并发 4 = ~80 次（宽松下界）
  assert.ok(started > 20, `expected >20 starts, got ${started}`);
  assert.equal(resolved, started);
  assert.ok(samples.length > 0);
});

test('Pipeline stop() aborts early', async () => {
  const p = new Pipeline();
  const sendFn = () => new Promise(r => setTimeout(() => r('ok'), 50));
  const promise = p.run({ sendFn, concurrency: 1, durationMs: 10000 });
  setTimeout(() => p.stop(), 80);
  await promise;
  // 应该早于 10s 返回
  assert.ok(true);
});

test('Pipeline records errors', async () => {
  const p = new Pipeline();
  let n = 0;
  const sendFn = () => {
    n++;
    return Promise.reject(new Error('fail'));
  };
  const samples = [];
  await p.run({ sendFn, concurrency: 1, durationMs: 50, onSample: (s) => samples.push(s) });
  assert.ok(n > 0);
  const totalErrors = samples.reduce((a, s) => a + s.errors, 0);
  assert.equal(totalErrors, n);
});
```

- [ ] **Step 2: 运行测试，确认失败**

```bash
cd sdk/irsp-client/JS && node --test test/perf.test.js
```

Expected: FAIL（`Pipeline is not a function`）

- [ ] **Step 3: 实现 Pipeline**

追加到 `sdk/irsp-client/JS/src/perf.js`:

```js
/**
 * Pipeline 控制器：在固定并发深度下持续发起请求，统计延迟与错误。
 *
 * 用法：
 *   const p = new Pipeline();
 *   await p.run({ sendFn, concurrency: 4, durationMs: 10000, onSample });
 *
 * sendFn: () => Promise<any>，每个 in-flight 请求调用一次
 * onSample: 可选，每秒回调一次 (SlidingStats.summary)
 */
export class Pipeline {
  constructor() {
    this._stopped = false;
  }

  /** 立即停止（已发出的请求等其完成）。 */
  stop() { this._stopped = true; }

  /**
   * @param {{sendFn:()=>Promise<any>, concurrency:number, durationMs:number, onSample?:(s:any)=>void}} opts
   * @returns {Promise<{total:number, errors:number}>}
   */
  async run({ sendFn, concurrency, durationMs, onSample }) {
    this._stopped = false;
    const stats = new SlidingStats(1);
    const start = Date.now();
    const deadline = start + durationMs;
    let total = 0;
    let errors = 0;

    // 每秒触发一次 onSample
    let lastSample = start;
    const maybeSample = (now) => {
      if (onSample && now - lastSample >= 1000) {
        onSample(stats.summary(now));
        lastSample = now;
      }
    };

    // 单 worker：持续发起请求直到 deadline 或 stop
    const worker = async () => {
      while (!this._stopped && Date.now() < deadline) {
        const t0 = Date.now();
        try {
          await sendFn();
          stats.record(Date.now() - t0);
        } catch (e) {
          stats.record(Date.now() - t0);
          stats.recordError();
          errors++;
        }
        total++;
        maybeSample(Date.now());
      }
    };

    const workers = [];
    for (let i = 0; i < concurrency; i++) workers.push(worker());
    await Promise.all(workers);

    if (onSample) onSample(stats.summary()); // 最后一次
    return { total, errors };
  }
}
```

- [ ] **Step 4: 运行测试，确认通过**

```bash
cd sdk/irsp-client/JS && node --test test/perf.test.js
```

Expected: PASS（7/7）

- [ ] **Step 5: 提交**

```bash
git add sdk/irsp-client/JS/src/perf.js sdk/irsp-client/JS/test/perf.test.js
git commit -m "feat(irsp-client): Pipeline 并发控制器"
```

---

## Task 6: demo-writeback 插件（构建 + 烟雾测试）

**Files:**
- Create: `core/tests/fixtures/demo-writeback/irplugin/` (复制 `sdk/plugin-sdk/example/irplugin/`)
- Create: `core/tests/fixtures/demo-writeback/src/demo_writeback.cpp`
- Create: `core/tests/fixtures/demo-writeback/CMakeLists.txt`
- Modify: `core/tests/CMakeLists.txt` (加 fixtures 子目录)

### Steps

- [ ] **Step 1: 复制 SDK 头**

```bash
mkdir -p core/tests/fixtures/demo-writeback/irplugin core/tests/fixtures/demo-writeback/src
cp sdk/plugin-sdk/example/irplugin/plugin.hpp core/tests/fixtures/demo-writeback/irplugin/
cp sdk/plugin-sdk/example/irplugin/plugin_abi.h core/tests/fixtures/demo-writeback/irplugin/
```

- [ ] **Step 2: 写 demo_writeback.cpp**

Create `core/tests/fixtures/demo-writeback/src/demo_writeback.cpp`:

```cpp
// 测试夹具插件：注册 "demo/" 前缀 ownership，SET 时原样 echo 回 TagEngine。
// 不连任何真实设备。供 sdk/irsp-client/JS/examples 写回测试用。
//
// 协议约定：
//   demo/__probe__   保留 topic，用于插件存在性检测（任意值都 echo）
//   demo/batch/<n>   用于批量写回
//   demo/<anything>  其它任意 topic 都 echo

#include <cstdint>
#include <new>
#include <string>
#include <string_view>

#include "irplugin/plugin.hpp"

namespace {

class DemoWritebackPlugin final : public irplugin::IPlugin {
  public:
    explicit DemoWritebackPlugin(const IrPluginHostApi *host) noexcept : host_(host) {}

    bool init() override {
        if (!host_.valid()) return false;
        // 声明：本插件负责 "demo/" 前缀下 Tag 的写回。
        host_.onWrite("demo/", [this](const IrPluginTagValue &t) { return onWrite(t); });
        return true;
    }

    bool start() override {
        host_.pushEvent("demo-writeback", "state", "demo-writeback started", IRPLUGIN_SEV_INFO);
        return true;
    }

    bool stop() override { return true; }

    bool destroy() override {
        delete this;
        return true;
    }

  private:
    bool onWrite(const IrPluginTagValue &t) {
        const std::string name(t.name.data, t.name.len);
        // 设备 echo：把值原样回推为 Tag（设备回读语义）
        switch (t.value.type) {
        case IRPLUGIN_TYPE_BOOL:
            host_.pushTag(name, t.value.as.boolean != 0);
            break;
        case IRPLUGIN_TYPE_INT32:
            host_.pushTag(name, static_cast<std::int32_t>(t.value.as.i32));
            break;
        case IRPLUGIN_TYPE_INT64:
            host_.pushTag(name, static_cast<std::int64_t>(t.value.as.i64));
            break;
        case IRPLUGIN_TYPE_FLOAT:
            host_.pushTag(name, t.value.as.f32);
            break;
        case IRPLUGIN_TYPE_DOUBLE:
            host_.pushTag(name, t.value.as.f64);
            break;
        case IRPLUGIN_TYPE_STRING:
            host_.pushTag(name, std::string_view(t.value.as.str.data, t.value.as.str.len));
            break;
        default:
            return false;
        }
        host_.pushEvent("demo-writeback", "writeback",
                        "echo " + name, IRPLUGIN_SEV_INFO);
        return true;
    }

    irplugin::Host host_;
};

} // namespace

IRPLUGIN_EXPORT IrPluginInfo getPluginInfo() {
    return IrPluginInfo{IRPLUGIN_ABI_VERSION, "demo-writeback",
                        "Demo Writeback Plugin (test fixture)", "1.0.0"};
}

IRPLUGIN_EXPORT int createPlugin(const IrPluginHostApi *host, const char * /*config_path*/,
                                 IrPluginInstance *out) {
    return irplugin::makeInstance(new (std::nothrow) DemoWritebackPlugin(host), out);
}
```

- [ ] **Step 3: 写 CMakeLists.txt**

Create `core/tests/fixtures/demo-writeback/CMakeLists.txt`:

```cmake
# demo-writeback 测试夹具：SET 写回 echo 插件。
# 输出 demo_writeback.dll/.so 到 ${CMAKE_BINARY_DIR}/plugins/，runtime 自动发现。

add_library(irplugin_demo INTERFACE)
target_include_directories(irplugin_demo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(irplugin_demo INTERFACE cxx_std_20)

add_library(demo_writeback MODULE src/demo_writeback.cpp)
target_link_libraries(demo_writeback PRIVATE irplugin_demo)
set_target_properties(demo_writeback PROPERTIES
    PREFIX ""                                    # 输出 demo_writeback.dll（无 lib 前缀）
    CXX_VISIBILITY_PRESET hidden)

# 把 .dll/.so 装到 build/plugins/，runtime 启动时自动加载
add_custom_command(TARGET demo_writeback POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:IndustrialRuntime>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:demo_writeback>"
            "$<TARGET_FILE_DIR:IndustrialRuntime>/plugins/$<TARGET_FILE_NAME:demo_writeback>")
```

- [ ] **Step 4: 加到 core/tests/CMakeLists.txt**

修改 `core/tests/CMakeLists.txt`，在末尾（或现有 add_subdirectory 附近）加：

```cmake
if(IR_BUILD_TESTS)
    add_subdirectory(fixtures/demo-writeback)
endif()
```

如果 `core/tests/CMakeLists.txt` 还没引用 fixtures 目录，加这行即可。

- [ ] **Step 5: 重新构建 runtime + 插件**

```bash
powershell -ExecutionPolicy Bypass -File tools/run-build.ps1
```

Expected: 构建成功，`cmake-build-release/plugins/demo_writeback.dll` 存在

- [ ] **Step 6: 启动 runtime 验证插件被加载**

```bash
./cmake-build-release/IndustrialRuntime.exe &
sleep 2
# 检查日志，应该看到：
#   插件目录 .../plugins，配置目录 .../config，已加载 1 个插件
#   demo-writeback: demo-writeback started
```

- [ ] **Step 7: 提交**

```bash
git add core/tests/fixtures/ core/tests/CMakeLists.txt
git commit -m "feat(core): demo-writeback 测试夹具插件（SET echo）"
```

---

## Task 7: 重构 index.html 为 tab 布局

**Files:**
- Modify: `sdk/irsp-client/JS/examples/index.html`（大改，替换原内容）

### Steps

- [ ] **Step 1: 备份原 monitor 逻辑**

原 154 行 index.html 的 `<script>` 块（第 81-152 行）的 monitor 逻辑下一 task 会搬到 `tabs/monitor.js`。先看一眼，心里有数。

- [ ] **Step 2: 重写 index.html 骨架**

替换 `sdk/irsp-client/JS/examples/index.html` 全部内容：

```html
<!doctype html>
<html lang="zh-CN" data-theme="business">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>IRSP Test Console</title>
  <script src="https://cdn.tailwindcss.com?plugins=forms,typography"></script>
  <link href="https://cdn.jsdelivr.net/npm/daisyui@5/dist/full.css" rel="stylesheet">
  <script src="https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js"></script>
</head>
<body class="min-h-screen bg-base-200">
  <header class="navbar bg-base-100 shadow sticky top-0 z-10">
    <div class="flex-1 items-center gap-3">
      <span id="dot" class="badge badge-xs"></span>
      <h1 class="text-base font-semibold">IRSP Test Console</h1>
      <span id="serverInfo" class="text-xs opacity-60">未连接</span>
    </div>
    <div class="flex-none gap-2">
      <input id="url" class="input input-sm input-bordered w-64" value="ws://127.0.0.1:9777" />
      <button id="connect" class="btn btn-sm btn-primary">连接</button>
      <button id="disconnect" class="btn btn-sm btn-ghost" disabled>断开</button>
    </div>
  </header>

  <div role="tablist" class="tabs tabs-bordered bg-base-100">
    <a role="tab" class="tab tab-active" data-tab="monitor">实时监控</a>
    <a role="tab" class="tab" data-tab="command">命令测试</a>
    <a role="tab" class="tab" data-tab="benchmark">性能压测</a>
    <a role="tab" class="tab" data-tab="writeback">写回测试</a>
  </div>

  <main class="p-4">
    <section id="tab-monitor" class="tab-panel"></section>
    <section id="tab-command" class="tab-panel hidden"></section>
    <section id="tab-benchmark" class="tab-panel hidden"></section>
    <section id="tab-writeback" class="tab-panel hidden"></section>
  </main>

  <script type="module">
    import { IrspClient } from '../src/index.js';
    import monitor from './tabs/monitor.js';
    import command from './tabs/command.js';
    import benchmark from './tabs/benchmark.js';
    import writeback from './tabs/writeback.js';

    const tabs = { monitor, command, benchmark, writeback };
    const $ = (id) => document.getElementById(id);
    const dot = $('dot'), serverInfo = $('serverInfo');

    let client = null;
    const shared = { getClient: () => client };

    // 初始化每个 tab
    Object.values(tabs).forEach(t => t.init(shared));

    function setConnected(on) {
      dot.classList.toggle('badge-primary', on);
      $('connect').disabled = on;
      $('disconnect').disabled = !on;
    }

    // tab 切换
    document.querySelectorAll('[role="tab"]').forEach(tab => {
      tab.onclick = () => {
        document.querySelectorAll('[role="tab"]').forEach(t => t.classList.remove('tab-active'));
        tab.classList.add('tab-active');
        const name = tab.dataset.tab;
        Object.entries(tabs).forEach(([k, t]) => {
          const panel = document.getElementById('tab-' + k);
          const visible = (k === name);
          panel.classList.toggle('hidden', !visible);
          if (visible) t.onShow?.();
          else t.onHide?.();
        });
      };
    });

    $('connect').onclick = async () => {
      const url = $('url').value.trim();
      client = new IrspClient(url);
      client.on('close', () => {
        setConnected(false);
        serverInfo.textContent = '已断开';
        Object.values(tabs).forEach(t => t.onDisconnect?.());
      });
      client.on('error', () => { serverInfo.textContent = '连接错误'; });
      try {
        await client.connect();
        setConnected(true);
        serverInfo.textContent = `${client.server.server} · ${client.server.encoding}`;
        Object.values(tabs).forEach(t => t.onConnect?.());
      } catch (err) {
        serverInfo.textContent = '连接失败: ' + err.message;
        setConnected(false);
      }
    };

    $('disconnect').onclick = async () => {
      try { await client?.bye(); } catch {}
      setConnected(false);
    };
  </script>
</body>
</html>
```

注意：此处 `<script>` 引用了 4 个还不存在的 `tabs/*.js` 文件。下一个 task 起会逐个创建，但先创建占位文件避免页面打开即崩。

- [ ] **Step 3: 创建 tab 占位文件**

为每个 tab 创建占位，让页面能跑起来：

`sdk/irsp-client/JS/examples/tabs/monitor.js`:
```js
export default {
  init(shared) {},
  onConnect() {},
  onDisconnect() {},
  onShow() {},
  onHide() {},
};
```

同样创建 `command.js` / `benchmark.js` / `writeback.js`，内容相同。

- [ ] **Step 4: 启动 runtime + serve.mjs 验证**

```bash
./cmake-build-release/IndustrialRuntime.exe &
cd sdk/irsp-client/JS && node examples/serve.mjs &
```

浏览器打开 `http://localhost:8080/examples/index.html`，应看到：
- 顶部连接栏
- 4 个 tab 可切换（点击切换有视觉反馈）
- 点连接按钮，server info 显示 `ir · irsp1`

- [ ] **Step 5: 提交**

```bash
git add sdk/irsp-client/JS/examples/index.html sdk/irsp-client/JS/examples/tabs/
git commit -m "feat(monitor): 重构为 tab 布局，引入 daisyUI + ECharts"
```

---

## Task 8: monitor.js tab（搬现有逻辑 + 前缀过滤）

**Files:**
- Modify: `sdk/irsp-client/JS/examples/tabs/monitor.js`

### Steps

- [ ] **Step 1: 实现 monitor.js**

替换 `sdk/irsp-client/JS/examples/tabs/monitor.js`：

```js
// Tab 1：实时监控。连接后 SCAN 预填 + SUBSCRIBE + SUBEVENT，渲染 Tags 表与 Events 流。

export default {
  init(shared) {
    this.shared = shared;
    this.tags = new Map(); // name -> {type, value, ts}
    this.client = null;

    // 构建面板 DOM
    const root = document.getElementById('tab-monitor');
    root.innerHTML = `
      <div class="flex items-center gap-2 mb-3">
        <span class="text-sm opacity-70">订阅模式:</span>
        <input id="mon-pattern" class="input input-sm input-bordered w-40" value="#" />
        <button id="mon-apply" class="btn btn-sm btn-ghost">应用</button>
        <span class="text-sm opacity-70 ml-4">过滤:</span>
        <input id="mon-filter" class="input input-sm input-bordered w-40" placeholder="如 demo/*" />
      </div>
      <div class="grid grid-cols-1 lg:grid-cols-[1.3fr_1fr] gap-4">
        <div class="card bg-base-100 shadow">
          <div class="card-body p-3">
            <h2 class="card-title text-xs uppercase opacity-60">Tags</h2>
            <div id="mon-tags" class="overflow-auto max-h-[60vh]">
              <div class="text-sm opacity-50 p-4">连接后显示实时 Tag…</div>
            </div>
          </div>
        </div>
        <div class="card bg-base-100 shadow">
          <div class="card-body p-3">
            <h2 class="card-title text-xs uppercase opacity-60">Events</h2>
            <div id="mon-log" class="overflow-auto max-h-[60vh]">
              <div class="text-sm opacity-50 p-4">连接后显示事件…</div>
            </div>
          </div>
        </div>
      </div>
    `;

    root.querySelector('#mon-apply').onclick = async () => {
      if (!this.client) return;
      const pattern = root.querySelector('#mon-pattern').value.trim() || '#';
      try {
        await this.client.subscribe(pattern);
        await this.client.subevent('info');
      } catch (e) { console.warn('subscribe failed', e); }
    };

    root.querySelector('#mon-filter').oninput = () => this._renderTags();
  },

  onConnect() {
    this.client = this.shared.getClient();
    this.client.on('tag', (t) => { this.tags.set(t.name, t); this._renderTags(); });
    this.client.on('event', (e) => this._addEvent(e));

    // 预填 + 订阅
    (async () => {
      try {
        const pattern = document.getElementById('mon-pattern').value.trim() || '#';
        const { names } = await this.client.scan('0', pattern.includes('#') || pattern.includes('+') ? pattern : '#');
        if (names.length) {
          const got = await this.client.mget(names);
          got.forEach((t) => t && this.tags.set(t.name, t));
          this._renderTags();
        }
        await this.client.subscribe(pattern);
        await this.client.subevent('info');
      } catch (e) { console.warn('monitor init failed', e); }
    })();
  },

  onDisconnect() {
    this.tags.clear();
    this._renderTags();
    document.getElementById('mon-log').innerHTML = '<div class="text-sm opacity-50 p-4">已断开</div>';
  },

  _renderTags() {
    const wrap = document.getElementById('mon-tags');
    if (this.tags.size === 0) {
      wrap.innerHTML = '<div class="text-sm opacity-50 p-4">暂无 Tag</div>';
      return;
    }
    const filterRaw = document.getElementById('mon-filter')?.value.trim() || '';
    let prefix = '', suffix = '';
    if (filterRaw.endsWith('*')) { prefix = filterRaw.slice(0, -1); }
    const fmtTime = (tsNs) => {
      try { return new Date(Number(tsNs / 1000000n)).toLocaleTimeString(); } catch { return ''; }
    };
    const fmtVal = (v) => (typeof v === 'bigint' ? v.toString() : String(v));

    const rows = [...this.tags.entries()].sort((a, b) => a[0].localeCompare(b[0]))
      .filter(([name]) => !prefix || name.startsWith(prefix))
      .map(([name, t]) => `<tr>
        <td class="py-1 px-2">${name}</td>
        <td class="py-1 px-2 opacity-60 text-xs">${t.type}</td>
        <td class="py-1 px-2 font-mono text-primary">${fmtVal(t.value)}</td>
        <td class="py-1 px-2 opacity-60 text-xs">${fmtTime(t.ts)}</td>
      </tr>`).join('');
    wrap.innerHTML = `<table class="table table-compact w-full">
      <thead><tr><th>Topic</th><th>类型</th><th>值</th><th>时间</th></tr></thead>
      <tbody>${rows}</tbody></table>`;
  },

  _addEvent(e) {
    const log = document.getElementById('mon-log');
    const empty = log.querySelector('.opacity-50');
    if (empty) log.innerHTML = '';
    const fmtTime = (tsNs) => {
      try { return new Date(Number(tsNs / 1000000n)).toLocaleTimeString(); } catch { return ''; }
    };
    const row = document.createElement('div');
    row.className = 'px-3 py-1 border-t border-base-200 text-sm';
    row.innerHTML = `<span class="badge badge-${e.severity === 'critical' ? 'error' : e.severity === 'warning' || e.severity === 'alarm' ? 'warning' : 'ghost'} badge-xs uppercase mr-2">${e.severity}</span>` +
      `<span class="mr-2">${e.category}: ${e.message}</span>` +
      `<span class="opacity-50 text-xs">${fmtTime(e.ts)}</span>`;
    log.prepend(row);
    while (log.childElementCount > 100) log.lastElementChild.remove();
  },
};
```

- [ ] **Step 2: 启动 runtime + serve.mjs 手测**

```bash
./cmake-build-release/IndustrialRuntime.exe &
cd sdk/irsp-client/JS && node examples/serve.mjs &
```

打开浏览器 → 点连接 → 切到"实时监控"tab，应看到 `system/heartbeat` 每秒新增一行 + Events 流刷心跳。

- [ ] **Step 3: 测试过滤**

在"过滤"输入框填 `system/*`，Tags 表只应显示 `system/heartbeat`。

- [ ] **Step 4: 提交**

```bash
git add sdk/irsp-client/JS/examples/tabs/monitor.js
git commit -m "feat(monitor): Tab1 实时监控（搬现有逻辑 + 前缀过滤）"
```

---

## Task 9: command.js tab

**Files:**
- Modify: `sdk/irsp-client/JS/examples/tabs/command.js`

### Steps

- [ ] **Step 1: 实现 command.js**

替换 `sdk/irsp-client/JS/examples/tabs/command.js`:

```js
// Tab 2：命令测试台。把 IRSP 所有命令做成按钮 + 参数输入，逐个验证。
// 右侧日志记录请求/响应（含耗时），错误红色高亮。

const TYPES = ['i64', 'i32', 'f64', 'bool', 'str'];

export default {
  init(shared) {
    this.shared = shared;
    this.client = null;
    this.logs = [];

    const root = document.getElementById('tab-command');
    root.innerHTML = `
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4 space-y-3">
            <h2 class="card-title text-xs uppercase opacity-60">连接命令</h2>
            <div class="flex gap-2 flex-wrap">
              <button class="btn btn-sm" data-cmd="HELLO">HELLO</button>
              <button class="btn btn-sm" data-cmd="PING">PING</button>
              <button class="btn btn-sm" data-cmd="BYE">BYE</button>
            </div>

            <h2 class="card-title text-xs uppercase opacity-60 mt-3">读命令</h2>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">GET</span>
              <input class="input input-sm input-bordered flex-1" data-arg="get_name" value="system/heartbeat" />
              <button class="btn btn-sm btn-primary" data-fn="get">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">MGET</span>
              <input class="input input-sm input-bordered flex-1" data-arg="mget_names" value="system/heartbeat,demo/foo" />
              <button class="btn btn-sm btn-primary" data-fn="mget">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">EXISTS</span>
              <input class="input input-sm input-bordered flex-1" data-arg="exists_name" value="system/heartbeat" />
              <button class="btn btn-sm btn-primary" data-fn="exists">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">SCAN</span>
              <input class="input input-sm input-bordered w-20" data-arg="scan_cursor" value="0" />
              <input class="input input-sm input-bordered flex-1" data-arg="scan_pattern" value="#" />
              <button class="btn btn-sm btn-primary" data-fn="scan">执行</button>
            </div>

            <h2 class="card-title text-xs uppercase opacity-60 mt-3">订阅命令</h2>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">WATCH</span>
              <input class="input input-sm input-bordered flex-1" data-arg="watch_names" value="demo/foo" />
              <button class="btn btn-sm" data-fn="watch">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">SUBSCRIBE</span>
              <input class="input input-sm input-bordered flex-1" data-arg="sub_patterns" value="demo/#" />
              <button class="btn btn-sm" data-fn="subscribe">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">SUBEVENT</span>
              <select class="select select-sm select-bordered" data-arg="subevent_sev">
                <option>info</option><option>warning</option><option>alarm</option><option>critical</option>
              </select>
              <button class="btn btn-sm" data-fn="subevent">执行</button>
            </div>
            <div class="flex gap-2 flex-wrap">
              <button class="btn btn-sm btn-ghost" data-fn="unwatch">UNWATCH</button>
              <button class="btn btn-sm btn-ghost" data-fn="unsubscribe">UNSUBSCRIBE</button>
              <button class="btn btn-sm btn-ghost" data-fn="unsubevent">UNSUBEVENT</button>
            </div>

            <h2 class="card-title text-xs uppercase opacity-60 mt-3">写命令</h2>
            <div class="flex gap-2 items-center">
              <span class="text-xs w-16">SET</span>
              <input class="input input-sm input-bordered flex-1" data-arg="set_name" value="demo/foo" />
              <select class="select select-sm select-bordered" data-arg="set_type">
                ${TYPES.map(t => `<option>${t}</option>`).join('')}
              </select>
              <input class="input input-sm input-bordered w-32" data-arg="set_value" value="42" />
              <button class="btn btn-sm btn-primary" data-fn="set">执行</button>
            </div>

            <h2 class="card-title text-xs uppercase opacity-60 mt-3">自由命令</h2>
            <textarea class="textarea textarea-bordered w-full text-sm font-mono" rows="3"
                      placeholder="每行一条，如 GET demo/foo&#10;SET demo/foo i64 42&#10;Ctrl+Enter 顺序执行"></textarea>
            <div class="text-xs opacity-60">Ctrl+Enter 执行</div>
          </div>
        </div>

        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <div class="flex items-center justify-between">
              <h2 class="card-title text-xs uppercase opacity-60">请求 / 响应日志</h2>
              <div class="flex gap-2">
                <button class="btn btn-xs btn-ghost" id="cmd-copy">复制</button>
                <button class="btn btn-xs btn-ghost" id="cmd-clear">清空</button>
              </div>
            </div>
            <div id="cmd-log" class="overflow-auto max-h-[70vh] font-mono text-xs space-y-1"></div>
          </div>
        </div>
      </div>
    `;

    // 命令映射
    const handlers = {
      HELLO: () => this.client._send(['HELLO', '1']),
      PING: () => this.client.ping(),
      BYE: () => this.client.bye(),
      get: () => this.client.get(this._arg('get_name')),
      mget: () => this.client.mget(this._arg('mget_names').split(',').map(s => s.trim()).filter(Boolean)),
      exists: () => this.client.exists(this._arg('exists_name')),
      scan: () => this.client.scan(this._arg('scan_cursor'), this._arg('scan_pattern')),
      watch: () => this.client.watch(...this._arg('watch_names').split(',').map(s => s.trim()).filter(Boolean)),
      subscribe: () => this.client.subscribe(this._arg('sub_patterns')),
      subevent: () => this.client.subevent(this._arg('subevent_sev')),
      unwatch: () => this.client.unwatch(this._arg('watch_names').split(',').map(s => s.trim()).filter(Boolean)),
      unsubscribe: () => this.client.unsubscribe(this._arg('sub_patterns')),
      unsubevent: () => this.client.unsubevent(),
      set: () => {
        const t = this._arg('set_type');
        const raw = this._arg('set_value');
        let v = raw;
        if (t === 'i64' || t === 'i32' || t === 'u64' || t === 'u32') v = BigInt(raw);
        else if (t === 'f64' || t === 'f32') v = Number(raw);
        else if (t === 'bool') v = raw === 'true' || raw === '1';
        return this.client.set(this._arg('set_name'), v, t);
      },
    };

    // 绑定按钮
    root.querySelectorAll('[data-cmd]').forEach(btn => {
      btn.onclick = () => this._run(btn.dataset.cmd, btn.dataset.cmd, handlers[btn.dataset.cmd]);
    });
    root.querySelectorAll('[data-fn]').forEach(btn => {
      btn.onclick = () => this._run(btn.dataset.fn, btn.dataset.fn, handlers[btn.dataset.fn]);
    });

    // 自由命令 textarea
    const ta = root.querySelector('textarea');
    ta.onkeydown = async (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
        const lines = ta.value.split('\n').map(s => s.trim()).filter(Boolean);
        for (const line of lines) {
          // inline 风格：空格分隔
          await this._runInline(line);
        }
      }
    };

    root.querySelector('#cmd-clear').onclick = () => {
      this.logs = [];
      this._renderLog();
    };
    root.querySelector('#cmd-copy').onclick = () => {
      navigator.clipboard.writeText(this.logs.join('\n'));
    };
  },

  onConnect() { this.client = this.shared.getClient(); },
  onDisconnect() { this.client = null; },

  _arg(name) {
    return document.getElementById('tab-command').querySelector(`[data-arg="${name}"]`).value;
  },

  async _run(label, requestDesc, fn) {
    if (!this.client) { alert('未连接'); return; }
    const t0 = performance.now();
    const entry = { ts: new Date().toLocaleTimeString(), request: requestDesc, response: null, ms: null, error: null };
    this.logs.push(entry);
    if (this.logs.length > 200) this.logs.shift();
    this._renderLog();
    try {
      const r = await fn();
      entry.ms = (performance.now() - t0).toFixed(2);
      entry.response = JSON.stringify(r, (k, v) => typeof v === 'bigint' ? v.toString() + 'n' : v, 2);
    } catch (e) {
      entry.ms = (performance.now() - t0).toFixed(2);
      entry.error = e.message || String(e);
    }
    this._renderLog();
  },

  async _runInline(line) {
    if (!this.client) return;
    const t0 = performance.now();
    const entry = { ts: new Date().toLocaleTimeString(), request: line, response: null, ms: null, error: null };
    this.logs.push(entry);
    this._renderLog();
    try {
      // 用 SDK 内部编码发送 inline（直接拼成字符串走 ws）
      const enc = new TextEncoder();
      // inline 模式：发文本帧（runtime 端按 inline 解析）
      this.client._pending.push({
        resolve: (r) => {
          entry.ms = (performance.now() - t0).toFixed(2);
          entry.response = JSON.stringify(r, (k, v) => typeof v === 'bigint' ? v.toString() + 'n' : v, 2);
          this._renderLog();
        },
        reject: (e) => {
          entry.ms = (performance.now() - t0).toFixed(2);
          entry.error = e.message || String(e);
          this._renderLog();
        },
      });
      this.client.ws.send(enc.encode(line + '\r\n'));
    } catch (e) {
      entry.error = e.message;
      this._renderLog();
    }
  },

  _renderLog() {
    const wrap = document.getElementById('cmd-log');
    wrap.innerHTML = this.logs.slice().reverse().map(e => `
      <div class="${e.error ? 'text-error' : ''} px-2 py-1 border-l-2 ${e.error ? 'border-error' : 'border-base-200'}">
        <div class="opacity-60 text-[10px]">${e.ts}</div>
        <div>→ ${escapeHtml(e.request)}</div>
        ${e.response ? `<div class="text-success">← ${escapeHtml(e.response)} <span class="opacity-50">(${e.ms}ms)</span></div>` : ''}
        ${e.error ? `<div class="text-error">✗ ${escapeHtml(e.error)} <span class="opacity-50">(${e.ms}ms)</span></div>` : ''}
      </div>
    `).join('') || '<div class="text-sm opacity-50">无日志</div>';
  },
};

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[c]);
}
```

- [ ] **Step 2: 启动 runtime + serve.mjs 手测**

```bash
./cmake-build-release/IndustrialRuntime.exe &
cd sdk/irsp-client/JS && node examples/serve.mjs &
```

打开浏览器 → 连接 → 切到"命令测试"tab：
- 点 PING → 日志显示 `← "pong" (0.x ms)`
- GET `system/heartbeat` → 返回当前值（i64）
- SET `demo/foo i64 42` → 返回 `ok`，切回 monitor tab 看 `demo/foo` 出现

- [ ] **Step 3: 提交**

```bash
git add sdk/irsp-client/JS/examples/tabs/command.js
git commit -m "feat(monitor): Tab2 命令测试台"
```

---

## Task 10: benchmark.js — 场景一（pipeline 请求-回复）

**Files:**
- Modify: `sdk/irsp-client/JS/examples/tabs/benchmark.js`

### Steps

- [ ] **Step 1: 实现场景一**

替换 `sdk/irsp-client/JS/examples/tabs/benchmark.js`:

```js
// Tab 3：性能压测。
// 场景一：pipeline 请求-回复吞吐/延迟（单连接，维持 N 个在途请求）
// 场景三：多连接并发（开 M 个 IrspClient，每连接 R ops/s）
// 场景二（推送扇出）延期到 Phase 2。

import { IrspClient } from '../src/index.js';
import { Pipeline } from '../../src/perf.js';

export default {
  init(shared) {
    this.shared = shared;
    this.chart = null;
    this.pipeline = null;
    this.scenario1Series = []; // {t, ops, p50, p95, p99}

    const root = document.getElementById('tab-benchmark');
    root.innerHTML = `
      <div class="space-y-4">
        <!-- 场景一 -->
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">场景一：请求-回复吞吐（pipeline）</h2>
            <div class="flex flex-wrap items-center gap-2 mt-2">
              <span class="text-xs">命令</span>
              <select id="bm1-cmd" class="select select-sm select-bordered">
                <option value="get">GET</option>
                <option value="mget">MGET</option>
                <option value="exists">EXISTS</option>
                <option value="ping">PING</option>
                <option value="set">SET (demo/*)</option>
              </select>
              <span class="text-xs">name</span>
              <input id="bm1-name" class="input input-sm input-bordered w-48" value="system/heartbeat" />
              <span class="text-xs">并发深度</span>
              <input id="bm1-conc" type="number" class="input input-sm input-bordered w-20" value="4" />
              <span class="text-xs">时长(秒)</span>
              <input id="bm1-dur" type="number" class="input input-sm input-bordered w-20" value="10" />
              <button id="bm1-start" class="btn btn-sm btn-primary">开始</button>
              <button id="bm1-stop" class="btn btn-sm btn-ghost" disabled>停止</button>
            </div>
            <div id="bm1-metrics" class="mt-3 font-mono text-sm opacity-70">未运行</div>
          </div>
        </div>

        <!-- 场景二（延期） -->
        <div class="card bg-base-100 shadow opacity-60">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">场景二：订阅推送扇出 <span class="badge badge-ghost ml-2">Phase 2</span></h2>
            <p class="text-xs opacity-70">延期实现：依赖 demo 插件 + 双连接（writer 灌 + subscriber 统计）</p>
          </div>
        </div>

        <!-- 场景三 -->
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">场景三：多连接并发</h2>
            <div class="flex flex-wrap items-center gap-2 mt-2">
              <span class="text-xs">连接数</span>
              <input id="bm3-conns" type="number" class="input input-sm input-bordered w-20" value="16" />
              <span class="text-xs">每连接 ops/s</span>
              <input id="bm3-rate" type="number" class="input input-sm input-bordered w-20" value="100" />
              <span class="text-xs">name</span>
              <input id="bm3-name" class="input input-sm input-bordered w-48" value="system/heartbeat" />
              <span class="text-xs">时长(秒)</span>
              <input id="bm3-dur" type="number" class="input input-sm input-bordered w-20" value="10" />
              <button id="bm3-start" class="btn btn-sm btn-primary">开始</button>
              <button id="bm3-stop" class="btn btn-sm btn-ghost" disabled>停止</button>
            </div>
            <div id="bm3-metrics" class="mt-3 font-mono text-sm opacity-70">未运行</div>
          </div>
        </div>

        <!-- 图表 -->
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">时序图</h2>
            <div id="bm-chart" style="width:100%;height:320px;"></div>
          </div>
        </div>
      </div>
    `;

    root.querySelector('#bm1-start').onclick = () => this._runScenario1();
    root.querySelector('#bm1-stop').onclick = () => this.pipeline?.stop();
  },

  onConnect() {},
  onDisconnect() {
    this.pipeline?.stop();
  },
  onHide() {
    this.pipeline?.stop();
  },

  _ensureChart() {
    if (this.chart) return this.chart;
    this.chart = echarts.init(document.getElementById('bm-chart'));
    this.chart.setOption({
      tooltip: { trigger: 'axis' },
      legend: { data: ['ops/s', 'p50', 'p95', 'p99'] },
      xAxis: { type: 'category', data: [] },
      yAxis: [
        { type: 'value', name: 'ops/s' },
        { type: 'value', name: 'latency(ms)' },
      ],
      series: [
        { name: 'ops/s', type: 'line', data: [] },
        { name: 'p50', type: 'line', yAxisIndex: 1, data: [] },
        { name: 'p95', type: 'line', yAxisIndex: 1, data: [] },
        { name: 'p99', type: 'line', yAxisIndex: 1, data: [] },
      ],
    });
    return this.chart;
  },

  _pushSample(t, s) {
    this.scenario1Series.push({ t, ops: Math.round(s.opsPerSec), p50: +s.p50.toFixed(3), p95: +s.p95.toFixed(3), p99: +s.p99.toFixed(3) });
    const c = this._ensureChart();
    c.setOption({
      xAxis: { data: this.scenario1Series.map(x => x.t) },
      series: [
        { data: this.scenario1Series.map(x => x.ops) },
        { data: this.scenario1Series.map(x => x.p50) },
        { data: this.scenario1Series.map(x => x.p95) },
        { data: this.scenario1Series.map(x => x.p99) },
      ],
    });
    document.getElementById('bm1-metrics').innerHTML =
      `ops/s: <b>${s.opsPerSec.toFixed(0)}</b> | ` +
      `p50: <b>${s.p50.toFixed(3)}ms</b> | ` +
      `p95: <b>${s.p95.toFixed(3)}ms</b> | ` +
      `p99: <b>${s.p99.toFixed(3)}ms</b> | ` +
      `count: ${s.count} | errors: ${s.errors}`;
  },

  async _runScenario1() {
    if (!this.shared.getClient()) { alert('请先在顶栏连接'); return; }
    const cmd = document.getElementById('bm1-cmd').value;
    const name = document.getElementById('bm1-name').value;
    const conc = parseInt(document.getElementById('bm1-conc').value, 10);
    const dur = parseInt(document.getElementById('bm1-dur').value, 10) * 1000;

    // 用独立连接避免污染 monitor
    const url = this.shared.getClient().url;
    const benchClient = new IrspClient(url);
    try { await benchClient.connect(); } catch (e) { alert('压测连接失败: ' + e.message); return; }

    this.scenario1Series = [];
    this.pipeline = new Pipeline();
    document.getElementById('bm1-start').disabled = true;
    document.getElementById('bm1-stop').disabled = false;

    const sendFns = {
      get: () => benchClient.get(name),
      mget: () => benchClient.mget([name]),
      exists: () => benchClient.exists(name),
      ping: () => benchClient.ping(),
      set: () => benchClient.set(name.startsWith('demo/') ? name : 'demo/bench/' + Math.random().toString(36).slice(2), Math.floor(Math.random() * 1000), 'i64'),
    };

    const startTime = Date.now();
    await this.pipeline.run({
      sendFn: sendFns[cmd] || sendFns.get,
      concurrency: conc,
      durationMs: dur,
      onSample: (s) => this._pushSample(((Date.now() - startTime) / 1000).toFixed(1) + 's', s),
    });

    document.getElementById('bm1-start').disabled = false;
    document.getElementById('bm1-stop').disabled = true;
    try { await benchClient.bye(); } catch {}
  },
};
```

- [ ] **Step 2: 手测场景一**

```bash
./cmake-build-release/IndustrialRuntime.exe &
cd sdk/irsp-client/JS && node examples/serve.mjs &
```

顶栏连接 → 切性能压测 tab → 场景一默认参数（GET system/heartbeat, 并发 4, 10s）→ 开始。预期：
- 指标区每秒刷新 ops/s + p50/p95/p99
- ECharts 曲线随时间延展
- 10 秒后"开始"按钮恢复

- [ ] **Step 3: 提交**

```bash
git add sdk/irsp-client/JS/examples/tabs/benchmark.js
git commit -m "feat(monitor): Tab3 场景一 pipeline 压测"
```

---

## Task 11: benchmark.js — 场景三（多连接并发）

**Files:**
- Modify: `sdk/irsp-client/JS/examples/tabs/benchmark.js`

### Steps

- [ ] **Step 1: 加场景三实现**

在 `_runScenario1` 方法后面追加（同一个 `export default` 对象内）：

```js
  async _runScenario3() {
    if (!this.shared.getClient()) { alert('请先在顶栏连接'); return; }
    const url = this.shared.getClient().url;
    const conns = parseInt(document.getElementById('bm3-conns').value, 10);
    const rate = parseInt(document.getElementById('bm3-rate').value, 10);
    const name = document.getElementById('bm3-name').value;
    const dur = parseInt(document.getElementById('bm3-dur').value, 10) * 1000;

    document.getElementById('bm3-start').disabled = true;
    document.getElementById('bm3-stop').disabled = false;

    // 开 N 个连接
    const clients = [];
    try {
      for (let i = 0; i < conns; i++) {
        const c = new IrspClient(url);
        await c.connect();
        clients.push(c);
      }
    } catch (e) {
      alert('打开连接失败: ' + e.message);
      for (const c of clients) try { await c.bye(); } catch {}
      document.getElementById('bm3-start').disabled = false;
      document.getElementById('bm3-stop').disabled = true;
      return;
    }

    this.scenario1Series = [];
    this._stopFlag = false;
    document.getElementById('bm3-stop').onclick = () => { this._stopFlag = true; };

    const intervalMs = 1000 / rate;
    const startMs = Date.now();
    const counts = new Array(clients.length).fill(0);
    const errors = new Array(clients.length).fill(0);
    const lats = [];

    const worker = async (c, idx) => {
      let nextAt = Date.now();
      while (!this._stopFlag && Date.now() - startMs < dur) {
        const now = Date.now();
        if (now < nextAt) {
          await new Promise(r => setTimeout(r, Math.min(10, nextAt - now)));
          continue;
        }
        nextAt += intervalMs;
        const t0 = performance.now();
        try {
          await c.get(name);
          lats.push(performance.now() - t0);
          counts[idx]++;
        } catch (e) {
          errors[idx]++;
        }
        // 每秒采样
        if (Math.floor((Date.now() - startMs) / 1000) > this.scenario1Series.length) {
          this._pushScenario3Sample(startMs, counts, errors, lats);
        }
      }
    };

    await Promise.all(clients.map((c, i) => worker(c, i)));
    this._pushScenario3Sample(startMs, counts, errors, lats);

    for (const c of clients) try { await c.bye(); } catch {}
    document.getElementById('bm3-start').disabled = false;
    document.getElementById('bm3-stop').disabled = true;
  },

  _pushScenario3Sample(startMs, counts, errors, lats) {
    const total = counts.reduce((a, b) => a + b, 0);
    const errs = errors.reduce((a, b) => a + b, 0);
    const elapsed = (Date.now() - startMs) / 1000;
    const opsPerSec = elapsed > 0 ? total / elapsed : 0;
    const sorted = [...lats].sort((a, b) => a - b);
    const p = (q) => sorted.length === 0 ? NaN : sorted[Math.min(sorted.length - 1, Math.floor(q / 100 * sorted.length))];
    const t = elapsed.toFixed(1) + 's';
    this.scenario1Series.push({ t, ops: Math.round(opsPerSec), p50: +p(50).toFixed(3), p95: +p(95).toFixed(3), p99: +p(99).toFixed(3) });
    const c = this._ensureChart();
    c.setOption({
      xAxis: { data: this.scenario1Series.map(x => x.t) },
      series: [
        { data: this.scenario1Series.map(x => x.ops) },
        { data: this.scenario1Series.map(x => x.p50) },
        { data: this.scenario1Series.map(x => x.p95) },
        { data: this.scenario1Series.map(x => x.p99) },
      ],
    });
    document.getElementById('bm3-metrics').innerHTML =
      `total ops/s: <b>${opsPerSec.toFixed(0)}</b> | ` +
      `avg latency: <b>${sorted.length > 0 ? (sorted.reduce((a,b)=>a+b,0)/sorted.length).toFixed(3) : 0}ms</b> | ` +
      `errors: ${errs} | total ops: ${total}`;
  },
```

并在 `init()` 里加上场景三按钮绑定：

找到 `root.querySelector('#bm1-stop').onclick = () => this.pipeline?.stop();` 这行后面追加：

```js
    root.querySelector('#bm3-start').onclick = () => this._runScenario3();
    root.querySelector('#bm3-stop').onclick = () => { this._stopFlag = true; };
```

注意：`onHide` 也要顺手停场景三，在 `onHide()` 加 `this._stopFlag = true;`：

```js
  onHide() {
    this.pipeline?.stop();
    this._stopFlag = true;
  },
```

- [ ] **Step 2: 手测场景三**

```bash
./cmake-build-release/IndustrialRuntime.exe &
cd sdk/irsp-client/JS && node examples/serve.mjs &
```

顶栏连接 → 场景三默认参数（16 连接, 100 ops/s, 10s）→ 开始。预期：
- 指标区每秒刷新
- ECharts 显示曲线
- 10 秒后停止
- 终态显示总 ops/s 与平均延迟

- [ ] **Step 3: 提交**

```bash
git add sdk/irsp-client/JS/examples/tabs/benchmark.js
git commit -m "feat(monitor): Tab3 场景三 多连接并发压测"
```

---

## Task 12: writeback.js tab

**Files:**
- Modify: `sdk/irsp-client/JS/examples/tabs/writeback.js`

### Steps

- [ ] **Step 1: 实现 writeback.js**

替换 `sdk/irsp-client/JS/examples/tabs/writeback.js`:

```js
// Tab 4：SET 写回测试。前置：demo-writeback 插件已加载。
// 流程：探针检测插件 → 单次写回可视化 → 批量写回 → 两层断言。

export default {
  init(shared) {
    this.shared = shared;
    this.client = null;
    this.pluginReady = false;
    this.chart = null;
    this.timelineSeries = [];

    const root = document.getElementById('tab-writeback');
    root.innerHTML = `
      <div class="space-y-4">
        <!-- 插件检测 -->
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">写回链路状态</h2>
            <div class="flex items-center gap-3 mt-2">
              <span>插件 demo-writeback：</span>
              <span id="wb-status" class="badge badge-ghost badge-sm">未检测</span>
              <button id="wb-probe" class="btn btn-sm btn-ghost">重新检测</button>
            </div>
            <p class="text-xs opacity-60 mt-1">探针：SET demo/__probe__=1，订阅 demo/__probe__，500ms 内收到推送视为已加载</p>
          </div>
        </div>

        <!-- 单次写回 -->
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">单次写回</h2>
            <div class="flex flex-wrap gap-2 items-center mt-2">
              <input id="wb-name" class="input input-sm input-bordered w-48" value="demo/foo" />
              <select id="wb-type" class="select select-sm select-bordered">
                <option>i64</option><option>f64</option><option>bool</option><option>str</option>
              </select>
              <input id="wb-value" class="input input-sm input-bordered w-32" value="42" />
              <button id="wb-send" class="btn btn-sm btn-primary" disabled>发送 SET</button>
            </div>
            <div id="wb-chart" class="mt-3" style="width:100%;height:200px;"></div>
            <div id="wb-result" class="font-mono text-sm mt-2 opacity-70">未运行</div>
          </div>
        </div>

        <!-- 批量写回 -->
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">批量写回</h2>
            <div class="flex flex-wrap gap-2 items-center mt-2">
              <span class="text-xs">前缀</span>
              <input id="wb-bp" class="input input-sm input-bordered w-40" value="demo/batch/" />
              <span class="text-xs">数量</span>
              <input id="wb-bn" type="number" class="input input-sm input-bordered w-20" value="100" />
              <span class="text-xs">起始值</span>
              <input id="wb-bv" type="number" class="input input-sm input-bordered w-20" value="0" />
              <button id="wb-bstart" class="btn btn-sm btn-primary" disabled>开始</button>
            </div>
            <progress id="wb-progress" class="progress progress-primary mt-2" value="0" max="100"></progress>
            <div id="wb-bmetrics" class="font-mono text-sm mt-2 opacity-70">未运行</div>
          </div>
        </div>

        <!-- 断言 -->
        <div class="card bg-base-100 shadow">
          <div class="card-body p-4">
            <h2 class="card-title text-sm">写回断言</h2>
            <p class="text-xs opacity-70">SET 之后 500ms 内该 topic 的 tag 推送值与 SET 值一致</p>
            <div class="flex gap-2 items-center mt-2">
              <span class="text-xs">topic</span>
              <input id="wb-aname" class="input input-sm input-bordered w-48" value="demo/assert" />
              <span class="text-xs">value</span>
              <input id="wb-aval" class="input input-sm input-bordered w-24" value="99" />
              <button id="wb-assert" class="btn btn-sm btn-primary" disabled>运行断言</button>
            </div>
            <div id="wb-assert-result" class="font-mono text-sm mt-2 opacity-70">未运行</div>
          </div>
        </div>
      </div>
    `;

    root.querySelector('#wb-probe').onclick = () => this._probe();
    root.querySelector('#wb-send').onclick = () => this._singleWrite();
    root.querySelector('#wb-bstart').onclick = () => this._batchWrite();
    root.querySelector('#wb-assert').onclick = () => this._assert();
  },

  onConnect() {
    this.client = this.shared.getClient();
    // 连接后自动探针
    this._probe();
  },
  onDisconnect() {
    this.client = null;
    this.pluginReady = false;
    this._setBtns(false);
    document.getElementById('wb-status').className = 'badge badge-ghost badge-sm';
    document.getElementById('wb-status').textContent = '未检测';
  },

  _setBtns(enabled) {
    document.getElementById('wb-send').disabled = !enabled;
    document.getElementById('wb-bstart').disabled = !enabled;
    document.getElementById('wb-assert').disabled = !enabled;
  },

  async _probe() {
    if (!this.client) return;
    const statusEl = document.getElementById('wb-status');
    statusEl.className = 'badge badge-warning badge-sm';
    statusEl.textContent = '检测中...';
    try {
      await this.client.subscribe('demo/__probe__');
      const probePromise = new Promise((resolve) => {
        const handler = (t) => {
          if (t.name === 'demo/__probe__') {
            this.client.off('tag', handler);
            resolve(t);
          }
        };
        this.client.on('tag', handler);
        setTimeout(() => { this.client.off('tag', handler); resolve(null); }, 500);
      });
      await this.client.set('demo/__probe__', 1, 'i64');
      const got = await probePromise;
      this.pluginReady = !!got;
      statusEl.className = `badge badge-sm ${got ? 'badge-success' : 'badge-error'}`;
      statusEl.textContent = got ? '✓ 已加载' : '✗ 未加载';
      this._setBtns(got);
    } catch (e) {
      statusEl.className = 'badge badge-error badge-sm';
      statusEl.textContent = '✗ 错误: ' + e.message;
      this.pluginReady = false;
    }
  },

  _ensureChart() {
    if (this.chart) return this.chart;
    this.chart = echarts.init(document.getElementById('wb-chart'));
    this.chart.setOption({
      tooltip: { trigger: 'axis' },
      xAxis: { type: 'category', data: ['SET 发出', '回复到达', '推送到达'] },
      yAxis: { type: 'value', name: 'ms' },
      series: [{ type: 'bar', data: [0, 0, 0], name: '相对时间' }],
    });
    return this.chart;
  },

  async _singleWrite() {
    if (!this.client || !this.pluginReady) return;
    const name = document.getElementById('wb-name').value;
    const type = document.getElementById('wb-type').value;
    const raw = document.getElementById('wb-value').value;
    let v = raw;
    if (type === 'i64' || type === 'i32') v = BigInt(raw);
    else if (type === 'f64' || type === 'f32') v = Number(raw);
    else if (type === 'bool') v = raw === 'true' || raw === '1';

    const t0 = performance.now();
    const events = [];
    const setSent = t0;
    let replyAt = null, pushAt = null;

    // 监听推送
    const pushPromise = new Promise((resolve) => {
      const handler = (t) => {
        if (t.name === name) {
          this.client.off('tag', handler);
          resolve(performance.now());
        }
      };
      this.client.on('tag', handler);
      setTimeout(() => { this.client.off('tag', handler); resolve(null); }, 2000);
    });

    try {
      const r = await this.client.set(name, v, type);
      replyAt = performance.now();
      events.push({ t: 0, label: 'SET 发出' });
      events.push({ t: replyAt - setSent, label: '回复到达' });
      pushAt = await pushPromise;
      if (pushAt != null) events.push({ t: pushAt - setSent, label: '推送到达' });

      const c = this._ensureChart();
      c.setOption({
        xAxis: { data: events.map(e => e.label) },
        series: [{ data: events.map(e => +e.t.toFixed(3)) }],
      });

      const totalMs = (pushAt || replyAt) - setSent;
      document.getElementById('wb-result').innerHTML =
        `SET ${name} = ${raw} → <b>${r}</b><br>` +
        `回复耗时: <b>${(replyAt - setSent).toFixed(3)}ms</b><br>` +
        `推送耗时: <b>${pushAt ? (pushAt - setSent).toFixed(3) + 'ms' : '超时'}</b><br>` +
        `总往返: <b>${totalMs.toFixed(3)}ms</b>`;
    } catch (e) {
      document.getElementById('wb-result').innerHTML = `<span class="text-error">错误: ${e.message}</span>`;
    }
  },

  async _batchWrite() {
    if (!this.client || !this.pluginReady) return;
    const prefix = document.getElementById('wb-bp').value;
    const n = parseInt(document.getElementById('wb-bn').value, 10);
    const startV = parseInt(document.getElementById('wb-bv').value, 10);
    const prog = document.getElementById('wb-progress');
    const metrics = document.getElementById('wb-bmetrics');
    const lats = [];

    const t0 = performance.now();
    for (let i = 0; i < n; i++) {
      const ts = performance.now();
      try {
        await this.client.set(prefix + i, startV + i, 'i64');
        lats.push(performance.now() - ts);
      } catch (e) { /* ignore */ }
      prog.value = ((i + 1) / n) * 100;
      if (i % 10 === 0) {
        metrics.innerHTML = `进度 ${i + 1}/${n} | 平均 ${(lats.reduce((a, b) => a + b, 0) / lats.length).toFixed(3)}ms`;
      }
    }
    const elapsed = performance.now() - t0;
    lats.sort((a, b) => a - b);
    metrics.innerHTML =
      `完成 ${n} 条 | 总耗时 ${(elapsed / 1000).toFixed(2)}s | ops/s <b>${(n / (elapsed / 1000)).toFixed(0)}</b><br>` +
      `latency p50: ${lats[Math.floor(lats.length * 0.5)]?.toFixed(3)}ms | ` +
      `p95: ${lats[Math.floor(lats.length * 0.95)]?.toFixed(3)}ms | ` +
      `max: ${lats[lats.length - 1]?.toFixed(3)}ms`;
  },

  async _assert() {
    if (!this.client || !this.pluginReady) return;
    const name = document.getElementById('wb-aname').value;
    const raw = document.getElementById('wb-aval').value;
    const expected = BigInt(raw);
    const resultEl = document.getElementById('wb-assert-result');

    // L1 + L2
    const pushPromise = new Promise((resolve) => {
      const handler = (t) => {
        if (t.name === name) {
          this.client.off('tag', handler);
          resolve(t);
        }
      };
      this.client.on('tag', handler);
      setTimeout(() => { this.client.off('tag', handler); resolve(null); }, 1000);
    });

    try {
      // L1：同步受理
      const reply = await this.client.set(name, expected, 'i64');
      // L2：echo 回环
      const tag = await pushPromise;
      const l1Pass = (reply === 'ok' || reply === 'accepted');
      const l2Pass = !!tag && BigInt(tag.value) === expected;

      const lines = [
        `L1 同步受理: <b class="${l1Pass ? 'text-success' : 'text-error'}">${l1Pass ? 'PASS' : 'FAIL'}</b> (reply: ${reply})`,
        `L2 echo 回环: <b class="${l2Pass ? 'text-success' : 'text-error'}">${l2Pass ? 'PASS' : 'FAIL'}</b> ${tag ? `(got ${tag.value})` : '(无推送)'}`,
      ];
      resultEl.innerHTML = lines.join('<br>');
    } catch (e) {
      resultEl.innerHTML = `<span class="text-error">错误: ${e.message}</span>`;
    }
  },
};
```

- [ ] **Step 2: 手测**

```bash
./cmake-build-release/IndustrialRuntime.exe &
cd sdk/irsp-client/JS && node examples/serve.mjs &
```

顶栏连接 → 切"写回测试"tab：
- 探针自动跑，显示 `✓ 已加载`
- 单次写回：默认 `demo/foo = 42` → ECharts 显示三事件，结果区显示耗时
- 批量：默认 `demo/batch/` 100 条 → 进度条 + 完成后显示 ops/s
- 断言：`demo/assert = 99` → L1 PASS + L2 PASS

- [ ] **Step 3: 提交**

```bash
git add sdk/irsp-client/JS/examples/tabs/writeback.js
git commit -m "feat(monitor): Tab4 SET 写回测试（探针/单次/批量/断言）"
```

---

## Task 13: 端到端验收

**Files:** 无代码改动，跑完整验收清单。

### Steps

- [ ] **Step 1: 重建 + 重启**

```bash
powershell -ExecutionPolicy Bypass -File tools/run-build.ps1
./cmake-build-release/IndustrialRuntime.exe &
cd sdk/irsp-client/JS && node examples/serve.mjs &
```

打开 `http://localhost:8080/examples/index.html`。

- [ ] **Step 2: 跑验收清单**

按 spec §12 逐条手测：

1. ✓ 顶栏点连接，server info 显示 `ir · irsp1`
2. ✓ Tab1 看到 `system/heartbeat` 每秒更新
3. ✓ Tab2 点 PING → 日志显示 `pong` + 耗时
4. ✓ Tab2 GET `system/heartbeat` → 返回当前 i64 值
5. ✓ Tab2 SET `demo/foo i64 42` → 返回 `ok`
6. ✓ Tab3 场景一跑 10s GET `system/heartbeat` 并发 4 → 出 ops/s + 延迟百分位 + ECharts 曲线
7. ✓ Tab3 场景三开 16 连接各 100 ops/s → 总 ops/s 数字合理（应 > 1000）
8. ✓ Tab4 探针检测 → `✓ 已加载`
9. ✓ Tab4 单次写回 `demo/foo=42` → ECharts 时间轴显示三事件
10. ✓ Tab4 断言 → L1 PASS + L2 PASS
11. ✓ Ctrl+C runtime → 页面显示断开，按钮禁用；重启 runtime 后可重连

- [ ] **Step 3: 跑所有 JS 单测**

```bash
cd sdk/irsp-client/JS && node --test
```

Expected: 所有测试 PASS（含新增 encode-value / set / perf 各套）

- [ ] **Step 4: 最终提交（如果有 lint/格式化调整）**

```bash
# 如果 tools/lint.ps1 存在并适合 JS 也跑一下，否则跳过
git status
# 若有未提交修改：
git add -A
git commit -m "chore: 端到端验收后的微调"
```

- [ ] **Step 5: PR 描述（可选，准备合并到 main 时）**

```bash
gh pr create --title "feat(monitor): 4 tab 测试台（监控/命令/压测/写回）" --body "$(cat <<'EOF'
## Summary
- 重构 sdk/irsp-client/JS/examples/index.html 为 4 tab 布局，daisyUI + ECharts
- Tab1 实时监控：搬现有逻辑，加前缀过滤
- Tab2 命令测试台：IRSP 所有命令做成按钮 + 参数输入，请求/响应日志带耗时
- Tab3 性能压测：场景一 pipeline 吞吐/延迟，场景三 多连接并发；场景二（推送扇出）延期
- Tab4 SET 写回测试：探针检测 + 单次可视化（ECharts 时间轴）+ 批量 + 两层断言
- SDK 扩展：client.set() / encodeValue() / inferType() / perf.js (SlidingStats + Pipeline)
- 测试夹具：core/tests/fixtures/demo-writeback/（SET echo 插件）

## Test plan
- [x] JS 单测：encode-value / set / perf
- [x] 手工验收清单（见 spec §12）
- [ ] CI（如启用）

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## 自审

**Spec 覆盖**：
- §2-§4：Task 7 + Task 8 ✓
- §5 命令测试台：Task 9 ✓
- §6 场景一/三：Task 10 + Task 11 ✓
- §6 场景二：延期（已在 Task 10 UI 中标记 Phase 2）✓
- §7 写回测试：Task 12 ✓
- §8 SDK 扩展：Task 1 + Task 2 + Task 3 ✓
- §8 perf.js：Task 4 + Task 5 ✓
- §9 demo 插件：Task 6 ✓
- §11 SDK 单测：嵌入 Task 1-5 TDD ✓
- §12 验收清单：Task 13 ✓

**Placeholder 扫描**：无 TBD/TODO；所有代码块完整；所有命令含 expected 输出。

**类型一致性**：
- `SlidingStats` / `Pipeline` 在 perf.js 定义，benchmark.js / writeback.js 引用一致
- `client.set(name, value, type?)` 签名贯穿 Task 3 → Task 9/12
- `inferType` / `encodeValue` 命名贯穿 Task 1/2 → Task 3

**风险点**：
- Task 6 的 CMake `add_custom_command` 复制 dll 到 `IndustrialRuntime` 输出目录，依赖 `IndustrialRuntime` target 已定义（在根 CMakeLists）。如果构建顺序出问题，fallback 用 `CMAKE_RUNTIME_OUTPUT_DIRECTORY`。
- Task 10 引用 `'../../src/perf.js'`（相对 tabs/ 目录），路径正确性需在浏览器实跑验证。
- Task 12 的 `_runScenario3` 用 `this._stopFlag` 控制停止，切 tab 时需要 `onHide` 触发，已在 Task 11 Step 1 添加。
