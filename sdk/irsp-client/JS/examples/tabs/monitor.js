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
