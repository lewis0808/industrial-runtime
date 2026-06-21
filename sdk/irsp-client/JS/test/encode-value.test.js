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
