import { Device, Subscription } from '@sfourdrinier/react-native-ble-plx';

import { bytesToBase64, base64ToBytes } from '@/ble/base64';
import { getBleManager } from '@/ble/manager';
import { DEFAULT_ATT_MTU, LC3_COMMAND_UUID, LC3_EVENT_UUID, LC3_SERVICE_UUID } from '@/lc3/constants';
import { Lc3Error } from '@/lc3/errors';
import { lc3Log } from '@/lc3/log';
import type { FragmentHandler, Lc3Transport } from '@/lc3/transport';

export class BleGattTransport implements Lc3Transport {
  readonly kind = 'ble' as const;
  private readonly handlers = new Set<FragmentHandler>();
  private monitor: Subscription | null = null;
  private attMtu: number;
  private closed = false;

  constructor(private readonly device: Device, attMtu: number) {
    this.attMtu = attMtu;
  }

  getAttMtu(): number {
    return this.attMtu;
  }

  async writeFragments(fragments: Uint8Array[]): Promise<void> {
    if (this.closed) throw new Lc3Error('TRANSPORT', 'Transport closed');
    const manager = getBleManager();
    for (const fragment of fragments) {
      await manager.writeCharacteristicWithResponseForDevice(
        this.device.id,
        LC3_SERVICE_UUID,
        LC3_COMMAND_UUID,
        bytesToBase64(fragment),
      );
    }
  }

  onFragment(handler: FragmentHandler): () => void {
    this.handlers.add(handler);
    return () => this.handlers.delete(handler);
  }

  startMonitor(): void {
    const manager = getBleManager();
    this.monitor = manager.monitorCharacteristicForDevice(
      this.device.id,
      LC3_SERVICE_UUID,
      LC3_EVENT_UUID,
      (error, characteristic) => {
        if (error) {
          lc3Log('notify', { error: error.errorCode });
          return;
        }
        if (!characteristic?.value) return;
        const bytes = base64ToBytes(characteristic.value);
        for (const handler of this.handlers) handler(bytes);
      },
    );
  }

  async close(): Promise<void> {
    this.closed = true;
    this.monitor?.remove();
    this.monitor = null;
    this.handlers.clear();
  }
}

export async function openBleTransport(device: Device): Promise<BleGattTransport> {
  const manager = getBleManager();
  let attMtu = DEFAULT_ATT_MTU;
  try {
    const updated = await manager.requestMTUForDevice(device.id, 512);
    attMtu = updated.mtu || attMtu;
  } catch {
    attMtu = device.mtu || DEFAULT_ATT_MTU;
  }
  lc3Log('mtu', { attMtu });
  await manager.discoverAllServicesAndCharacteristicsForDevice(device.id);
  const transport = new BleGattTransport(device, Math.max(attMtu, DEFAULT_ATT_MTU));
  transport.startMonitor();
  return transport;
}
