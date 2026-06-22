// Tab 3：性能压测。
// 场景一：pipeline 请求-回复吞吐/延迟（单连接，维持 N 个在途请求）
// 场景三：多连接并发（开 M 个 IrspClient，每连接 R ops/s）
// 场景二（推送扇出）延期到 Phase 2。

import { IrspClient } from '../../src/index.js';
import { Pipeline } from '../../src/perf.js';

const CHART_THEME = {
  bg: 'transparent',
  axisLine: '#2a323e',
  axisLabel: '#7d8590',
  splitLine: '#1a2029',
  text: '#e4e7eb',
  cyan: '#00d4ff',
  orange: '#ff8c42',
  green: '#3fb950',
  yellow: '#d29922',
  red: '#f85149',
  purple: '#a371f7',
};

const HISTORY_KEY = 'irsp-bench-history';

function loadHistory() {
  try { return JSON.parse(localStorage.getItem(HISTORY_KEY) || '{}'); } catch { return {}; }
}
function saveHistory(h) {
  try { localStorage.setItem(HISTORY_KEY, JSON.stringify(h)); } catch {}
}

export default {
  init(shared) {
    this.shared = shared;
    this.chart = null;
    this.pipeline = null;
    this.scenario1Series = [];

    const root = document.getElementById('tab-benchmark');
    root.innerHTML = `
      <div class="space-y-3">
        <!-- 场景一 -->
        <div class="surface" style="padding: 14px;">
          <div class="h-cmd-group">场景一 · 请求-回复吞吐（pipeline）</div>
          <div class="flex flex-wrap items-center gap-2 mt-2">
            <select id="bm1-cmd" class="select select-sm mono">
              <option value="get">GET</option>
              <option value="mget">MGET</option>
              <option value="exists">EXISTS</option>
              <option value="ping">PING</option>
              <option value="set">SET (demo/*)</option>
            </select>
            <input id="bm1-name" class="input input-sm mono w-48" value="system/heartbeat" />
            <span class="text-xs text-muted mono">DEPTH</span>
            <input id="bm1-conc" type="number" class="input input-sm mono w-16" value="4" />
            <span class="text-xs text-muted mono">DURATION</span>
            <input id="bm1-dur" type="number" class="input input-sm mono w-16" value="10" />
            <span class="text-xs text-muted mono">s</span>
            <button id="bm1-start" class="btn btn-primary btn-sm">开始</button>
            <button id="bm1-stop" class="btn btn-ghost btn-sm" disabled>停止</button>
          </div>
          <div id="bm1-metrics" class="text-xs text-dim mono mt-3">未运行</div>
        </div>

        <!-- 场景二（延期） -->
        <div class="surface" style="padding: 14px; opacity: 0.5;">
          <div class="h-cmd-group">场景二 · 订阅推送扇出 <span style="color:var(--cyan);margin-left:8px;">Phase 2</span></div>
          <div class="text-xs text-muted mt-2">延期实现：依赖 demo 插件 + 双连接（writer 灌 + subscriber 统计）</div>
        </div>

        <!-- 场景三 -->
        <div class="surface" style="padding: 14px;">
          <div class="h-cmd-group">场景三 · 多连接并发</div>
          <div class="flex flex-wrap items-center gap-2 mt-2">
            <span class="text-xs text-muted mono">CONNS</span>
            <input id="bm3-conns" type="number" class="input input-sm mono w-16" value="16" />
            <span class="text-xs text-muted mono">RATE/CONN</span>
            <input id="bm3-rate" type="number" class="input input-sm mono w-16" value="100" />
            <span class="text-xs text-muted mono">NAME</span>
            <input id="bm3-name" class="input input-sm mono w-48" value="system/heartbeat" />
            <span class="text-xs text-muted mono">DURATION</span>
            <input id="bm3-dur" type="number" class="input input-sm mono w-16" value="10" />
            <span class="text-xs text-muted mono">s</span>
            <button id="bm3-start" class="btn btn-primary btn-sm">开始</button>
            <button id="bm3-stop" class="btn btn-ghost btn-sm" disabled>停止</button>
          </div>
          <div id="bm3-metrics" class="text-xs text-dim mono mt-3">未运行</div>
        </div>

        <!-- 时序图 -->
        <div class="surface" style="padding: 14px;">
          <div class="flex items-center justify-between mb-2">
            <div class="h-cmd-group" style="margin:0;border:0;padding:0;">时序图</div>
            <button id="bm-clear-hist" class="btn btn-ghost btn-xs">清除历史</button>
          </div>
          <div id="bm-chart" style="width:100%;height:340px;"></div>
        </div>
      </div>
    `;

    root.querySelector('#bm1-start').onclick = () => this._runScenario1();
    root.querySelector('#bm1-stop').onclick = () => this.pipeline?.stop();
    root.querySelector('#bm3-start').onclick = () => this._runScenario3();
    root.querySelector('#bm3-stop').onclick = () => { this._stopFlag = true; };
    root.querySelector('#bm-clear-hist').onclick = () => {
      saveHistory({});
      this._updateChartWithHistory();
      alert('历史已清除');
    };
  },

  onConnect() {},
  onDisconnect() {
    this.pipeline?.stop();
  },
  onHide() {
    this.pipeline?.stop();
    this._stopFlag = true;
  },

  _ensureChart() {
    if (this.chart) return this.chart;
    this.chart = echarts.init(document.getElementById('bm-chart'));
    this._applyChartOption(this.chart, { s1: [], s3: [], hist: loadHistory() });
    return this.chart;
  },

  _applyChartOption(chart, { s1, s3, hist }) {
    const xData = (s1.length ? s1 : s3).map(x => x.t);
    chart.setOption({
      backgroundColor: CHART_THEME.bg,
      tooltip: {
        trigger: 'axis',
        backgroundColor: '#13181f',
        borderColor: '#2a323e',
        textStyle: { color: CHART_THEME.text, fontFamily: 'JetBrains Mono, monospace', fontSize: 11 },
      },
      legend: {
        data: ['ops/s', 'p50', 'p95', 'p99', 'hist ops/s', 'hist p95-p99'],
        textStyle: { color: CHART_THEME.axisLabel, fontSize: 11 },
        top: 0,
      },
      grid: { left: 50, right: 50, top: 40, bottom: 30 },
      xAxis: {
        type: 'category',
        data: xData,
        axisLine: { lineStyle: { color: CHART_THEME.axisLine } },
        axisLabel: { color: CHART_THEME.axisLabel, fontFamily: 'JetBrains Mono, monospace', fontSize: 10 },
      },
      yAxis: [
        {
          type: 'value', name: 'ops/s',
          nameTextStyle: { color: CHART_THEME.axisLabel, fontSize: 10 },
          axisLine: { lineStyle: { color: CHART_THEME.axisLine } },
          axisLabel: { color: CHART_THEME.axisLabel, fontFamily: 'JetBrains Mono, monospace', fontSize: 10 },
          splitLine: { lineStyle: { color: CHART_THEME.splitLine } },
        },
        {
          type: 'value', name: 'latency(ms)',
          nameTextStyle: { color: CHART_THEME.axisLabel, fontSize: 10 },
          axisLine: { lineStyle: { color: CHART_THEME.axisLine } },
          axisLabel: { color: CHART_THEME.axisLabel, fontFamily: 'JetBrains Mono, monospace', fontSize: 10 },
          splitLine: { show: false },
        },
      ],
      series: [
        { name: 'ops/s', type: 'line', data: s1.map(x => x.ops), lineStyle: { color: CHART_THEME.cyan, width: 2 }, itemStyle: { color: CHART_THEME.cyan }, symbol: 'none' },
        { name: 'p50', type: 'line', yAxisIndex: 1, data: s1.map(x => x.p50), lineStyle: { color: CHART_THEME.green, width: 1.5 }, itemStyle: { color: CHART_THEME.green }, symbol: 'none' },
        { name: 'p95', type: 'line', yAxisIndex: 1, data: s1.map(x => x.p95), lineStyle: { color: CHART_THEME.yellow, width: 1.5 }, itemStyle: { color: CHART_THEME.yellow }, symbol: 'none' },
        { name: 'p99', type: 'line', yAxisIndex: 1, data: s1.map(x => x.p99), lineStyle: { color: CHART_THEME.red, width: 1.5 }, itemStyle: { color: CHART_THEME.red }, symbol: 'none' },
        // 历史参考线（虚线）
        ...(hist?.opsSeries?.length ? [{
          name: 'hist ops/s', type: 'line', data: hist.opsSeries,
          lineStyle: { color: CHART_THEME.cyan, width: 1, type: 'dashed', opacity: 0.5 },
          itemStyle: { color: CHART_THEME.cyan }, symbol: 'none',
        }] : []),
      ],
    }, true);
  },

  _updateChartWithHistory() {
    if (!this.chart) return;
    const hist = loadHistory();
    this._applyChartOption(this.chart, { s1: this.scenario1Series, s3: [], hist });
  },

  _pushSample(t, s) {
    this.scenario1Series.push({ t, ops: Math.round(s.opsPerSec), p50: +s.p50.toFixed(3), p95: +s.p95.toFixed(3), p99: +s.p99.toFixed(3) });
    const c = this._ensureChart();
    const hist = loadHistory();
    this._applyChartOption(c, { s1: this.scenario1Series, s3: [], hist });

    // 与历史对比
    const prev = hist.lastSummary;
    const delta = prev ? ((s.opsPerSec - prev.opsPerSec) / prev.opsPerSec * 100) : null;
    const deltaHtml = delta == null ? '' :
      `<span class="metric-delta ${delta >= 0 ? 'up' : 'down'}">${delta >= 0 ? '▲' : '▼'} ${Math.abs(delta).toFixed(1)}% vs 上次</span>`;

    document.getElementById('bm1-metrics').innerHTML = `
      <div class="metric-grid">
        <div class="metric-cell"><div class="metric-label">OPS/S</div><div class="metric-value cyan">${s.opsPerSec.toFixed(0)}</div>${deltaHtml}</div>
        <div class="metric-cell"><div class="metric-label">P50</div><div class="metric-value">${s.p50.toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">P95</div><div class="metric-value" style="color:var(--yellow);">${s.p95.toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">P99</div><div class="metric-value" style="color:var(--red);">${s.p99.toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">COUNT</div><div class="metric-value">${s.count}</div></div>
        <div class="metric-cell"><div class="metric-label">ERRORS</div><div class="metric-value ${s.errors > 0 ? 'text-red' : ''}">${s.errors}</div></div>
      </div>
    `;
  },

  async _runScenario1() {
    if (!this.shared.getClient()) { alert('请先在顶栏连接'); return; }
    const cmd = document.getElementById('bm1-cmd').value;
    const name = document.getElementById('bm1-name').value;
    const conc = parseInt(document.getElementById('bm1-conc').value, 10);
    const dur = parseInt(document.getElementById('bm1-dur').value, 10) * 1000;

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
    const result = await this.pipeline.run({
      sendFn: sendFns[cmd] || sendFns.get,
      concurrency: conc,
      durationMs: dur,
      onSample: (s) => this._pushSample(((Date.now() - startTime) / 1000).toFixed(1) + 's', s),
    });

    // 保存历史
    const hist = loadHistory();
    hist.opsSeries = this.scenario1Series.map(x => x.ops);
    hist.lastSummary = {
      opsPerSec: result.total > 0 ? result.total / (dur / 1000) : 0,
      cmd, concurrency: conc,
    };
    saveHistory(hist);

    document.getElementById('bm1-start').disabled = false;
    document.getElementById('bm1-stop').disabled = true;
    try { await benchClient.bye(); } catch {}
  },

  async _runScenario3() {
    if (!this.shared.getClient()) { alert('请先在顶栏连接'); return; }
    const url = this.shared.getClient().url;
    const conns = parseInt(document.getElementById('bm3-conns').value, 10);
    const rate = parseInt(document.getElementById('bm3-rate').value, 10);
    const name = document.getElementById('bm3-name').value;
    const dur = parseInt(document.getElementById('bm3-dur').value, 10) * 1000;

    document.getElementById('bm3-start').disabled = true;
    document.getElementById('bm3-stop').disabled = false;

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
    const hist = loadHistory();
    this._applyChartOption(c, { s1: this.scenario1Series, s3: [], hist });

    const avg = sorted.length > 0 ? (sorted.reduce((a, b) => a + b, 0) / sorted.length) : 0;
    document.getElementById('bm3-metrics').innerHTML = `
      <div class="metric-grid">
        <div class="metric-cell"><div class="metric-label">TOTAL OPS/S</div><div class="metric-value cyan">${opsPerSec.toFixed(0)}</div></div>
        <div class="metric-cell"><div class="metric-label">AVG LATENCY</div><div class="metric-value">${avg.toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">P95</div><div class="metric-value" style="color:var(--yellow);">${p(95).toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">P99</div><div class="metric-value" style="color:var(--red);">${p(99).toFixed(3)}<span class="text-dim" style="font-size:12px;">ms</span></div></div>
        <div class="metric-cell"><div class="metric-label">TOTAL OPS</div><div class="metric-value">${total}</div></div>
        <div class="metric-cell"><div class="metric-label">ERRORS</div><div class="metric-value ${errs > 0 ? 'text-red' : ''}">${errs}</div></div>
      </div>
    `;
  },
};
