import type { Lc3Transport } from '@/lc3/transport';

export class BleGattTransport implements Lc3Transport {
  readonly kind = 'ble' as const;
  getAttMtu(): number {
    return 23;
  }
  async writeFragments(): Promise<void> {
    throw new Error('BLE is not available on web');
  }
  onFragment(): () => void {
    return () => undefined;
  }
  async close(): Promise<void> {}
}

export async function openBleTransport(): Promise<never> {
  throw new Error('BLE is not available on web');
}
