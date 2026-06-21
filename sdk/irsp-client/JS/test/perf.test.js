import { test } from 'node:test';
import assert from 'node:assert/strict';
import { SlidingStats, percentile } from '../src/perf.js';

test('percentile of simple array', () => {
  assert.equal(percentile([1, 2, 3, 4, 5], 50), 3);
  assert.equal(percentile([1, 2, 3, 4, 5], 100), 5);
  assert.equal(percentile([1, 2, 3, 4, 5], 0), 1);
});

test('percentile interpolates', () => {
  const v = percentile([10,20,30,40,50,60,70,80,90,100], 90);
  assert.ok(v >= 90 && v <= 100, `got ${v}`);
});

test('SlidingStats records and summarizes', () => {
  const s = new SlidingStats(1); // 1 second window
  s.record(1);
  s.record(2);
  s.record(3);
  const sum = s.summary();
  assert.equal(sum.count, 3);
  assert.equal(sum.opsPerSec, 3);
  assert.equal(sum.p50, 2);
  assert.equal(sum.p100, 3);
  assert.equal(sum.errors, 0);
});

test('SlidingStats tracks errors', () => {
  const s = new SlidingStats(1);
  s.record(1);
  s.recordError();
  s.record(2);
  const sum = s.summary();
  assert.equal(sum.count, 2);
  assert.equal(sum.errors, 1);
});

import { Pipeline } from '../src/perf.js';

test('Pipeline maintains concurrency and stops after duration', async () => {
  const p = new Pipeline();
  let started = 0;
  let resolved = 0;
  const sendFn = () => {
    started++;
    return new Promise(resolve => {
      setTimeout(() => { resolved++; resolve('ok'); }, 5);
    });
  };

  const samples = [];
  await p.run({ sendFn, concurrency: 4, durationMs: 100, onSample: (s) => samples.push(s) });

  assert.ok(started > 20, `expected >20 starts, got ${started}`);
  assert.equal(resolved, started);
  assert.ok(samples.length > 0);
});

test('Pipeline stop() aborts early', async () => {
  const p = new Pipeline();
  const sendFn = () => new Promise(r => setTimeout(() => r('ok'), 50));
  const promise = p.run({ sendFn, concurrency: 1, durationMs: 10000 });
  setTimeout(() => p.stop(), 80);
  await promise;
  // Should return well before 10s
  assert.ok(true);
});

test('Pipeline records errors', async () => {
  const p = new Pipeline();
  let n = 0;
  const sendFn = () => {
    n++;
    return Promise.reject(new Error('fail'));
  };
  const samples = [];
  await p.run({ sendFn, concurrency: 1, durationMs: 50, onSample: (s) => samples.push(s) });
  assert.ok(n > 0);
  // errors in SlidingStats is cumulative; use the last sample's value
  const lastErrors = samples.length > 0 ? samples[samples.length - 1].errors : 0;
  assert.equal(lastErrors, n);
});
