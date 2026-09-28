export function getBleManager(): never {
  throw new Error('BLE is not available on web');
}

export async function waitForBlePoweredOn(): Promise<never> {
  throw new Error('BLE is not available on web');
}

export function bleSupported(): boolean {
  return false;
}
