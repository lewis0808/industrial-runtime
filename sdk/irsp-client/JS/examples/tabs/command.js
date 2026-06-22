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
      <div class="grid grid-cols-1 lg:grid-cols-2 gap-3">
        <div class="surface" style="padding: 14px;">
          <div class="h-cmd-group">连接命令</div>
          <div class="flex gap-2 flex-wrap mb-3">
            <button class="btn btn-ghost btn-sm" data-cmd="HELLO">HELLO</button>
            <button class="btn btn-ghost btn-sm" data-cmd="PING">PING</button>
            <button class="btn btn-ghost btn-sm" data-cmd="BYE">BYE</button>
          </div>

          <div class="h-cmd-group">读命令</div>
          <div class="space-y-2 mb-3">
            <div class="flex gap-2 items-center">
              <span class="text-xs text-muted mono w-14">GET</span>
              <input class="input input-sm mono flex-1" data-arg="get_name" value="system/heartbeat" />
              <button class="btn btn-primary btn-sm" data-fn="get">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs text-muted mono w-14">MGET</span>
              <input class="input input-sm mono flex-1" data-arg="mget_names" value="system/heartbeat,demo/foo" />
              <button class="btn btn-primary btn-sm" data-fn="mget">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs text-muted mono w-14">EXISTS</span>
              <input class="input input-sm mono flex-1" data-arg="exists_name" value="system/heartbeat" />
              <button class="btn btn-primary btn-sm" data-fn="exists">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs text-muted mono w-14">SCAN</span>
              <input class="input input-sm mono w-20" data-arg="scan_cursor" value="0" />
              <input class="input input-sm mono flex-1" data-arg="scan_pattern" value="#" />
              <button class="btn btn-primary btn-sm" data-fn="scan">执行</button>
            </div>
          </div>

          <div class="h-cmd-group">订阅命令</div>
          <div class="space-y-2 mb-3">
            <div class="flex gap-2 items-center">
              <span class="text-xs text-muted mono w-14">WATCH</span>
              <input class="input input-sm mono flex-1" data-arg="watch_names" value="demo/foo" />
              <button class="btn btn-ghost btn-sm" data-fn="watch">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs text-muted mono w-14">SUB</span>
              <input class="input input-sm mono flex-1" data-arg="sub_patterns" value="demo/#" />
              <button class="btn btn-ghost btn-sm" data-fn="subscribe">执行</button>
            </div>
            <div class="flex gap-2 items-center">
              <span class="text-xs text-muted mono w-14">SUBEVENT</span>
              <select class="select select-sm mono" data-arg="subevent_sev">
                <option>info</option><option>warning</option><option>alarm</option><option>critical</option>
              </select>
              <button class="btn btn-ghost btn-sm" data-fn="subevent">执行</button>
            </div>
            <div class="flex gap-2 flex-wrap mt-2">
              <button class="btn btn-ghost btn-xs" data-fn="unwatch">UNWATCH</button>
              <button class="btn btn-ghost btn-xs" data-fn="unsubscribe">UNSUBSCRIBE</button>
              <button class="btn btn-ghost btn-xs" data-fn="unsubevent">UNSUBEVENT</button>
            </div>
          </div>

          <div class="h-cmd-group">写命令</div>
          <div class="flex gap-2 items-center">
            <span class="text-xs text-muted mono w-14">SET</span>
            <input class="input input-sm mono flex-1" data-arg="set_name" value="demo/foo" />
            <select class="select select-sm mono" data-arg="set_type">
              ${TYPES.map(t => `<option>${t}</option>`).join('')}
            </select>
            <input class="input input-sm mono w-28" data-arg="set_value" value="42" />
            <button class="btn btn-primary btn-sm" data-fn="set">执行</button>
          </div>

          <div class="h-cmd-group">自由命令（inline）</div>
          <textarea class="textarea mono w-full" style="background:var(--bg-base);border:1px solid var(--border);font-size:12px;" rows="3"
                    placeholder="每行一条，如 GET demo/foo&#10;SET 走按钮模式（inline SET 不支持二进制编码）&#10;Ctrl+Enter 顺序执行"></textarea>
          <div class="text-xs text-dim mono mt-1">Ctrl+Enter 顺序执行 · inline SET 不支持（需二进制编码）</div>
        </div>

        <div class="surface" style="padding: 14px; display:flex;flex-direction:column;max-height:85vh;">
          <div class="flex items-center justify-between mb-2">
            <div class="h-cmd-group" style="margin:0;border:0;padding:0;">请求 / 响应日志</div>
            <div class="flex gap-1">
              <button class="btn btn-ghost btn-xs" id="cmd-copy">复制</button>
              <button class="btn btn-ghost btn-xs" id="cmd-clear">清空</button>
            </div>
          </div>
          <div id="cmd-log" class="overflow-auto" style="flex:1;"></div>
        </div>
      </div>
    `;

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

    root.querySelectorAll('[data-cmd]').forEach(btn => {
      btn.onclick = () => this._run(btn.dataset.cmd, btn.dataset.cmd, handlers[btn.dataset.cmd]);
    });
    root.querySelectorAll('[data-fn]').forEach(btn => {
      btn.onclick = () => this._run(btn.dataset.fn, btn.dataset.fn, handlers[btn.dataset.fn]);
    });

    const ta = root.querySelector('textarea');
    ta.onkeydown = async (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
        const lines = ta.value.split('\n').map(s => s.trim()).filter(Boolean);
        for (const line of lines) {
          await this._runInline(line);
        }
      }
    };

    root.querySelector('#cmd-clear').onclick = () => {
      this.logs = [];
      this._renderLog();
    };
    root.querySelector('#cmd-copy').onclick = () => {
      navigator.clipboard.writeText(this.logs.map(l =>
        `${l.ts} → ${l.request}\n${l.error ? 'ERR ' + l.error : '← ' + (l.response || '')} (${l.ms}ms)`
      ).join('\n'));
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
    const entry = { ts: new Date().toLocaleTimeString('zh-CN', { hour12: false }), request: requestDesc, response: null, ms: null, error: null };
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
    const entry = { ts: new Date().toLocaleTimeString('zh-CN', { hour12: false }), request: line, response: null, ms: null, error: null };
    this.logs.push(entry);
    this._renderLog();
    try {
      const enc = new TextEncoder();
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
      <div class="log-entry ${e.error ? 'error' : ''}">
        <div class="log-ts">${e.ts}</div>
        <div class="log-req">→ ${escapeHtml(e.request)}</div>
        ${e.response ? `<div class="log-resp">← ${escapeHtml(e.response)} <span class="log-ms">(${e.ms}ms)</span></div>` : ''}
        ${e.error ? `<div class="log-err">✗ ${escapeHtml(e.error)} <span class="log-ms">(${e.ms}ms)</span></div>` : ''}
      </div>
    `).join('') || '<div class="text-sm text-dim" style="padding:24px;text-align:center;">无日志</div>';
  },
};

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[c]);
}
