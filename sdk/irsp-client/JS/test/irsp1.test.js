// irsp1 编解码单测（零依赖，无需服务端）：node --test
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { encodeRequest, decode, decodeValue, asStr, IrspError } from '../src/index.js';

const enc = (s) => new TextEncoder().encode(s);

test('encodeRequest 生成 bulk 数组', () => {
  const bytes = encodeRequest(['GET', 'a/b']);
  assert.equal(new TextDecoder().decode(bytes), '*2\r\n$3\r\nGET\r\n$3\r\na/b\r\n');
});

test('decode 基本类型', () => {
  assert.equal(decode(enc('+OK\r\n')), 'OK');
  assert.equal(decode(enc(':42\r\n')), 42n);
  assert.equal(decode(enc('$-1\r\n')), null);

  const err = decode(enc('-WRONG_ARITY too many\r\n'));
  assert.ok(err instanceof IrspError);
  assert.equal(err.code, 'WRONG_ARITY');
  assert.equal(err.irspMessage, 'too many');
});

test('decode 数组与 map', () => {
  const arr = decode(enc('*2\r\n$1\r\na\r\n$1\r\nb\r\n'));
  assert.equal(arr.length, 2);
  assert.equal(asStr(arr[0]), 'a');
  assert.equal(asStr(arr[1]), 'b');

  const m = decode(enc('%2\r\n$4\r\nname\r\n$3\r\na/b\r\n$4\r\ntype\r\n$3\r\nf64\r\n'));
  assert.equal(asStr(m.name), 'a/b');
  assert.equal(asStr(m.type), 'f64');
});

test('decodeValue 按类型解码（小端）', () => {
  const f = new Uint8Array(8);
  new DataView(f.buffer).setFloat64(0, 42.5, true);
  assert.equal(decodeValue('f64', f), 42.5);

  const i = new Uint8Array(4);
  new DataView(i.buffer).setInt32(0, 5, true);
  assert.equal(decodeValue('i32', i), 5);

  assert.equal(decodeValue('bool', new Uint8Array([1])), true);
  assert.equal(decodeValue('bool', new Uint8Array([0])), false);
  assert.equal(decodeValue('str', enc('hi')), 'hi');

  const big = new Uint8Array(8);
  new DataView(big.buffer).setBigInt64(0, 1749800000000000000n, true);
  assert.equal(decodeValue('i64', big), 1749800000000000000n);
});
