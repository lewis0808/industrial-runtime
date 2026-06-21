// 压测工具：滑动窗口统计、百分位、Pipeline 控制器。

/** 简单线性插值百分位。samples 不需要预排序（内部排序）。 */
export function percentile(samples, p) {
  if (samples.length === 0) return NaN;
  if (p <= 0) return Math.min(...samples);
  if (p >= 100) return Math.max(...samples);
  const sorted = [...samples].sort((a, b) => a - b);
  const rank = (p / 100) * (sorted.length - 1);
  const lo = Math.floor(rank);
  const hi = Math.ceil(rank);
  if (lo === hi) return sorted[lo];
  return sorted[lo] + (sorted[hi] - sorted[lo]) * (rank - lo);
}

/**
 * 滑动窗口统计（默认 1 秒桶）。
 */
export class SlidingStats {
  /**
   * @param {number} windowSec 窗口大小（秒），统计最近 windowSec 秒
   */
  constructor(windowSec = 1) {
    this.windowMs = windowSec * 1000;
    this._samples = [];      // 所有样本（带 ts）
    this._errors = 0;
  }

  /** 记一条 latency（ms）。 */
  record(latencyMs, nowMs = Date.now()) {
    this._samples.push({ ts: nowMs, lat: latencyMs });
    this._gc(nowMs);
  }

  /** 记一条错误（不计 latency）。 */
  recordError(nowMs = Date.now()) {
    this._errors++;
    this._gc(nowMs);
  }

  _gc(nowMs) {
    const cutoff = nowMs - this.windowMs;
    while (this._samples.length > 0 && this._samples[0].ts < cutoff) {
      this._samples.shift();
    }
  }

  /** 返回当前窗口的统计摘要。 */
  summary(nowMs = Date.now()) {
    this._gc(nowMs);
    const lats = this._samples.map(s => s.lat);
    lats.sort((a, b) => a - b);
    return {
      count: lats.length,
      opsPerSec: lats.length / (this.windowMs / 1000),
      p50: percentile(lats, 50),
      p95: percentile(lats, 95),
      p99: percentile(lats, 99),
      p100: percentile(lats, 100),
      errors: this._errors,
    };
  }

  reset() { this._samples = []; this._errors = 0; }
}
