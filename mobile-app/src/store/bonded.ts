import * as SecureStore from 'expo-secure-store';

const KEY = 'ontora.lastDevice';

export type BondedDevice = {
  id: string;
  name: string;
  address: number;
};

export async function loadBondedDevice(): Promise<BondedDevice | null> {
  try {
    const raw = await SecureStore.getItemAsync(KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as BondedDevice;
    if (!parsed?.id) return null;
    return parsed;
  } catch {
    return null;
  }
}

export async function saveBondedDevice(device: BondedDevice): Promise<void> {
  await SecureStore.setItemAsync(KEY, JSON.stringify(device));
}

export async function clearBondedDevice(): Promise<void> {
  await SecureStore.deleteItemAsync(KEY);
}
