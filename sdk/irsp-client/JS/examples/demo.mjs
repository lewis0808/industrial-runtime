// IRSP 客户端演示。先运行 IndustrialRuntime.exe（默认监听 9777，每秒推送 system/heartbeat），
// 再执行： node sdk/irsp-client/examples/demo.mjs

import { IrspClient } from '../src/index.js';

const url = process.env.IRSP_URL ?? 'ws://127.0.0.1:9777';
const client = new IrspClient(url);

client.on('tag', (t) => {
  console.log(`[tag] ${t.name} (${t.type}) = ${t.value}  @${t.ts}`);
});
client.on('event', (e) => {
  console.log(`[event] [${e.severity}] ${e.category}: ${e.message}`);
});
client.on('error', (e) => console.error('[error]', e.message));

await client.connect();
console.log('已连接，服务端能力:', client.server);

// 读取一次心跳（若运行不足 1 秒可能尚不存在）。
const hb = await client.get('system/heartbeat');
console.log('GET system/heartbeat =>', hb);

console.log('exists =>', await client.exists('system/heartbeat'));
console.log('scan   =>', await client.scan('0', '#'));

// 订阅整棵 system 子树 + Info 级以上事件，观察 5 秒推送。
console.log('subscribe system/# =>', await client.subscribe('system/#'));
console.log('subevent info =>', await client.subevent('info'));

await new Promise((r) => setTimeout(r, 5000));

await client.bye();
console.log('已断开。');
