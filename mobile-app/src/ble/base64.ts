const TABLE = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

export function bytesToBase64(bytes: Uint8Array): string {
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i];
    const b = i + 1 < bytes.length ? bytes[i + 1] : 0;
    const c = i + 2 < bytes.length ? bytes[i + 2] : 0;
    const triple = (a << 16) | (b << 8) | c;
    out += TABLE[(triple >> 18) & 63];
    out += TABLE[(triple >> 12) & 63];
    out += i + 1 < bytes.length ? TABLE[(triple >> 6) & 63] : '=';
    out += i + 2 < bytes.length ? TABLE[triple & 63] : '=';
  }
  return out;
}

export function base64ToBytes(value: string): Uint8Array {
  const clean = value.replace(/[^A-Za-z0-9+/=]/g, '');
  const padded = clean + '='.repeat((4 - (clean.length % 4)) % 4);
  const padding = padded.endsWith('==') ? 2 : padded.endsWith('=') ? 1 : 0;
  const out = new Uint8Array((padded.length * 3) / 4 - padding);
  let byteIndex = 0;
  for (let i = 0; i < padded.length; i += 4) {
    const c0 = TABLE.indexOf(padded[i]);
    const c1 = TABLE.indexOf(padded[i + 1]);
    const c2 = padded[i + 2] === '=' ? 0 : TABLE.indexOf(padded[i + 2]);
    const c3 = padded[i + 3] === '=' ? 0 : TABLE.indexOf(padded[i + 3]);
    const triple = (c0 << 18) | (c1 << 12) | (c2 << 6) | c3;
    if (byteIndex < out.length) out[byteIndex++] = (triple >> 16) & 255;
    if (byteIndex < out.length) out[byteIndex++] = (triple >> 8) & 255;
    if (byteIndex < out.length) out[byteIndex++] = triple & 255;
  }
  return out;
}
