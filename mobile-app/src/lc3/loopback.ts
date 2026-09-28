import { DEFAULT_ATT_MTU } from '@/lc3/constants';
import { FrameAssembler, encodeFrame } from '@/lc3/framing';
import type { FragmentHandler, Lc3Transport } from '@/lc3/transport';

/** In-process framed pipe used by the mock handheld and unit tests. */
export class LoopbackTransport implements Lc3Transport {
  readonly kind = 'mock' as const;
  private readonly handlers = new Set<FragmentHandler>();
  private readonly inbound = new FrameAssembler();
  private closed = false;
  attMtu: number;

  constructor(
    private readonly onRequest: (json: string, frameId: number) => string | Promise<string>,
    attMtu: number = DEFAULT_ATT_MTU,
  ) {
    this.attMtu = attMtu;
  }

  getAttMtu(): number {
    return this.attMtu;
  }

  async writeFragments(fragments: Uint8Array[]): Promise<void> {
    if (this.closed) throw new Error('Transport closed');
    for (const fragment of fragments) {
      const assembled = this.inbound.push(fragment);
      if (!assembled) continue;
      const reply = await this.onRequest(assembled.json, assembled.frameId);
      this.emitJson(reply, assembled.frameId);
    }
  }

  emitJson(json: string, frameId: number): void {
    const packets = encodeFrame(json, frameId, this.attMtu);
    queueMicrotask(() => {
      for (const packet of packets) {
        for (const handler of this.handlers) handler(packet);
      }
    });
  }

  onFragment(handler: FragmentHandler): () => void {
    this.handlers.add(handler);
    return () => this.handlers.delete(handler);
  }

  async close(): Promise<void> {
    this.closed = true;
    this.handlers.clear();
    this.inbound.reset();
  }
}
