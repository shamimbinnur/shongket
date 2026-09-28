import { DEFAULT_ATT_MTU, FLAG_END, FLAG_START, HEADER_SIZE, MAX_JSON_BYTES } from '@/lc3/constants';
import { Lc3FramingError } from '@/lc3/errors';
import { FrameAssembler, encodeFrame, jsonPayloadCapacity } from '@/lc3/framing';

describe('jsonPayloadCapacity', () => {
  test('default MTU 23 carries 12 JSON bytes', () => {
    expect(jsonPayloadCapacity(DEFAULT_ATT_MTU)).toBe(12);
  });
});

describe('encodeFrame', () => {
  test('one-fragment frames set START and END', () => {
    const [packet] = encodeFrame('{"v":1}', 1, 512);
    expect(packet[0]).toBe(1);
    expect(packet[1]).toBe(FLAG_START | FLAG_END);
    const view = new DataView(packet.buffer);
    expect(view.getUint16(2, false)).toBe(1);
    expect(view.getUint16(4, false)).toBe(7);
    expect(view.getUint16(6, false)).toBe(0);
  });

  test('multi-fragment frames are contiguous and big-endian', () => {
    const json = '{"v":1,"op":"list_peers","args":{"offset":0,"limit":3}}';
    const fragments = encodeFrame(json, 42, DEFAULT_ATT_MTU);
    expect(fragments.length).toBeGreaterThan(1);
    expect(fragments[0][1] & FLAG_START).toBe(FLAG_START);
    expect(fragments[0][1] & FLAG_END).toBe(0);
    expect(fragments[fragments.length - 1][1] & FLAG_END).toBe(FLAG_END);

    const assembler = new FrameAssembler();
    let result = null;
    for (const fragment of fragments) {
      result = assembler.push(fragment);
    }
    expect(result).toEqual({ frameId: 42, json });
  });

  test('rejects empty json and oversized documents', () => {
    expect(() => encodeFrame('', 1)).toThrow(Lc3FramingError);
    expect(() => encodeFrame('x'.repeat(MAX_JSON_BYTES + 1), 1)).toThrow(Lc3FramingError);
    expect(() => encodeFrame('{}', 0)).toThrow(Lc3FramingError);
  });
});

describe('FrameAssembler', () => {
  test('rejects interleaved frames and resets', () => {
    const a = encodeFrame(`{"a":"${'x'.repeat(40)}"}`, 1, DEFAULT_ATT_MTU);
    const b = encodeFrame(`{"b":"${'y'.repeat(40)}"}`, 2, DEFAULT_ATT_MTU);
    expect(a.length).toBeGreaterThan(1);
    const assembler = new FrameAssembler();
    assembler.push(a[0]);
    expect(() => assembler.push(b[0])).toThrow(Lc3FramingError);
    const single = encodeFrame('{"ok":true}', 3, 512);
    expect(assembler.push(single[0])).toEqual({ frameId: 3, json: '{"ok":true}' });
  });

  test('rejects reserved flags, bad version, and out-of-order offsets', () => {
    const assembler = new FrameAssembler();
    const packet = encodeFrame('{"v":1}', 1, 512)[0];
    packet[1] = FLAG_START | FLAG_END | 0x04;
    expect(() => assembler.push(packet)).toThrow(Lc3FramingError);

    const version = encodeFrame('{"v":1}', 1, 512)[0];
    version[0] = 2;
    expect(() => assembler.push(version)).toThrow(Lc3FramingError);

    const fragments = encodeFrame('{"v":1,"op":"hello","args":{}}', 9, DEFAULT_ATT_MTU);
    assembler.reset();
    assembler.push(fragments[0]);
    const skipped = fragments[2] ?? fragments[1];
    expect(() => assembler.push(skipped)).toThrow(Lc3FramingError);
  });

  test('rejects fragments shorter than the header', () => {
    const assembler = new FrameAssembler();
    expect(() => assembler.push(new Uint8Array(HEADER_SIZE - 1))).toThrow(Lc3FramingError);
  });
});
