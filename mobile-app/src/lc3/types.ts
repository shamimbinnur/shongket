export type Lc3Op =
  | 'hello'
  | 'get_node'
  | 'list_peers'
  | 'get_peer'
  | 'list_messages'
  | 'get_message'
  | 'get_gps'
  | 'get_status'
  | 'send_message'
  | 'mark_read'
  | 'mark_all_read'
  | 'ping';

export type Lc3ErrorCode =
  | 'INVALID_JSON'
  | 'UNSUPPORTED_VERSION'
  | 'UNKNOWN_OPERATION'
  | 'NOT_FOUND'
  | 'BUSY'
  | 'RADIO_UNAVAILABLE'
  | 'ADDRESS_CONFLICT'
  | 'INVALID_DESTINATION'
  | 'PEER_OFFLINE'
  | 'INVALID_TEXT'
  | 'RESPONSE_TOO_LARGE'
  | 'TIMEOUT'
  | 'TRANSPORT'
  | 'FRAMING';

export type MessageDir = 'in' | 'out';
export type MessageKind = 'direct' | 'crew';
export type MessageSource = 'radio' | 'keypad' | 'mobile';
export type DeliveryState = 'sending' | 'delivered' | 'failed' | 'none';
export type GpsFixState = 'FIX' | 'STALE' | 'SEARCH';

export type Lc3Request = {
  v: 1;
  op: Lc3Op;
  args: Record<string, unknown>;
};

export type Lc3Success<T> = {
  v: 1;
  ok: true;
  data: T;
};

export type Lc3Failure = {
  v: 1;
  ok: false;
  error: {
    code: Lc3ErrorCode | string;
    message: string;
  };
};

export type Lc3Response<T> = Lc3Success<T> | Lc3Failure;

export type HelloData = {
  apiVersion: number;
  protocolVersion: number | string;
  bootSession: string;
  eventSeq: number;
  peerRevision: number;
  messageRevision: number;
  capabilities: string[];
};

export type NodeData = {
  address: number;
  name: string;
  fingerprint: string;
  crewId: string | number;
  uptime: number;
  radio: string | boolean;
  crypto: string | boolean;
  config: string | boolean;
  conflict: boolean;
  onlineCount: number;
  unreadCount: number;
  phoneSecure: boolean;
  delivery?: DeliveryState;
};

export type PeerSummary = {
  address: number;
  name: string;
  online: boolean;
  conflict: boolean;
  hop: number;
  rssi: number;
  locationAvailable: boolean;
};

export type PeerPage = {
  revision: number;
  total: number;
  nextOffset?: number;
  peers: PeerSummary[];
};

export type PeerLocation = {
  valid?: boolean;
  current: boolean;
  age?: number;
  lat?: number;
  lon?: number;
  distanceM?: number;
  bearing?: number;
};

export type PeerDetail = PeerSummary & {
  location?: PeerLocation;
};

export type MessageSummary = {
  origin: number;
  id: number;
  dir: MessageDir;
  kind: MessageKind;
  peerAddress: number;
  peerName: string;
  preview: string;
  unread: boolean;
  delivery: DeliveryState;
  ageSeconds?: number;
};

export type MessagePage = {
  revision: number;
  total: number;
  nextOffset?: number;
  messages: MessageSummary[];
};

export type MessageDetail = MessageSummary & {
  text: string;
  source: MessageSource;
  time: number;
};

export type GpsData = {
  state: GpsFixState;
  valid: boolean;
  current: boolean;
  satellites: number;
  fixAgeMs: number;
  lat?: number;
  lon?: number;
};

export type StatusData = {
  lora?: Record<string, unknown>;
  mesh?: Record<string, unknown>;
  ble?: Record<string, unknown>;
  lastPacket?: Record<string, unknown>;
};

export type SendResult = {
  messageId: number;
  state: DeliveryState;
};

export type PingData = {
  uptime: number;
  echo?: unknown;
};

export type PeersChangedData = {
  revision: number;
  onlineCount: number;
};

export type MessagesChangedData = {
  revision: number;
  count: number;
  unread: number;
};

export type NodeChangedData = {
  onlineCount?: number;
  unreadCount?: number;
  delivery?: DeliveryState;
  radio?: string;
  conflict?: boolean;
};

export type Lc3EventName =
  | 'peers_changed'
  | 'messages_changed'
  | 'gps_changed'
  | 'node_changed'
  | 'resync_required';

export type Lc3Event =
  | { v: 1; event: 'peers_changed'; seq: number; data: PeersChangedData }
  | { v: 1; event: 'messages_changed'; seq: number; data: MessagesChangedData }
  | { v: 1; event: 'gps_changed'; seq: number; data: GpsData }
  | { v: 1; event: 'node_changed'; seq: number; data: NodeChangedData }
  | { v: 1; event: 'resync_required'; seq: number; data: Record<string, unknown> };

export type DiscoveredHandheld = {
  id: string;
  name: string;
  address: number;
  rssi: number | null;
};
