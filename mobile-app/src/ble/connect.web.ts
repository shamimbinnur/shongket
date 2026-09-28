import type { Lc3Transport } from '@/lc3/transport';

export async function connectHandheld(_deviceId: string): Promise<{
  transport: Lc3Transport;
  name: string;
  address: number | null;
  deviceId: string;
}> {
  throw new Error('BLE is not available on web');
}

export function watchDisconnect(_deviceId: string, _onDisconnect: () => void): () => void {
  return () => undefined;
}

export async function cancelConnection(_deviceId: string): Promise<void> {}

export function stopNativeScan(): void {}

export function startNativeScan(
  _onDevice: (id: string, name: string | null, rssi: number | null) => void,
  _onError: (code: string | number) => void,
): () => void {
  return () => undefined;
}
