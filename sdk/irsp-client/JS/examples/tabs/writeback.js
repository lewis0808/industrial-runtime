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
      <div class="space-y-3">
        <!-- 插件检测 -->
        <div class="surface" style="padding: 14px;">
          <div class="h-cmd-group">写回链路状态</div>
          <div class="flex items-center gap-3 mt-3">
            <div id="wb-status-wrap" class="flex items-center gap-2">
              <span id="wb-status-dot" class="live-dot offline"></span>
              <span id="wb-status" class="mono text-xs text-muted">未检测</span>
            </div>
            <button id="wb-probe" class="btn btn-ghost btn-sm">重新检测</button>
            <span class="text-xs text-dim mono">SET demo/__probe__=1 → 500ms 内收到推送 = 已加载</span>
          </div>
        </div>

        <!-- 单次写回 -->
        <div class="surface" style="padding: 14px;">
          <div class="h-cmd-group">单次写回 · 时序可视化</div>
          <div class="flex flex-wrap gap-2 items-center mt-2">
            <input id="wb-name" class="input input-sm mono w-48" value="demo/foo" />
            <select id="wb-type" class="select select-sm mono">
              <option>i64</option><option>f64</option><option>bool</option><option>str</option>
            </select>
            <input id="wb-value" class="input input-sm mono w-32" value="42" />
            <button id="wb-send" class="btn btn-primary btn-sm" disabled>发送 SET</button>
          </div>
          <div id="wb-chart" class="mt-3" style="width:100%;height:200px;"></div>
          <div id="wb-result" class="text-xs text-dim mono mt-2">未运行</div>
        </div>

        <!-- 批量写回 -->
        <div class="surface" style="padding: 14px;">
          <div class="h-cmd-group">批量写回</div>
          <div class="flex flex-wrap gap-2 items-center mt-2">
            <span class="text-xs text-muted mono">PREFIX</span>
            <input id="wb-bp" class="input input-sm mono w-40" value="demo/batch/" />
            <span class="text-xs text-muted mono">COUNT</span>
            <input id="wb-bn" type="number" class="input input-sm mono w-20" value="100" />
            <span class="text-xs text-muted mono">START</span>
            <input id="wb-bv" type="number" class="input input-sm mono w-20" value="0" />
            <button id="wb-bstart" class="btn btn-primary btn-sm" disabled>开始</button>
          </div>
          <div class="progress mt-3"><div id="wb-progress-bar" style="width:0%;"></div></div>
          <div id="wb-bmetrics" class="text-xs text-dim mono mt-2">未运行</div>
        </div>

        <!-- 断言 -->
        <div class="surface" style="padding: 14px;">
          <div class="h-cmd-group">写回断言 · L1 同步受理 + L2 echo 回环</div>
          <div class="flex gap-2 items-center mt-2">
            <span class="text-xs text-muted mono">TOPIC</span>
            <input id="wb-aname" class="input input-sm mono w-48" value="demo/assert" />
            <span class="text-xs text-muted mono">VALUE</span>
            <input id="wb-aval" class="input input-sm mono w-24" value="99" />
            <button id="wb-assert" class="btn btn-primary btn-sm" disabled>运行断言</button>
          </div>
          <div id="wb-assert-result" class="mt-3"></div>
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
    this._probe();
  },
  onDisconnect() {
    this.client = null;
    this.pluginReady = false;
    this._setBtns(false);
    this._setStatus('offline', '未检测');
  },
  onHide() {},

  _setBtns(enabled) {
    document.getElementById('wb-send').disabled = !enabled;
    document.getElementById('wb-bstart').disabled = !enabled;
    document.getElementById('wb-assert').disabled = !enabled;
  },

  _setStatus(state, text) {
    const dot = document.getElementById('wb-status-dot');
    const lbl = document.getElementById('wb-status');
    dot.className = 'live-dot ' + state;
    if (state === 'online') {
      lbl.className = 'mono text-xs text-cyan';
    } else if (state === 'offline') {
      lbl.className = 'mono text-xs text-muted';
    } else if (state === 'pending') {
      lbl.className = 'mono text-xs text-yellow';
    } else if (state === 'error') {
      lbl.className = 'mono text-xs text-red';
    }
    lbl.textContent = text;
  },

  async _probe() {
    if (!this.client) return;
    this._setStatus('pending', '检测中…');
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
      if (got) {
        this._setStatus('online', '✓ 插件已加载');
      } else {
        this._setStatus('error', '✗ 未加载');
      }
      this._setBtns(got);
    } catch (e) {
      this._setStatus('error', '✗ 错误: ' + e.message);
      this.pluginReady = false;
    }
  },

  _ensureChart() {
    if (this.chart) return this.chart;
    this.chart = echarts.init(document.getElementById('wb-chart'));
    this.chart.setOption({
      backgroundColor: 'transparent',
      tooltip: {
        trigger: 'axis',
        backgroundColor: '#13181f',
        borderColor: '#2a323e',
        textStyle: { color: '#e4e7eb', fontFamily: 'JetBrains Mono, monospace', fontSize: 11 },
      },
      grid: { left: 50, right: 30, top: 20, bottom: 30 },
      xAxis: {
        type: 'category', data: ['SET 发出', '回复到达', '推送到达'],
        axisLine: { lineStyle: { color: '#2a323e' } },
        axisLabel: { color: '#7d8590', fontFamily: 'JetBrains Mono, monospace', fontSize: 10 },
      },
      yAxis: {
        type: 'value', name: 'ms',
        nameTextStyle: { color: '#7d8590', fontSize: 10 },
        axisLine: { lineStyle: { color: '#2a323e' } },
        axisLabel: { color: '#7d8590', fontFamily: 'JetBrains Mono, monospace', fontSize: 10 },
        splitLine: { lineStyle: { color: '#1a2029' } },
      },
      series: [{
        type: 'bar', data: [0, 0, 0], name: '相对时间',
        itemStyle: { color: '#00d4ff' },
        barWidth: '40%',
      }],
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
        series: [{
          data: events.map(e => +e.t.toFixed(3)),
          itemStyle: {
            color: (p) => p.name === '推送到达' ? '#3fb950' : (p.name === '回复到达' ? '#00d4ff' : '#7d8590'),
          },
        }],
      });

      const totalMs = (pushAt || replyAt) - setSent;
      document.getElementById('wb-result').innerHTML = `
        <div class="metric-grid">
          <div class="metric-cell"><div class="metric-label">RESPONSE</div><div class="metric-value cyan">${(replyAt - setSent).toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
          <div class="metric-cell"><div class="metric-label">PUSH</div><div class="metric-value ${pushAt ? '' : 'text-red'}">${pushAt ? (pushAt - setSent).toFixed(3) + '<span class="text-dim" style="font-size:12px;">ms</span>' : '超时'}</div></div>
          <div class="metric-cell"><div class="metric-label">ROUND TRIP</div><div class="metric-value">${totalMs.toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
          <div class="metric-cell"><div class="metric-label">REPLY</div><div class="metric-value" style="font-size:14px;">${r}</div></div>
        </div>
      `;
    } catch (e) {
      document.getElementById('wb-result').innerHTML = `<span class="text-red mono">错误: ${e.message}</span>`;
    }
  },

  async _batchWrite() {
    if (!this.client || !this.pluginReady) return;
    const prefix = document.getElementById('wb-bp').value;
    const n = parseInt(document.getElementById('wb-bn').value, 10);
    const startV = parseInt(document.getElementById('wb-bv').value, 10);
    const bar = document.getElementById('wb-progress-bar');
    const metrics = document.getElementById('wb-bmetrics');
    const lats = [];

    const t0 = performance.now();
    for (let i = 0; i < n; i++) {
      const ts = performance.now();
      try {
        await this.client.set(prefix + i, startV + i, 'i64');
        lats.push(performance.now() - ts);
      } catch (e) { /* ignore */ }
      bar.style.width = ((i + 1) / n * 100) + '%';
      if (i % 10 === 0 || i === n - 1) {
        const avg = lats.length ? lats.reduce((a, b) => a + b, 0) / lats.length : 0;
        metrics.innerHTML = `进度 <span class="text-cyan">${i + 1}/${n}</span> · 平均 ${avg.toFixed(3)}ms`;
      }
    }
    const elapsed = performance.now() - t0;
    lats.sort((a, b) => a - b);
    const p = (q) => lats.length ? lats[Math.min(lats.length - 1, Math.floor(q / 100 * lats.length))].toFixed(3) : '—';
    metrics.innerHTML = `
      <div class="metric-grid">
        <div class="metric-cell"><div class="metric-label">OPS/S</div><div class="metric-value cyan">${(n / (elapsed / 1000)).toFixed(0)}</div></div>
        <div class="metric-cell"><div class="metric-label">ELAPSED</div><div class="metric-value">${(elapsed / 1000).toFixed(2)}<span class="text-dim" style="font-size:12px;">s</span></div></div>
        <div class="metric-cell"><div class="metric-label">P50</div><div class="metric-value">${p(50)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">P95</div><div class="metric-value" style="color:var(--yellow);">${p(95)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">MAX</div><div class="metric-value" style="color:var(--red);">${lats.length ? lats[lats.length - 1].toFixed(3) : '—'}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">COUNT</div><div class="metric-value">${n}</div></div>
      </div>
    `;
  },

  async _assert() {
    if (!this.client || !this.pluginReady) return;
    const name = document.getElementById('wb-aname').value;
    const raw = document.getElementById('wb-aval').value;
    const expected = BigInt(raw);
    const resultEl = document.getElementById('wb-assert-result');

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
      const reply = await this.client.set(name, expected, 'i64');
      const tag = await pushPromise;
      const l1Pass = (reply === 'ok' || reply === 'accepted' || reply === 'OK');
      const l2Pass = !!tag && BigInt(tag.value) === expected;

      resultEl.innerHTML = `
        <div class="assert-line ${l1Pass ? 'assert-pass' : 'assert-fail'}">
          <span class="assert-tag ${l1Pass ? 'pass' : 'fail'}">${l1Pass ? 'PASS' : 'FAIL'}</span>
          <span>L1 同步受理</span>
          <span class="text-muted" style="margin-left:auto;">reply: ${reply}</span>
        </div>
        <div class="assert-line ${l2Pass ? 'assert-pass' : 'assert-fail'}">
          <span class="assert-tag ${l2Pass ? 'pass' : 'fail'}">${l2Pass ? 'PASS' : 'FAIL'}</span>
          <span>L2 echo 回环</span>
          <span class="text-muted" style="margin-left:auto;">${tag ? `got ${tag.value}` : '无推送'}</span>
        </div>
      `;
    } catch (e) {
      resultEl.innerHTML = `<div class="assert-line assert-fail"><span class="assert-tag fail">ERR</span><span class="text-red mono">${e.message}</span></div>`;
    }
  },
};
