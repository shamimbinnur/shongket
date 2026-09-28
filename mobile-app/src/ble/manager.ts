import { BleManager, State } from '@sfourdrinier/react-native-ble-plx';

let manager: BleManager | null = null;

export function getBleManager(): BleManager {
  if (!manager) {
    manager = new BleManager();
  }
  return manager;
}

export async function waitForBlePoweredOn(timeoutMs = 8000): Promise<void> {
  const ble = getBleManager();
  const current = await ble.state();
  if (current === State.PoweredOn) return;
  await new Promise<void>((resolve, reject) => {
    const timer = setTimeout(() => {
      subscription.remove();
      reject(new Error('Bluetooth is not powered on'));
    }, timeoutMs);
    const subscription = ble.onStateChange((state) => {
      if (state === State.PoweredOn) {
        clearTimeout(timer);
        subscription.remove();
        resolve();
      }
    }, true);
  });
}

export function bleSupported(): boolean {
  return true;
}
