import {
  MAX_REQUEST_FRAME_ID,
  MIN_EVENT_FRAME_ID,
  MIN_REQUEST_FRAME_ID,
  REQUEST_TIMEOUT_MS,
} from '@/lc3/constants';
import { Lc3ApiError, Lc3Error, Lc3TimeoutError } from '@/lc3/errors';
import { FrameAssembler, encodeFrame } from '@/lc3/framing';
import { lc3Log } from '@/lc3/log';
import type { Lc3Transport } from '@/lc3/transport';
import type {
  GpsData,
  HelloData,
  Lc3Event,
  Lc3Op,
  MessageDetail,
  MessagePage,
  NodeData,
  PeerDetail,
  PeerPage,
  PingData,
  SendResult,
  StatusData,
} from '@/lc3/types';

type Pending = {
  op: Lc3Op;
  resolve: (value: unknown) => void;
  reject: (error: unknown) => void;
  timer: ReturnType<typeof setTimeout>;
};

export type EventListener = (event: Lc3Event) => void;
export type SeqGapListener = (expected: number, actual: number) => void;

function numberOrUndefined(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined;
}

function normalizePeerSummary(value: unknown): PeerPage['peers'][number] {
  const item = value && typeof value === 'object' ? value as Record<string, unknown> : {};
  return { address: Number(item.address), name: String(item.name ?? item.address ?? 'Unknown'), online: Boolean(item.online), conflict: Boolean(item.conflict), hop: Number(item.hop ?? 0), rssi: Number(item.rssi ?? 0), locationAvailable: Boolean(item.locationAvailable ?? item.location) };
}

function normalizeMessageSummary(value: unknown): MessagePage['messages'][number] {
  const item = value && typeof value === 'object' ? value as Record<string, unknown> : {};
  return { origin: Number(item.origin), id: Number(item.id), dir: item.dir as MessagePage['messages'][number]['dir'], kind: item.kind as MessagePage['messages'][number]['kind'], peerAddress: Number(item.peerAddress ?? item.peer), peerName: String(item.peerName ?? item.name ?? 'Unknown'), preview: String(item.preview ?? ''), unread: Boolean(item.unread), delivery: (item.delivery === 'received' ? 'none' : item.delivery) as MessagePage['messages'][number]['delivery'], ageSeconds: numberOrUndefined(item.ageSeconds) };
}

function isEventDoc(value: unknown): value is Lc3Event {
  if (!value || typeof value !== 'object') return false;
  const doc = value as Record<string, unknown>;
  return doc.v === 1 && typeof doc.event === 'string' && typeof doc.seq === 'number';
}

export class Lc3Client {
  private nextId = MIN_REQUEST_FRAME_ID;
  private readonly pending = new Map<number, Pending>();
  private readonly assembler = new FrameAssembler();
  private readonly eventListeners = new Set<EventListener>();
  private readonly seqGapListeners = new Set<SeqGapListener>();
  private unsubscribeTransport: (() => void) | null = null;
  private lastSeq: number | null = null;
  private lastGpsLogAt = 0;
  private closed = false;

  constructor(
    private readonly transport: Lc3Transport,
    private readonly timeoutMs: number = REQUEST_TIMEOUT_MS,
  ) {
    this.unsubscribeTransport = transport.onFragment((packet) => this.handleIncoming(packet));
  }

  get lastEventSeq(): number | null {
    return this.lastSeq;
  }

  onEvent(listener: EventListener): () => void {
    this.eventListeners.add(listener);
    return () => this.eventListeners.delete(listener);
  }

  onSeqGap(listener: SeqGapListener): () => void {
    this.seqGapListeners.add(listener);
    return () => this.seqGapListeners.delete(listener);
  }

  async hello(): Promise<HelloData> {
    const data = await this.request<Record<string, unknown>>('hello');
    if ('apiVersion' in data) return data as HelloData;
    return { apiVersion: Number(data.api ?? 1), protocolVersion: String(data.protocol ?? 'LC3'), bootSession: String(data.session ?? ''), eventSeq: Number(data.eventSeq ?? 0), peerRevision: Number(data.peerRevision ?? 0), messageRevision: Number(data.messageRevision ?? 0), capabilities: Array.isArray(data.capabilities) ? data.capabilities.map(String) : [] };
  }

  async getNode(): Promise<NodeData> {
    const data = await this.request<Record<string, unknown>>('get_node');
    if ('onlineCount' in data) return data as NodeData;
    return { address: Number(data.address), name: String(data.name ?? 'LC3'), fingerprint: String(data.hardwareId ?? ''), crewId: String(data.crewId ?? ''), uptime: Number(data.uptime ?? 0), radio: Boolean(data.radio), crypto: Boolean(data.crypto), config: Boolean(data.config), conflict: Boolean(data.addressConflict), onlineCount: Number(data.online ?? 1), unreadCount: Number(data.unread ?? 0), phoneSecure: Boolean(data.phoneConnected) };
  }

  async listPeers(offset = 0, limit = 3): Promise<PeerPage> {
    const data = await this.request<Record<string, unknown>>('list_peers', { offset, limit });
    const rawItems = Array.isArray(data.items) ? data.items : Array.isArray(data.peers) ? data.peers : [];
    return { revision: Number(data.revision ?? 0), total: Number(data.total ?? rawItems.length), nextOffset: typeof data.nextOffset === 'number' ? data.nextOffset : undefined, peers: rawItems.map(normalizePeerSummary) };
  }

  async getPeer(address: number): Promise<PeerDetail> {
    const data = await this.request<Record<string, unknown>>('get_peer', { address });
    const summary = normalizePeerSummary(data);
    const rawLocation = data.location && typeof data.location === 'object' ? data.location as Record<string, unknown> : null;
    return { ...summary, location: rawLocation && rawLocation.available !== false ? { valid: rawLocation.valid === undefined ? Boolean(rawLocation.available ?? true) : Boolean(rawLocation.valid), current: Boolean(rawLocation.current), age: numberOrUndefined(rawLocation.age), lat: numberOrUndefined(rawLocation.lat), lon: numberOrUndefined(rawLocation.lon), distanceM: numberOrUndefined(rawLocation.distanceM), bearing: numberOrUndefined(rawLocation.bearing) } : undefined };
  }

  async listMessages(offset = 0, limit = 2): Promise<MessagePage> {
    const data = await this.request<Record<string, unknown>>('list_messages', { offset, limit });
    const rawItems = Array.isArray(data.items) ? data.items : Array.isArray(data.messages) ? data.messages : [];
    return { revision: Number(data.revision ?? 0), total: Number(data.total ?? rawItems.length), nextOffset: typeof data.nextOffset === 'number' ? data.nextOffset : undefined, messages: rawItems.map(normalizeMessageSummary) };
  }

  async getMessage(origin: number, id: number): Promise<MessageDetail> {
    const data = await this.request<Record<string, unknown>>('get_message', { origin, id });
    return { ...normalizeMessageSummary(data), text: String(data.text ?? ''), source: data.source as MessageDetail['source'], time: Number(data.time ?? 0) };
  }

  async getGps(): Promise<GpsData> {
    const data = await this.request<Record<string, unknown>>('get_gps');
    return { state: data.state as GpsData['state'], valid: Boolean(data.valid), current: Boolean(data.current), satellites: Number(data.satellites ?? 0), fixAgeMs: Number(data.fixAgeMs ?? data.ageMs ?? 0), lat: numberOrUndefined(data.lat), lon: numberOrUndefined(data.lon) };
  }

  async getStatus(): Promise<StatusData> {
    return this.request('get_status');
  }

  async sendMessage(destination: number, text: string): Promise<SendResult> {
    return this.request('send_message', { destination, text });
  }

  async markRead(origin: number, id: number): Promise<Record<string, never>> {
    return this.request('mark_read', { origin, id });
  }

  async markAllRead(): Promise<Record<string, never>> {
    return this.request('mark_all_read');
  }

  async ping(echo?: unknown): Promise<PingData> {
    return this.request('ping', echo === undefined ? {} : { echo });
  }

  async close(): Promise<void> {
    if (this.closed) return;
    this.closed = true;
    this.unsubscribeTransport?.();
    this.unsubscribeTransport = null;
    for (const [frameId, pending] of this.pending) {
      clearTimeout(pending.timer);
      pending.reject(new Lc3Error('TRANSPORT', 'Client closed'));
      this.pending.delete(frameId);
    }
    this.assembler.reset();
    await this.transport.close();
  }

  private nextRequestId(): number {
    const id = this.nextId;
    this.nextId = this.nextId >= MAX_REQUEST_FRAME_ID ? MIN_REQUEST_FRAME_ID : this.nextId + 1;
    return id;
  }

  async request<T = unknown>(op: Lc3Op, args: Record<string, unknown> = {}): Promise<T> {
    if (this.closed) {
      throw new Lc3Error('TRANSPORT', 'Client closed');
    }
    const frameId = this.nextRequestId();
    const body = JSON.stringify({ v: 1, op, args });
    const fragments = encodeFrame(body, frameId, this.transport.getAttMtu());
    lc3Log(op, { frameId, fragments: fragments.length });

    const result = new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(frameId);
        lc3Log(op, { frameId, error: 'TIMEOUT' });
        reject(new Lc3TimeoutError(op, frameId));
      }, this.timeoutMs);
      this.pending.set(frameId, {
        op,
        resolve: (value) => resolve(value as T),
        reject,
        timer,
      });
    });

    try {
      // Start observing the response timeout before waiting for the BLE write.
      // Some Android stacks can keep writeFragments pending longer than the
      // request timeout; awaiting the write first leaves `result` temporarily
      // unhandled when its timer rejects.
      const [, response] = await Promise.all([
        this.transport.writeFragments(fragments),
        result,
      ]);
      return response;
    } catch (error) {
      const pending = this.pending.get(frameId);
      if (pending) {
        clearTimeout(pending.timer);
        this.pending.delete(frameId);
      }
      throw error instanceof Lc3Error ? error : new Lc3Error('TRANSPORT', 'Write failed');
    }
  }

  private handleIncoming(packet: Uint8Array): void {
    let assembled;
    try {
      assembled = this.assembler.push(packet);
    } catch {
      lc3Log('framing', { error: 'FRAMING' });
      return;
    }
    if (!assembled) return;

    let doc: unknown;
    try {
      doc = JSON.parse(assembled.json);
    } catch {
      lc3Log('json', { frameId: assembled.frameId, error: 'INVALID_JSON' });
      return;
    }

    if (isEventDoc(doc)) {
      this.dispatchEvent(doc, assembled.frameId);
      return;
    }

    const pending = this.pending.get(assembled.frameId);
    if (!pending) {
      lc3Log('orphan', { frameId: assembled.frameId });
      return;
    }
    clearTimeout(pending.timer);
    this.pending.delete(assembled.frameId);

    if (!doc || typeof doc !== 'object') {
      pending.reject(new Lc3ApiError('INVALID_JSON', 'Empty response'));
      return;
    }
    const response = doc as Record<string, unknown>;
    if (response.ok === false) {
      const error = (response.error ?? {}) as { code?: string; message?: string };
      const code = error.code ?? 'UNKNOWN_OPERATION';
      lc3Log(pending.op, { frameId: assembled.frameId, error: code });
      pending.reject(new Lc3ApiError(code, error.message ?? code));
      return;
    }
    pending.resolve(response.data ?? {});
  }

  private dispatchEvent(event: Lc3Event, frameId: number): void {
    if (frameId < MIN_EVENT_FRAME_ID) {
      lc3Log('event', { event: event.event, frameId, error: 'bad-event-id' });
    }
    if (this.lastSeq !== null && event.seq > this.lastSeq + 1) {
      for (const listener of this.seqGapListeners) {
        listener(this.lastSeq + 1, event.seq);
      }
    }
    this.lastSeq = event.seq;
    const now = Date.now();
    if (event.event !== 'gps_changed' || now - this.lastGpsLogAt >= 5_000) {
      lc3Log('event', { event: event.event, seq: event.seq, frameId });
      if (event.event === 'gps_changed') this.lastGpsLogAt = now;
    }
    for (const listener of this.eventListeners) {
      listener(event);
    }
  }
}
