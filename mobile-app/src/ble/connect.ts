import { ConnectionManager } from '@sfourdrinier/react-native-ble-plx';

import { openBleTransport } from '@/ble/gatt-transport';
import { getBleManager } from '@/ble/manager';
import { parseAdvertisedName } from '@/lc3/text';
import type { Lc3Transport } from '@/lc3/transport';

let connectionManager: ConnectionManager | null = null;

function manager(): ConnectionManager {
  if (!connectionManager) connectionManager = new ConnectionManager(getBleManager());
  return connectionManager;
}

export async function connectHandheld(deviceId: string): Promise<{
  transport: Lc3Transport;
  name: string;
  address: number | null;
  deviceId: string;
}> {
  const device = await manager().connect(deviceId, { maxRetries: 2, timeoutMs: 20000 });
  const transport = await openBleTransport(device);
  const parsed = parseAdvertisedName(device.name ?? device.localName);
  return {
    transport,
    name: device.name ?? parsed?.nodeName ?? 'LC3',
    address: parsed?.address ?? null,
    deviceId: device.id,
  };
}

export function watchDisconnect(deviceId: string, onDisconnect: () => void): () => void {
  const subscription = getBleManager().onDeviceDisconnected(deviceId, () => onDisconnect());
  return () => subscription.remove();
}

export async function cancelConnection(deviceId: string): Promise<void> {
  try {
    await getBleManager().cancelDeviceConnection(deviceId);
  } catch {
    /* already gone */
  }
}

export function stopNativeScan(): void {
  try {
    getBleManager().stopDeviceScan();
  } catch {
    /* not scanning */
  }
}

export function startNativeScan(
  onDevice: (id: string, name: string | null, rssi: number | null) => void,
  onError: (code: string | number) => void,
): () => void {
  const ble = getBleManager();
  ble.startDeviceScan(null, { allowDuplicates: true }, (error, device) => {
    if (error) {
      onError(error.errorCode);
      return;
    }
    if (!device) return;
    onDevice(device.id, device.name ?? device.localName ?? null, device.rssi);
  });
  return () => ble.stopDeviceScan();
}
