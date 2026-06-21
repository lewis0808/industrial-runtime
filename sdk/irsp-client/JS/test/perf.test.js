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
