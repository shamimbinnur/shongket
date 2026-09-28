export type FragmentHandler = (packet: Uint8Array) => void;

export interface Lc3Transport {
  readonly kind: 'ble' | 'mock';
  getAttMtu(): number;
  writeFragments(fragments: Uint8Array[]): Promise<void>;
  onFragment(handler: FragmentHandler): () => void;
  close(): Promise<void>;
}
