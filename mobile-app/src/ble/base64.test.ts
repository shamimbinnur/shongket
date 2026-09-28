import { base64ToBytes, bytesToBase64 } from '@/ble/base64';

describe('base64', () => {
  test('round-trips fragment bytes including padding', () => {
    const samples = [
      new Uint8Array([1, 2, 3]),
      new Uint8Array([1]),
      new Uint8Array([1, 2]),
      new Uint8Array(Array.from({ length: 20 }, (_, i) => i)),
    ];
    for (const sample of samples) {
      const encoded = bytesToBase64(sample);
      expect(Array.from(base64ToBytes(encoded))).toEqual(Array.from(sample));
    }
  });
});
