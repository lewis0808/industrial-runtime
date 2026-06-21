// irsp1 编解码（IRSP V1 编码层的 JS 实现）。
// 一个 WebSocket 二进制消息 = 一个 IRSP 帧（一个顶层 IrspValue）。
// 详见 irsp/doc/encoding/irsp1.md。

/** IRSP 错误回复（irsp1 的 `-CODE message`）。 */
export class IrspError extends Error {
  /** @param {string} code @param {string} message */
  constructor(code, message) {
    super(message ? `${code} ${message}` : code);
    this.name = 'IrspError';
    this.code = code;
    this.irspMessage = message;
  }
}

const CRLF = new Uint8Array([13, 10]);

function concat(chunks) {
  let total = 0;
  for (const c of chunks) total += c.length;
  const out = new Uint8Array(total);
  let off = 0;
  for (const c of chunks) {
    out.set(c, off);
    off += c.length;
  }
  return out;
}

/**
 * 把命令编码为 irsp1 请求帧（bulk 数组）。
 * @param {Array<string|Uint8Array>} parts 命令名与参数（字符串按 UTF-8，或原始字节）
 * @returns {Uint8Array}
 */
export function encodeRequest(parts) {
  const enc = new TextEncoder();
  const chunks = [enc.encode(`*${parts.length}\r\n`)];
  for (const p of parts) {
    const bytes = typeof p === 'string' ? enc.encode(p) : p;
    chunks.push(enc.encode(`$${bytes.length}\r\n`), bytes, CRLF);
  }
  return concat(chunks);
}

/** 把 bulk（Uint8Array）或简单字符串转为 JS 字符串。 */
export function asStr(v) {
  if (v == null) return null;
  if (typeof v === 'string') return v;
  return new TextDecoder().decode(v);
}

/**
 * 解码一个完整 irsp1 帧。
 * 返回值映射：simple→string，error→IrspError，integer→BigInt，bulk→Uint8Array|null，
 * array→Array，map→普通对象（键为 UTF-8 字符串）。
 * @param {Uint8Array} bytes
 */
export function decode(bytes) {
  let i = 0;
  const dec = new TextDecoder();

  function readLine() {
    let j = i;
    while (j + 1 < bytes.length && !(bytes[j] === 13 && bytes[j + 1] === 10)) j++;
    const line = dec.decode(bytes.subarray(i, j));
    i = j + 2;
    return line;
  }

  function parse() {
    if (i >= bytes.length) throw new Error('irsp1: 帧不完整');
    const type = String.fromCharCode(bytes[i]);
    i += 1;
    const line = readLine();
    switch (type) {
      case '+':
        return line;
      case '-': {
        const sp = line.indexOf(' ');
        return sp < 0 ? new IrspError(line, '') : new IrspError(line.slice(0, sp), line.slice(sp + 1));
      }
      case ':':
        return BigInt(line);
      case '$': {
        const n = parseInt(line, 10);
        if (n < 0) return null;
        const data = bytes.subarray(i, i + n);
        i += n + 2; // 跳过数据与结尾 CRLF
        return data;
      }
      case '*': {
        const n = parseInt(line, 10);
        if (n < 0) return null;
        const arr = [];
        for (let k = 0; k < n; k++) arr.push(parse());
        return arr;
      }
      case '%': {
        const n = parseInt(line, 10);
        const obj = {};
        for (let k = 0; k < n; k++) {
          const key = parse();
          const val = parse();
          obj[asStr(key)] = val;
        }
        return obj;
      }
      default:
        throw new Error(`irsp1: 未知类型字节 0x${bytes[i - 1 - line.length - 2]?.toString(16)}`);
    }
  }

  return parse();
}

/** 类型标签 + 原始字节 → JS 值（小端，见 datatype.md）。 */
export function decodeValue(type, b) {
  if (b == null) return null;
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  switch (type) {
    case 'null': return null;
    case 'bool': return b[0] !== 0;
    case 'i8': return dv.getInt8(0);
    case 'i16': return dv.getInt16(0, true);
    case 'i32': return dv.getInt32(0, true);
    case 'i64': return dv.getBigInt64(0, true);
    case 'u8': return dv.getUint8(0);
    case 'u16': return dv.getUint16(0, true);
    case 'u32': return dv.getUint32(0, true);
    case 'u64': return dv.getBigUint64(0, true);
    case 'f32': return dv.getFloat32(0, true);
    case 'f64': return dv.getFloat64(0, true);
    case 'str': return new TextDecoder().decode(b);
    default: return b; // 未知类型保留原始字节
  }
}

/** JS 值 → 类型标签对应的小端字节（与 decodeValue 对称，见 datatype.md）。 */
export function encodeValue(type, value) {
  switch (type) {
    case 'null': return new Uint8Array(0);
    case 'bool': return new Uint8Array([value ? 1 : 0]);
    case 'i8': {
      const b = new Uint8Array(1);
      new DataView(b.buffer).setInt8(0, value);
      return b;
    }
    case 'i16': {
      const b = new Uint8Array(2);
      new DataView(b.buffer).setInt16(0, value, true);
      return b;
    }
    case 'i32': {
      const b = new Uint8Array(4);
      new DataView(b.buffer).setInt32(0, value, true);
      return b;
    }
    case 'i64': {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setBigInt64(0, BigInt(value), true);
      return b;
    }
    case 'u8': {
      const b = new Uint8Array(1);
      new DataView(b.buffer).setUint8(0, value);
      return b;
    }
    case 'u16': {
      const b = new Uint8Array(2);
      new DataView(b.buffer).setUint16(0, value, true);
      return b;
    }
    case 'u32': {
      const b = new Uint8Array(4);
      new DataView(b.buffer).setUint32(0, value, true);
      return b;
    }
    case 'u64': {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setBigUint64(0, BigInt(value), true);
      return b;
    }
    case 'f32': {
      const b = new Uint8Array(4);
      new DataView(b.buffer).setFloat32(0, value, true);
      return b;
    }
    case 'f64': {
      const b = new Uint8Array(8);
      new DataView(b.buffer).setFloat64(0, value, true);
      return b;
    }
    case 'str': return new TextEncoder().encode(value);
    case 'binary': {
      if (!(value instanceof Uint8Array)) throw new Error('irsp1: binary expects Uint8Array');
      return value;
    }
    default: throw new Error(`irsp1: unknown type "${type}"`);
  }
}

/** 由 JS 值推断 IRSP 类型标签（用于 SET 无显式 type 时）。 */
export function inferType(value) {
  if (typeof value === 'bigint') return 'i64';
  if (typeof value === 'boolean') return 'bool';
  if (typeof value === 'number') {
    if (Number.isInteger(value) && Math.abs(value) <= 0x7fffffff) return 'i32';
    return 'f64';
  }
  if (typeof value === 'string') return 'str';
  if (value instanceof Uint8Array) return 'binary';
  throw new Error(`irsp1: cannot infer type for ${typeof value}`);
}
