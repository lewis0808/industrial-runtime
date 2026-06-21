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
