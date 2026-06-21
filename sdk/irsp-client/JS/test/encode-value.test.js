import { test } from 'node:test';
import assert from 'node:assert/strict';
import { encodeValue, decodeValue } from '../src/irsp1.js';

test('encodeValue bool round-trip', () => {
  const bytes = encodeValue('bool', true);
  assert.deepEqual(Array.from(bytes), [1]);
  assert.equal(decodeValue('bool', bytes), true);
});

test('encodeValue i32 round-trip', () => {
  const bytes = encodeValue('i32', 123456);
  assert.equal(bytes.length, 4);
  assert.equal(decodeValue('i32', bytes), 123456);
});

test('encodeValue i64 round-trip', () => {
  const bytes = encodeValue('i64', 9007199254740993n);
  assert.equal(bytes.length, 8);
  assert.equal(decodeValue('i64', bytes), 9007199254740993n);
});

test('encodeValue f64 round-trip', () => {
  const bytes = encodeValue('f64', 3.141592653589793);
  assert.equal(bytes.length, 8);
  assert.equal(decodeValue('f64', bytes), 3.141592653589793);
});

test('encodeValue str round-trip', () => {
  const bytes = encodeValue('str', 'hello 世界');
  assert.equal(decodeValue('str', bytes), 'hello 世界');
});

test('encodeValue i16 round-trip', () => {
  const bytes = encodeValue('i16', -32000);
  assert.equal(decodeValue('i16', bytes), -32000);
});

test('encodeValue throws on unknown type', () => {
  assert.throws(() => encodeValue('unknown', 1), /unknown type/);
});

test('encodeValue binary round-trip', () => {
  const src = new Uint8Array([1, 2, 3, 4, 5]);
  const bytes = encodeValue('binary', src);
  assert.deepEqual(Array.from(bytes), [1, 2, 3, 4, 5]);
  assert.deepEqual(Array.from(decodeValue('binary', bytes)), [1, 2, 3, 4, 5]);
});

test('encodeValue binary rejects non-Uint8Array', () => {
  assert.throws(() => encodeValue('binary', [1, 2, 3]), /Uint8Array/);
});

import { inferType } from '../src/irsp1.js';

test('inferType bigint -> i64', () => {
  assert.equal(inferType(123n), 'i64');
});

test('inferType number int-range -> i32 or i64', () => {
  assert.equal(inferType(42), 'i32');
  assert.equal(inferType(Number.MAX_SAFE_INTEGER + 1), 'f64'); // 超过安全整数范围
});

test('inferType number float -> f64', () => {
  assert.equal(inferType(3.14), 'f64');
});

test('inferType boolean -> bool', () => {
  assert.equal(inferType(true), 'bool');
});

test('inferType string -> str', () => {
  assert.equal(inferType('hello'), 'str');
});

test('inferType Uint8Array -> binary', () => {
  assert.equal(inferType(new Uint8Array([1, 2, 3])), 'binary');
});
