// Tab 1：实时监控。连接后 SCAN 预填 + SUBSCRIBE + SUBEVENT，渲染 Tags 表与 Events 流。

const TYPE_CLASS = {
  i64: 'type-i64', i32: 'type-i32', i16: 'type-i16', i8: 'type-i8',
  u64: 'type-u64', u32: 'type-u32', u16: 'type-u16', u8: 'type-u8',
  f64: 'type-f64', f32: 'type-f32',
  bool: 'type-bool', str: 'type-str', null: 'type-null',
};

// 按 topic 关键词推断单位（可视化加分项，不改变值）
function inferUnit(name) {
  const n = name.toLowerCase();
  if (n.includes('temp') || n.includes('temperature')) return '°';
  if (n.includes('pct') || n.includes('percent') || n.includes('humidity')) return '%';
  if (n.includes('pressure')) return 'kPa';
  if (n.includes('speed') || n.includes('rpm')) return 'rpm';
  if (n.includes('voltage')) return 'V';
  if (n.includes('current')) return 'A';
  if (n.includes('power')) return 'W';
  return '';
}

export default {
  init(shared) {
    this.shared = shared;
    this.tags = new Map();       // name -> {type, value, ts}
    this.prevValues = new Map(); // name -> 上一次的值（用于变化方向）
    this.lastChangedAt = new Map(); // name -> 是否最近变化（用于一次性高亮）
    this.client = null;

    const root = document.getElementById('tab-monitor');
    root.innerHTML = `
      <div class="flex items-center gap-3 mb-3 surface-elevate" style="padding: 8px 12px;">
        <span class="text-xs text-muted mono">SUB</span>
        <input id="mon-pattern" class="input mono flex-1" style="max-width:200px;" value="#" />
        <button id="mon-apply" class="btn btn-ghost btn-sm">应用</button>
        <div style="width:1px;height:20px;background:var(--border);margin:0 8px;"></div>
        <span class="text-xs text-muted mono">FILTER</span>
        <input id="mon-filter" class="input mono flex-1" style="max-width:200px;" placeholder="如 demo/*" />
      </div>
      <div class="grid grid-cols-1 lg:grid-cols-[1.4fr_1fr] gap-3">
        <div class="surface" style="display:flex;flex-direction:column;max-height:75vh;">
          <div class="section-title">
            <span>TAGS</span>
            <span class="count" id="mon-tag-count">0</span>
          </div>
          <div id="mon-tags" class="overflow-auto" style="flex:1;">
            <div class="text-sm text-dim" style="padding:24px;text-align:center;">连接后显示实时 Tag…</div>
          </div>
        </div>
        <div class="surface" style="display:flex;flex-direction:column;max-height:75vh;">
          <div class="section-title">
            <span>EVENTS</span>
            <span class="count" id="mon-evt-count">0</span>
          </div>
          <div id="mon-log" class="overflow-auto" style="flex:1;">
            <div class="text-sm text-dim" style="padding:24px;text-align:center;">连接后显示事件…</div>
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
    this.client.on('tag', (t) => {
      const prev = this.tags.get(t.name);
      if (prev && prev.value !== t.value) {
        this.prevValues.set(t.name, prev.value);
        this.lastChangedAt.set(t.name, performance.now());
      }
      this.tags.set(t.name, t);
      this._renderTags();
    });
    this.client.on('event', (e) => this._addEvent(e));

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
    this.prevValues.clear();
    this.lastChangedAt.clear();
    this._renderTags();
    document.getElementById('mon-log').innerHTML = '<div class="text-sm text-dim" style="padding:24px;text-align:center;">已断开</div>';
    document.getElementById('mon-evt-count').textContent = '0';
  },

  _renderTags() {
    const wrap = document.getElementById('mon-tags');
    const countEl = document.getElementById('mon-tag-count');
    const filterRaw = document.getElementById('mon-filter')?.value.trim() || '';
    let prefix = '';
    if (filterRaw.endsWith('*')) { prefix = filterRaw.slice(0, -1); }

    const entries = [...this.tags.entries()].sort((a, b) => a[0].localeCompare(b[0]))
      .filter(([name]) => !prefix || name.startsWith(prefix));

    countEl.textContent = entries.length;

    if (entries.length === 0) {
      wrap.innerHTML = '<div class="text-sm text-dim" style="padding:24px;text-align:center;">暂无 Tag</div>';
      return;
    }

    const fmtTime = (tsNs) => {
      try { return new Date(Number(tsNs / 1000000n)).toLocaleTimeString('zh-CN', { hour12: false }); } catch { return ''; }
    };
    const fmtVal = (v) => (typeof v === 'bigint' ? v.toString() : String(v));
    const now = performance.now();

    const rows = entries.map(([name, t]) => {
      const cls = TYPE_CLASS[t.type] || 'type-null';
      const unit = inferUnit(name);
      const changedAt = this.lastChangedAt.get(name);
      let changeClass = '';
      let delta = '';
      if (changedAt && now - changedAt < 1000) {
        const prev = this.prevValues.get(name);
        if (prev !== undefined) {
          try {
            const cur = typeof t.value === 'bigint' ? t.value : (t.value === 'true' ? 1 : (t.value === 'false' ? 0 : Number(t.value)));
            const old = typeof prev === 'bigint' ? prev : (prev === 'true' ? 1 : (prev === 'false' ? 0 : Number(prev)));
            if (cur > old) { changeClass = 'changed-up'; delta = '<span class="tag-delta up">▲</span>'; }
            else if (cur < old) { changeClass = 'changed-down'; delta = '<span class="tag-delta down">▼</span>'; }
          } catch {}
        }
      }
      return `<div class="tag-row ${changeClass}">
        <div class="tag-name">${name}</div>
        <div><span class="type-badge ${cls}">${t.type}</span></div>
        <div class="tag-value">${fmtVal(t.value)}${unit ? `<span class="text-dim" style="font-size:11px;">${unit}</span>` : ''} ${delta}</div>
        <div class="tag-time">${fmtTime(t.ts)}</div>
      </div>`;
    }).join('');
    wrap.innerHTML = rows;
  },

  _addEvent(e) {
    const log = document.getElementById('mon-log');
    const empty = log.querySelector('.text-dim');
    if (empty) log.innerHTML = '';
    const sev = (e.severity || 'info').toLowerCase();
    const fmtTime = (tsNs) => {
      try { return new Date(Number(tsNs / 1000000n)).toLocaleTimeString('zh-CN', { hour12: false }); } catch { return ''; }
    };
    const row = document.createElement('div');
    row.className = 'event-row';
    row.innerHTML = `<div class="event-bar ${sev}"></div>` +
      `<span class="event-sev ${sev}">${sev}</span>` +
      `<span class="event-msg">${e.category}: ${e.message}</span>` +
      `<span class="event-time">${fmtTime(e.ts)}</span>`;
    log.prepend(row);
    while (log.childElementCount > 100) log.lastElementChild.remove();
    document.getElementById('mon-evt-count').textContent = log.childElementCount;
  },
};
