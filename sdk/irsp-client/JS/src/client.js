// IRSP WebSocket 客户端（浏览器 / Node 22+，零运行时依赖，使用全局 WebSocket）。
//
// 语义：请求-回复在连接上按 FIFO 顺序对应（RESP 风格，无请求 id）；
// 服务端主动推送的帧带 `push` 字段（"tag" / "event"），据此与回复区分。

import { encodeRequest, decode, decodeValue, asStr, IrspError, encodeValue, inferType } from './irsp1.js';

/** @typedef {{name:string,type:string,ts:bigint,value:any,quality?:string}} TagValue */
/** @typedef {{source:string,category:string,severity:string,ts:bigint,message:string}} IrspEvent */

/** 极简事件发射器。事件：'tag'、'event'、'close'、'error'。 */
class Emitter {
  constructor() { this._h = new Map(); }
  on(name, fn) { (this._h.get(name) ?? this._h.set(name, new Set()).get(name)).add(fn); return this; }
  off(name, fn) { this._h.get(name)?.delete(fn); return this; }
  emit(name, ...args) { for (const fn of this._h.get(name) ?? []) fn(...args); }
}

function isPushFrame(v) {
  return v != null && typeof v === 'object' && !Array.isArray(v) &&
    !(v instanceof Uint8Array) && !(v instanceof IrspError) && 'push' in v;
}

export class IrspClient extends Emitter {
  /** @param {string} url 形如 ws://127.0.0.1:9777 */
  constructor(url) {
    super();
    this.url = url;
    this.ws = null;
    this.server = null;        // HELLO 返回的服务端能力（字段已解码为字符串）
    this._pending = [];        // 等待回复的 {resolve,reject} 队列（FIFO）
    this._closing = false;     // 主动关闭标记，用于抑制关闭时的假 error
  }

  /** 建立连接并完成 HELLO 握手。 */
  connect() {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(this.url, 'irsp');
      ws.binaryType = 'arraybuffer';
      this.ws = ws;

      ws.onmessage = (ev) => {
        const bytes = ev.data instanceof ArrayBuffer ? new Uint8Array(ev.data)
          : ev.data instanceof Uint8Array ? ev.data : new Uint8Array(ev.data);
        this._onFrame(bytes);
      };
      ws.onerror = () => { if (!this._closing) this.emit('error', new Error('WebSocket 错误')); };
      ws.onclose = () => {
        const err = new Error('连接已关闭');
        while (this._pending.length) this._pending.shift().reject(err);
        this.emit('close');
      };
      ws.onopen = async () => {
        try {
          const hello = await this._send(['HELLO', '1']);
          this.server = {};
          for (const k of Object.keys(hello)) this.server[k] = asStr(hello[k]);
          resolve(this);
        } catch (e) {
          reject(e);
        }
      };
    });
  }

  _onFrame(bytes) {
    let value;
    try {
      value = decode(bytes);
    } catch (e) {
      this.emit('error', e);
      return;
    }
    if (isPushFrame(value)) {
      const kind = asStr(value.push);
      if (kind === 'tag') this.emit('tag', this._decodeTag(value));
      else if (kind === 'event') this.emit('event', this._decodeEvent(value));
      return;
    }
    const p = this._pending.shift();
    if (!p) return; // 无对应请求，忽略
    if (value instanceof IrspError) p.reject(value);
    else p.resolve(value);
  }

  /** @param {Array<string|Uint8Array>} parts */
  _send(parts) {
    return new Promise((resolve, reject) => {
      if (!this.ws || this.ws.readyState !== 1 /* OPEN */) {
        reject(new Error('连接未就绪'));
        return;
      }
      this._pending.push({ resolve, reject });
      this.ws.send(encodeRequest(parts));
    });
  }

  _decodeTag(m) {
    const type = asStr(m.type);
    /** @type {TagValue} */
    const tag = { name: asStr(m.name), type, ts: m.ts, value: decodeValue(type, m.value) };
    if (m.quality != null) tag.quality = asStr(m.quality);
    return tag;
  }

  _decodeEvent(m) {
    return {
      source: asStr(m.source),
      category: asStr(m.category),
      severity: asStr(m.severity),
      ts: m.ts,
      message: asStr(m.message),
    };
  }

  // ---- Tag 读取 ----

  /** @returns {Promise<TagValue|null>} */
  async get(name) {
    const r = await this._send(['GET', name]);
    return r === null ? null : this._decodeTag(r);
  }

  /** @param {string[]} names @returns {Promise<Array<TagValue|null>>} */
  async mget(names) {
    const r = await this._send(['MGET', ...names]);
    return r.map((x) => (x === null ? null : this._decodeTag(x)));
  }

  async exists(name) {
    return Number(await this._send(['EXISTS', name])) !== 0;
  }

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

  /** @returns {Promise<{nextCursor:string,names:string[]}>} */
  async scan(cursor = '0', pattern = '#', count = 0) {
    const parts = ['SCAN', String(cursor), pattern];
    if (count > 0) parts.push('COUNT', String(count));
    const r = await this._send(parts);
    return { nextCursor: asStr(r[0]), names: r[1].map(asStr) };
  }

  // ---- 订阅 ----

  async watch(...names) { return Number(await this._send(['WATCH', ...names])); }
  async unwatch(...names) { return Number(await this._send(['UNWATCH', ...names])); }
  async subscribe(...patterns) { return Number(await this._send(['SUBSCRIBE', ...patterns])); }
  async unsubscribe(...patterns) { return Number(await this._send(['UNSUBSCRIBE', ...patterns])); }

  /** @param {string} [minSeverity] info|warning|alarm|critical @param {string} [category] */
  async subevent(minSeverity, category) {
    const parts = ['SUBEVENT'];
    if (minSeverity) parts.push(minSeverity);
    if (category) parts.push(category);
    return Number(await this._send(parts));
  }
  async unsubevent() { return Number(await this._send(['UNSUBEVENT'])); }

  // ---- 连接管理 ----

  async ping(payload) {
    const r = await this._send(payload ? ['PING', payload] : ['PING']);
    return asStr(r);
  }

  async bye() {
    try { await this._send(['BYE']); } catch { /* 关闭途中忽略 */ }
    this.close();
  }

  close() { this._closing = true; this.ws?.close(); }
}
