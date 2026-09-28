import {
  CREW_ADDRESS,
  DEFAULT_ATT_MTU,
  MAX_MESSAGE_TEXT,
  MESSAGE_PAGE_LIMIT,
  MIN_EVENT_FRAME_ID,
  PEER_PAGE_LIMIT,
  PRINTABLE_ASCII_RE,
} from '@/lc3/constants';
import { LoopbackTransport } from '@/lc3/loopback';
import type {
  DeliveryState,
  GpsData,
  HelloData,
  Lc3Op,
  MessageDetail,
  MessageSummary,
  NodeData,
  PeerDetail,
  StatusData,
} from '@/lc3/types';

type MockPeer = PeerDetail;
type MockMessage = MessageDetail;

function ok(data: unknown): string {
  return JSON.stringify({ v: 1, ok: true, data });
}

function fail(code: string, message: string): string {
  return JSON.stringify({ v: 1, ok: false, error: { code, message } });
}

function toSummary(message: MockMessage): MessageSummary {
  const { text: _text, source: _source, time: _time, ...summary } = message;
  return summary;
}

export class MockHandheld {
  bootSession = 'a1b2c3d4e5f600';
  eventSeq = 0;
  peerRevision = 1;
  messageRevision = 1;
  inflight: MockMessage | null = null;
  private eventFrameId = MIN_EVENT_FRAME_ID;
  readonly transport: LoopbackTransport;

  node: NodeData = {
    address: 1,
    name: 'ALPHA',
    fingerprint: 'aabbccddeeff',
    crewId: 'CREW-7',
    uptime: 3600,
    radio: 'ready',
    crypto: 'ready',
    config: 'ok',
    conflict: false,
    onlineCount: 4,
    unreadCount: 1,
    phoneSecure: true,
    delivery: 'none',
  };

  gps: GpsData = {
    state: 'FIX',
    valid: true,
    current: true,
    satellites: 9,
    fixAgeMs: 800,
    lat: 37.7749,
    lon: -122.4194,
  };

  status: StatusData = {
    lora: { spreadingFactor: 8, bandwidth: 125, txPower: 17 },
    mesh: { tx: 128, rx: 410, errors: 2 },
    ble: { queueDrops: 0 },
    lastPacket: { rssi: -87, snr: 4, ageMs: 1200 },
  };

  peers: MockPeer[] = [
    {
      address: 2,
      name: 'BRAVO',
      online: true,
      conflict: false,
      hop: 1,
      rssi: -62,
      locationAvailable: true,
      location: { current: true, valid: true, lat: 37.7755, lon: -122.418, distanceM: 142, bearing: 48 },
    },
    {
      address: 3,
      name: 'CHARLIE',
      online: true,
      conflict: false,
      hop: 2,
      rssi: -91,
      locationAvailable: true,
      location: { current: false, valid: true, age: 90, lat: 37.77, lon: -122.43 },
    },
    {
      address: 4,
      name: 'DELTA',
      online: false,
      conflict: false,
      hop: 3,
      rssi: -110,
      locationAvailable: false,
    },
    {
      address: 5,
      name: 'ECHO',
      online: true,
      conflict: true,
      hop: 1,
      rssi: -70,
      locationAvailable: false,
    },
  ];

  messages: MockMessage[] = [
    {
      origin: 2,
      id: 101,
      dir: 'in',
      kind: 'direct',
      peerAddress: 2,
      peerName: 'BRAVO',
      preview: 'Meet at gate 2',
      unread: true,
      delivery: 'none',
      ageSeconds: 100,
      text: 'Meet at gate 2',
      source: 'radio',
      time: 3500,
    },
    {
      origin: 1,
      id: 44,
      dir: 'out',
      kind: 'crew',
      peerAddress: CREW_ADDRESS,
      peerName: 'CREW',
      preview: 'Radio check',
      unread: false,
      delivery: 'delivered',
      ageSeconds: 1200,
      text: 'Radio check',
      source: 'keypad',
      time: 2400,
    },
    {
      origin: 3,
      id: 12,
      dir: 'in',
      kind: 'crew',
      peerAddress: 3,
      peerName: 'CHARLIE',
      preview: 'North trail clear',
      unread: false,
      delivery: 'none',
      ageSeconds: 2500,
      text: 'North trail clear',
      source: 'radio',
      time: 1100,
    },
  ];

  constructor(attMtu: number = DEFAULT_ATT_MTU) {
    this.transport = new LoopbackTransport((json, frameId) => this.handle(json, frameId), attMtu);
  }

  private nextEventFrameId(): number {
    const id = this.eventFrameId;
    this.eventFrameId = this.eventFrameId >= 0xffff ? MIN_EVENT_FRAME_ID : this.eventFrameId + 1;
    return id;
  }

  emit(event: string, data: unknown): void {
    this.eventSeq += 1;
    this.transport.emitJson(
      JSON.stringify({ v: 1, event, seq: this.eventSeq, data }),
      this.nextEventFrameId(),
    );
  }

  private handle(json: string, _frameId: number): string {
    let doc: { v?: number; op?: Lc3Op; args?: Record<string, unknown> };
    try {
      doc = JSON.parse(json);
    } catch {
      return fail('INVALID_JSON', 'JSON could not be parsed');
    }
    if (doc.v !== 1) return fail('UNSUPPORTED_VERSION', 'v is not 1');
    const args = doc.args ?? {};
    switch (doc.op) {
      case 'hello':
        return ok(this.hello());
      case 'get_node':
        return ok({ ...this.node, unreadCount: this.unreadCount(), phoneSecure: true });
      case 'list_peers':
        return this.listPeers(args);
      case 'get_peer':
        return this.getPeer(args);
      case 'list_messages':
        return this.listMessages(args);
      case 'get_message':
        return this.getMessage(args);
      case 'get_gps':
        return ok(this.gps);
      case 'get_status':
        return ok(this.status);
      case 'send_message':
        return this.sendMessage(args);
      case 'mark_read':
        return this.markRead(args);
      case 'mark_all_read':
        return this.markAllRead();
      case 'ping':
        return ok({ uptime: this.node.uptime, echo: args.echo });
      default:
        return fail('UNKNOWN_OPERATION', 'op is not implemented');
    }
  }

  private hello(): HelloData {
    return {
      apiVersion: 1,
      protocolVersion: 1,
      bootSession: this.bootSession,
      eventSeq: this.eventSeq,
      peerRevision: this.peerRevision,
      messageRevision: this.messageRevision,
      capabilities: ['peers', 'messages', 'gps', 'status'],
    };
  }

  private unreadCount(): number {
    return this.messages.filter((message) => message.unread).length;
  }

  private listPeers(args: Record<string, unknown>): string {
    const offset = Number(args.offset ?? 0);
    const limit = Math.min(PEER_PAGE_LIMIT, Math.max(1, Number(args.limit ?? PEER_PAGE_LIMIT)));
    const sorted = [...this.peers].sort((a, b) => {
      if (a.online !== b.online) return a.online ? -1 : 1;
      return a.address - b.address;
    });
    const slice = sorted.slice(offset, offset + limit);
    const nextOffset = offset + slice.length < sorted.length ? offset + slice.length : undefined;
    return ok({
      revision: this.peerRevision,
      total: sorted.length,
      nextOffset,
      peers: slice.map(({ location: _location, ...summary }) => summary),
    });
  }

  private getPeer(args: Record<string, unknown>): string {
    const address = Number(args.address);
    const peer = this.peers.find((item) => item.address === address);
    if (!peer) return fail('NOT_FOUND', 'Peer not found');
    return ok(peer);
  }

  private listMessages(args: Record<string, unknown>): string {
    const offset = Number(args.offset ?? 0);
    const limit = Math.min(MESSAGE_PAGE_LIMIT, Math.max(1, Number(args.limit ?? MESSAGE_PAGE_LIMIT)));
    const sorted = [...this.messages].sort((a, b) => b.time - a.time);
    const slice = sorted.slice(offset, offset + limit);
    const nextOffset = offset + slice.length < sorted.length ? offset + slice.length : undefined;
    return ok({
      revision: this.messageRevision,
      total: sorted.length,
      nextOffset,
      messages: slice.map(toSummary),
    });
  }

  private getMessage(args: Record<string, unknown>): string {
    const origin = Number(args.origin);
    const id = Number(args.id);
    const message = this.messages.find((item) => item.origin === origin && item.id === id);
    if (!message) return fail('NOT_FOUND', 'Message not found');
    return ok(message);
  }

  private sendMessage(args: Record<string, unknown>): string {
    if (this.inflight) return fail('BUSY', 'another local message is awaiting delivery');
    const destination = Number(args.destination);
    const text = String(args.text ?? '');
    if (!text || text.length > MAX_MESSAGE_TEXT || !PRINTABLE_ASCII_RE.test(text)) {
      return fail('INVALID_TEXT', 'text is empty, over 80 bytes, or not printable ASCII');
    }
    if (destination === this.node.address) {
      return fail('INVALID_DESTINATION', 'address is reserved, local, or invalid');
    }
    if (destination !== CREW_ADDRESS) {
      const peer = this.peers.find((item) => item.address === destination);
      if (!peer) return fail('PEER_OFFLINE', 'direct recipient is unknown or offline');
      if (!peer.online) return fail('PEER_OFFLINE', 'direct recipient is unknown or offline');
      if (peer.conflict) return fail('ADDRESS_CONFLICT', 'destination address conflicts');
    }

    const id = Math.max(0, ...this.messages.map((message) => message.id)) + 1;
    const peer = this.peers.find((item) => item.address === destination);
    const message: MockMessage = {
      origin: this.node.address,
      id,
      dir: 'out',
      kind: destination === CREW_ADDRESS ? 'crew' : 'direct',
      peerAddress: destination,
      peerName: destination === CREW_ADDRESS ? 'CREW' : (peer?.name ?? String(destination)),
      preview: text.slice(0, 24),
      unread: false,
      delivery: 'sending',
      ageSeconds: 0,
      text,
      source: 'mobile',
      time: this.node.uptime,
    };
    this.inflight = message;
    this.messages.unshift(message);
    this.messageRevision += 1;
    this.node.delivery = 'sending';

    queueMicrotask(() => this.emit('messages_changed', {
      revision: this.messageRevision,
      count: this.messages.length,
      unread: this.unreadCount(),
    }));
    queueMicrotask(() => this.emit('node_changed', {
      unreadCount: this.unreadCount(),
      delivery: 'sending',
    }));

    setTimeout(() => {
      if (this.inflight?.id !== id) return;
      message.delivery = 'delivered';
      this.inflight = null;
      this.node.delivery = 'delivered';
      this.messageRevision += 1;
      this.emit('messages_changed', {
        revision: this.messageRevision,
        count: this.messages.length,
        unread: this.unreadCount(),
      });
      this.emit('node_changed', { delivery: 'delivered', unreadCount: this.unreadCount() });
    }, 400);

    return ok({ messageId: id, state: 'sending' satisfies DeliveryState });
  }

  private markRead(args: Record<string, unknown>): string {
    const origin = Number(args.origin);
    const id = Number(args.id);
    const message = this.messages.find((item) => item.origin === origin && item.id === id);
    if (!message) return fail('NOT_FOUND', 'Message not found');
    message.unread = false;
    this.node.unreadCount = this.unreadCount();
    this.messageRevision += 1;
    queueMicrotask(() => this.emit('messages_changed', {
      revision: this.messageRevision,
      count: this.messages.length,
      unread: this.unreadCount(),
    }));
    return ok({});
  }

  private markAllRead(): string {
    for (const message of this.messages) message.unread = false;
    this.node.unreadCount = 0;
    this.messageRevision += 1;
    queueMicrotask(() => this.emit('messages_changed', {
      revision: this.messageRevision,
      count: this.messages.length,
      unread: 0,
    }));
    return ok({});
  }
}

export function createMockTransport(attMtu?: number): { handheld: MockHandheld; transport: LoopbackTransport } {
  const handheld = new MockHandheld(attMtu);
  return { handheld, transport: handheld.transport };
}
