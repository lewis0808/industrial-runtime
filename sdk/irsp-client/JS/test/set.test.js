import { test } from 'node:test';
import assert from 'node:assert/strict';
import { IrspClient } from '../src/client.js';

// Mock WebSocket for Node. Captures sent frames, allows injecting replies.
class MockWebSocket {
  constructor() {
    this.readyState = 1; // OPEN
    this.sent = [];
    this._handlers = {};
  }
  send(data) { this.sent.push(new Uint8Array(data)); }
  close() { this.readyState = 3; }
  emit(message) { this._handlers.message({ data: message.buffer }); }
  set onmessage(fn) { this._handlers.message = fn; }
  set onopen(fn) { this._open = fn; }
  set onclose(fn) {}
  set onerror(fn) {}
}

test('set() encodes SET name type value', async () => {
  const client = new IrspClient('ws://x');
  const mock = new MockWebSocket();
  client.ws = mock;
  // Wire onmessage the same way connect() does
  mock.onmessage = (ev) => client._onFrame(new Uint8Array(ev.data));

  const promise = client.set('demo/foo', 42n, 'i64');
  // Let microtask flush _send queue
  await Promise.resolve();

  const sent = mock.sent[0];
  const dec = new TextDecoder();
  const text = dec.decode(sent);
  assert.ok(text.startsWith('*4\r\n'), `array of 4, got: ${text}`);
  assert.ok(text.includes('$3\r\nSET\r\n'));
  assert.ok(text.includes('$8\r\ndemo/foo\r\n'));
  assert.ok(text.includes('$3\r\ni64\r\n'));
  assert.ok(text.includes('$8\r\n')); // value bulk of 8 bytes

  // Reply '+ok'
  const ok = new TextEncoder().encode('+ok\r\n');
  mock.emit(ok);
  const r = await promise;
  assert.equal(r, 'ok');
});

test('set() infers type when not provided', async () => {
  const client = new IrspClient('ws://x');
  const mock = new MockWebSocket();
  client.ws = mock;
  mock.onmessage = (ev) => client._onFrame(new Uint8Array(ev.data));

  const promise = client.set('demo/bar', 3.14);
  await Promise.resolve();

  const sent = mock.sent[0];
  const text = new TextDecoder().decode(sent);
  assert.ok(text.includes('$3\r\nf64\r\n'), text);

  const ok = new TextEncoder().encode('+ok\r\n');
  mock.emit(ok);
  await promise;
});
