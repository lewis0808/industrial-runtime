# @industrial-runtime/irp-client (JavaScript)

IRP（Industrial Runtime Protocol）的 JavaScript 客户端 SDK，用于浏览器与 Node（≥22）。
**零运行时依赖**：使用平台原生 `WebSocket`。协议规格见仓库 `irp/`。

位置：`sdk/irp-client/JS/`。

## 怎么用

### A. Node 项目里按包名导入

先安装（本地路径 / link / 发布任选其一），再按包名导：

```bash
# 本地路径安装（monorepo 未发布时最常用）
npm install ./sdk/irp-client/JS
# 或开发期软链： cd sdk/irp-client/JS && npm link  ；消费项目 npm link @industrial-runtime/irp-client
# 或发布后：    npm publish  ；消费方 npm install @industrial-runtime/irp-client
```

```js
import { IrpClient } from '@industrial-runtime/irp-client';

const client = new IrpClient('ws://127.0.0.1:9777');
client.on('tag', (t) => console.log(`${t.name} = ${t.value}`));
client.on('event', (e) => console.log(`[${e.severity}] ${e.message}`));

await client.connect();                 // 自动完成 HELLO 握手
console.log(client.server);             // { server, version, encoding, transports, capabilities }

const tag = await client.get('plant/line1/temp');   // { name, type, ts, value } 或 null
await client.subscribe('plant/#');      // 子树订阅，变化经 'tag' 事件推送
await client.subevent('warning');       // 订阅 warning 级以上事件
// ...
await client.bye();
```

> 注：JS 生态里"按包名导入"必须先让 Node 在 `node_modules` 解析到它（即上面的安装步骤）。
> 仓库内若想零安装直接引用，可用相对路径：`import { IrpClient } from '<相对路径>/JS/src/index.js'`。

### B. 浏览器里实时查看（HTML demo）

1. 启动服务端：运行 `IndustrialRuntime`（默认监听 9777，每秒推送 `system/heartbeat`）。
2. 起静态服务器（零依赖）：
   ```bash
   cd sdk/irp-client/JS
   node examples/serve.mjs        # 或 npm run serve
   ```
3. 浏览器打开 `http://localhost:8080/examples/index.html`，点“连接” →
   实时看到 Tag 表与事件流。

> 浏览器原生 ESM 不认裸包名，HTML 用相对路径 `import ... from '../src/index.js'`；
> 经 http 加载（serve.mjs）而非 `file://`，以满足模块脚本的同源要求。
> 也可用打包器（Vite/webpack）后按包名导入。

### C. Node 命令行示例

```bash
cd sdk/irp-client/JS
node examples/demo.mjs        # 或 npm run demo
```

## API

| 方法 | 说明 | 返回 |
|------|------|------|
| `connect()` | 建立连接并 HELLO 握手 | `Promise<this>` |
| `get(name)` | 读取单个 Tag | `TagValue \| null` |
| `mget(names)` | 批量读取 | `Array<TagValue \| null>` |
| `exists(name)` | 是否存在 | `boolean` |
| `scan(cursor?, pattern?, count?)` | 游标遍历 Tag 名 | `{ nextCursor, names }` |
| `watch(...names)` | 关注具体 Tag（精确） | `number` |
| `subscribe(...patterns)` | 订阅子树（`+`/`#` 通配） | `number` |
| `unwatch(...)` / `unsubscribe(...)` | 取消 | `number` |
| `subevent(minSeverity?, category?)` | 订阅事件 | `number` |
| `unsubevent()` | 取消事件订阅 | `number` |
| `ping(payload?)` | 保活 | `string` |
| `bye()` / `close()` | 关闭连接 | — |

事件（`client.on(name, fn)`）：`'tag'`（TagValue）、`'event'`（IrpEvent）、`'close'`、`'error'`。

### 类型

```ts
TagValue  = { name: string, type: string, ts: bigint, value: any, quality?: string }
IrpEvent  = { source: string, category: string, severity: string, ts: bigint, message: string }
```

- `type` 为类型标签：`bool/i8../u64/f32/f64/str/null`（见 `irp/protocol/datatype.md`）。
- `value` 已按 `type` 解码；`i64/u64` 与 `ts`（纳秒）为 `bigint`，避免精度丢失。

## 设计要点

- 请求/回复在连接上按 **FIFO 顺序**对应（RESP 风格，无请求 id）；服务端主动推送的帧带
  `push` 字段（`tag`/`event`），SDK 据此与回复区分。
- 编码 resp1（`src/resp1.js`），数值以小端二进制 bulk 承载。
- 仅 V1：`SET`/Stream 未提供（协议预留）。

## 测试

```bash
node --test         # resp1 编解码单测（无需服务端）
```
