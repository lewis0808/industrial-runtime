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
