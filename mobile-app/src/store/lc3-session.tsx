import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';

import {
  cancelConnection,
  connectHandheld,
  startNativeScan,
  stopNativeScan,
  watchDisconnect,
} from '@/ble/connect';
import { bleSupported, waitForBlePoweredOn } from '@/ble/manager';
import { requestBlePermissions } from '@/ble/permissions';
import { Lc3Client } from '@/lc3/client';
import { Lc3ApiError } from '@/lc3/errors';
import { lc3Log } from '@/lc3/log';
import { MockHandheld } from '@/lc3/mock';
import { fetchMessageSnapshot, fetchPeerSnapshot, fetchSnapshots } from '@/lc3/snapshot';
import { parseAdvertisedName } from '@/lc3/text';
import type { Lc3Transport } from '@/lc3/transport';
import type {
  DiscoveredHandheld,
  GpsData,
  HelloData,
  MessageDetail,
  MessageSummary,
  NodeData,
  PeerDetail,
  PeerSummary,
  StatusData,
} from '@/lc3/types';
import { type BondedDevice, loadBondedDevice, saveBondedDevice } from '@/store/bonded';

export type ConnectionState =
  | 'idle'
  | 'scanning'
  | 'connecting'
  | 'pairing'
  | 'ready'
  | 'disconnected'
  | 'unavailable';

export type SessionSnapshot = {
  connection: ConnectionState;
  transportKind: 'ble' | 'mock' | null;
  error: string | null;
  hint: string | null;
  discovered: DiscoveredHandheld[];
  lastDeviceName: string | null;
  hello: HelloData | null;
  node: NodeData | null;
  peers: PeerSummary[];
  peerTotal: number;
  peerRevision: number | null;
  messages: MessageSummary[];
  messageTotal: number;
  messageRevision: number | null;
  gps: GpsData | null;
  status: StatusData | null;
  selectedPeer: PeerDetail | null;
  selectedMessage: MessageDetail | null;
  sendBusy: boolean;
  bleAvailable: boolean;
};

type SessionApi = SessionSnapshot & {
  startScan: () => Promise<void>;
  stopScan: () => void;
  connectDevice: (deviceId: string) => Promise<void>;
  connectMock: () => Promise<void>;
  disconnect: () => Promise<void>;
  refreshAll: () => Promise<void>;
  loadPeer: (address: number) => Promise<PeerDetail | null>;
  loadMessage: (origin: number, id: number) => Promise<MessageDetail | null>;
  send: (destination: number, text: string) => Promise<void>;
  markRead: (origin: number, id: number) => Promise<void>;
  markAllRead: () => Promise<void>;
};

const empty: SessionSnapshot = {
  connection: 'idle',
  transportKind: null,
  error: null,
  hint: null,
  discovered: [],
  lastDeviceName: null,
  hello: null,
  node: null,
  peers: [],
  peerTotal: 0,
  peerRevision: null,
  messages: [],
  messageTotal: 0,
  messageRevision: null,
  gps: null,
  status: null,
  selectedPeer: null,
  selectedMessage: null,
  sendBusy: false,
  bleAvailable: bleSupported(),
};

const SessionContext = createContext<SessionApi | null>(null);

function userError(error: unknown, fallback: string): string {
  if (error instanceof Error) return error.message;
  return fallback;
}

export function Lc3SessionProvider({ children }: { children: ReactNode }) {
  const [snap, setSnap] = useState<SessionSnapshot>(empty);
  const clientRef = useRef<Lc3Client | null>(null);
  const deviceIdRef = useRef<string | null>(null);
  const scanStopRef = useRef<(() => void) | null>(null);
  const lastSentRef = useRef<{ origin: number; id: number } | null>(null);
  const unsubClientRef = useRef<(() => void) | null>(null);
  const unsubDisconnectRef = useRef<(() => void) | null>(null);
  const gpsTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const refreshRef = useRef<Promise<void> | null>(null);

  const patch = useCallback((update: Partial<SessionSnapshot> | ((prev: SessionSnapshot) => SessionSnapshot)) => {
    setSnap((prev) => (typeof update === 'function' ? update(prev) : { ...prev, ...update }));
  }, []);

  const teardownClient = useCallback(async () => {
    unsubClientRef.current?.();
    unsubClientRef.current = null;
    unsubDisconnectRef.current?.();
    unsubDisconnectRef.current = null;
    const client = clientRef.current;
    clientRef.current = null;
    lastSentRef.current = null;
    if (client) {
      try {
        await client.close();
      } catch {
        /* already closed */
      }
    }
  }, []);

  const applySnapshots = useCallback(
    async (client: Lc3Client) => {
      if (refreshRef.current) return refreshRef.current;
      const refresh = (async () => {
        const data = await fetchSnapshots(client);
        patch({
          hello: data.hello,
          node: data.node,
          peers: data.peers,
          peerTotal: data.peerTotal,
          peerRevision: data.peerRevision,
          messages: data.messages,
          messageTotal: data.messageTotal,
          messageRevision: data.messageRevision,
          gps: data.gps,
          status: data.status,
          sendBusy: data.node?.delivery === 'sending',
          connection: 'ready',
          error: null,
        });
      })();
      refreshRef.current = refresh;
      try {
        await refresh;
      } finally {
        if (refreshRef.current === refresh) refreshRef.current = null;
      }
    },
    [patch],
  );

  const bindClient = useCallback(
    (client: Lc3Client) => {
      const unsubEvent = client.onEvent((event) => {
        void (async () => {
          try {
            const eventData =
              event.data && typeof event.data === 'object'
                ? (event.data as Record<string, unknown>)
                : {};
            if (event.event === 'resync_required') {
              await applySnapshots(client);
              return;
            }
            if (event.event === 'peers_changed') {
              const page = await fetchPeerSnapshot(client);
              patch((prev) => ({
                ...prev,
                peers: page.peers,
                peerRevision: page.revision,
                peerTotal: page.total,
                node: prev.node
                  ? {
                      ...prev.node,
                      onlineCount:
                        typeof (eventData.onlineCount ?? eventData.online) === 'number'
                          ? Number(eventData.onlineCount ?? eventData.online)
                          : prev.node.onlineCount,
                    }
                  : prev.node,
              }));
              return;
            }
            if (event.event === 'messages_changed') {
              const page = await fetchMessageSnapshot(client);
              const inflight = lastSentRef.current;
              let selected = undefined as MessageDetail | undefined;
              let sendBusy = false;
              if (inflight) {
                const detail = await client.getMessage(inflight.origin, inflight.id);
                selected = detail;
                if (detail.delivery !== 'sending') {
                  lastSentRef.current = null;
                } else {
                  sendBusy = true;
                }
              }
              patch((prev) => ({
                ...prev,
                messages: page.messages,
                messageRevision: page.revision,
                messageTotal: page.total,
                sendBusy,
                selectedMessage: selected ?? prev.selectedMessage,
                node: prev.node
                  ? {
                      ...prev.node,
                      unreadCount:
                        typeof eventData.unread === 'number'
                          ? eventData.unread
                          : prev.node.unreadCount,
                    }
                  : prev.node,
              }));
              return;
            }
            if (event.event === 'gps_changed') {
              if (!gpsTimerRef.current) {
                  gpsTimerRef.current = setTimeout(() => {
                  gpsTimerRef.current = null;
                  void client.getGps().then((gps) => patch({ gps })).catch(() => undefined);
                }, 750);
              }
              return;
            }
            if (event.event === 'node_changed') {
              patch((prev) => ({
                ...prev,
                sendBusy: (typeof eventData.delivery === 'string' ? eventData.delivery.toLowerCase() : prev.node?.delivery) === 'sending',
                node: prev.node
                  ? {
                      ...prev.node,
                      onlineCount:
                        typeof eventData.onlineCount === 'number'
                          ? eventData.onlineCount
                          : prev.node.onlineCount,
                      unreadCount:
                        typeof (eventData.unreadCount ?? eventData.unread) === 'number'
                          ? Number(eventData.unreadCount ?? eventData.unread)
                          : prev.node.unreadCount,
                      delivery:
                        typeof eventData.delivery === 'string'
                          ? (eventData.delivery.toLowerCase() as NodeData['delivery'])
                          : prev.node.delivery,
                      radio:
                        typeof eventData.radio === 'string' || typeof eventData.radio === 'boolean' ? eventData.radio : prev.node.radio,
                      conflict:
                        typeof eventData.conflict === 'boolean'
                          ? eventData.conflict
                          : prev.node.conflict,
                    }
                  : prev.node,
              }));
            }
          } catch (error) {
            lc3Log('event', { error: 'resync' });
            patch({ error: userError(error, 'Failed to refresh after radio event') });
          }
        })();
      });
      const unsubGap = client.onSeqGap(() => {
        void applySnapshots(client).catch((error) => {
          patch({ error: userError(error, 'Failed to resync radio data.') });
        });
      });
      unsubClientRef.current = () => {
        unsubEvent();
        unsubGap();
      };
    },
    [applySnapshots, patch],
  );

  const attachTransport = useCallback(
    async (transport: Lc3Transport, kind: 'ble' | 'mock', deviceName: string | null) => {
      await teardownClient();
      const client = new Lc3Client(transport);
      clientRef.current = client;
      bindClient(client);
      patch({
        connection: 'pairing',
        transportKind: kind,
        lastDeviceName: deviceName,
        hint: kind === 'ble' ? 'Enter the six-digit code shown on the handheld screen.' : null,
      });
      await applySnapshots(client);
      patch({ hint: null, connection: 'ready' });
    },
    [applySnapshots, bindClient, patch, teardownClient],
  );

  const stopScan = useCallback(() => {
    scanStopRef.current?.();
    scanStopRef.current = null;
    stopNativeScan();
    setSnap((prev) => (prev.connection === 'scanning' ? { ...prev, connection: 'idle' } : prev));
  }, []);

  const startScan = useCallback(async () => {
    if (!bleSupported()) {
      patch({
        connection: 'unavailable',
        error: 'Bluetooth is not available. Use a development build on a phone.',
      });
      return;
    }
    const allowed = await requestBlePermissions();
    if (!allowed) {
      patch({ error: 'Allow Nearby devices and Location permissions to scan for the handheld.' });
      return;
    }
    try {
      await waitForBlePoweredOn();
    } catch {
      patch({ error: 'Turn Bluetooth on to scan for the handheld.' });
      return;
    }
    stopScan();
    patch({
      connection: 'scanning',
      error: null,
      discovered: [],
      hint: 'Scanning for CL3 handhelds. On the radio, open 4 Bluetooth → Pair new phone and press D.',
    });
    const seen = new Map<string, DiscoveredHandheld>();
    const stopNative = startNativeScan(
      (id, name, rssi) => {
        const parsed = parseAdvertisedName(name);
        if (!parsed) return;
        if (seen.has(id)) return;
        seen.set(id, {
          id,
          name: name ?? `CL3-${parsed.address}-${parsed.nodeName}`,
          address: parsed.address,
          rssi,
        });
        patch({ discovered: [...seen.values()] });
      },
      (code) => {
        lc3Log('scan', { error: String(code) });
        stopNativeScan();
        const message = code === 102
          ? 'Bluetooth turned off during the scan. Turn it on and try again.'
          : code === 601
            ? 'Location is off. Turn on the phone’s Location switch and try again.'
            : `Bluetooth scan failed (${String(code)}). Check Bluetooth and Location settings, then try again.`;
        patch({
          connection: 'idle',
          error: message,
        });
      },
    );
    const timer = setTimeout(() => {
      stopNative();
      scanStopRef.current = null;
      setSnap((prev) =>
        prev.connection === 'scanning'
          ? {
              ...prev,
              connection: 'idle',
              hint: prev.discovered.length
                ? null
                : 'No CL3 handheld found. Open 4 Bluetooth → Pair new phone, press D, and retry within 60 seconds.',
            }
          : prev,
      );
    }, 60_000);
    scanStopRef.current = () => {
      clearTimeout(timer);
      stopNative();
    };
  }, [patch, stopScan]);

  const connectDevice = useCallback(
    async (deviceId: string) => {
      if (!bleSupported()) return;
      stopScan();
      patch({
        connection: 'connecting',
        error: null,
        hint: 'Connecting. If asked, enter the passkey from the handheld TFT.',
      });
      try {
        patch({ connection: 'pairing' });
        const linked = await connectHandheld(deviceId);
        deviceIdRef.current = linked.deviceId;
        await attachTransport(linked.transport, 'ble', linked.name);
        if (linked.address !== null) {
          await saveBondedDevice({ id: linked.deviceId, name: linked.name, address: linked.address });
        }
        unsubDisconnectRef.current = watchDisconnect(linked.deviceId, () => {
          lc3Log('disconnect', { reason: 'link' });
          void teardownClient();
          patch({
            connection: 'disconnected',
            hint: 'Handheld disconnected. Reconnect from the Connect screen.',
            sendBusy: false,
          });
        });
      } catch (error) {
        patch({
          connection: 'disconnected',
          error: userError(error, 'Could not connect. Open pairing on the handheld and try again.'),
        });
      }
    },
    [attachTransport, patch, stopScan, teardownClient],
  );

  const connectMock = useCallback(async () => {
    stopScan();
    const handheld = new MockHandheld(185);
    await attachTransport(handheld.transport, 'mock', `${handheld.node.name} (mock)`);
  }, [attachTransport, stopScan]);

  const disconnect = useCallback(async () => {
    stopScan();
    const deviceId = deviceIdRef.current;
    await teardownClient();
    if (deviceId) await cancelConnection(deviceId);
    deviceIdRef.current = null;
    patch({
      connection: 'disconnected',
      transportKind: null,
      hello: null,
      node: null,
      peers: [],
      messages: [],
      gps: null,
      status: null,
      selectedPeer: null,
      selectedMessage: null,
      sendBusy: false,
      hint: null,
    });
  }, [patch, stopScan, teardownClient]);

  const refreshAll = useCallback(async () => {
    const client = clientRef.current;
    if (!client) return;
    try {
      await applySnapshots(client);
    } catch (error) {
      patch({ error: userError(error, 'Could not refresh the handheld.') });
    }
  }, [applySnapshots, patch]);

  const loadPeer = useCallback(
    async (address: number) => {
      const client = clientRef.current;
      if (!client) return null;
      patch({ selectedPeer: null });
      const peer = await client.getPeer(address);
      patch({ selectedPeer: peer });
      return peer;
    },
    [patch],
  );

  const loadMessage = useCallback(
    async (origin: number, id: number) => {
      const client = clientRef.current;
      if (!client) return null;
      patch({ selectedMessage: null });
      const message = await client.getMessage(origin, id);
      patch({ selectedMessage: message });
      return message;
    },
    [patch],
  );

  const send = useCallback(async (destination: number, text: string) => {
    const client = clientRef.current;
    if (!client) throw new Error('Not connected');
    patch({ sendBusy: true, error: null });
    try {
      const result = await client.sendMessage(destination, text);
      const node = await client.getNode();
      lastSentRef.current = { origin: node.address, id: result.messageId };
      patch({ sendBusy: true, node });
    } catch (error) {
      patch({ sendBusy: false });
      if (error instanceof Lc3ApiError && error.code === 'BUSY') {
        const page = await fetchMessageSnapshot(client);
        patch({
          error: 'Handheld already has a message in flight.',
          messages: page.messages,
          messageRevision: page.revision,
          messageTotal: page.total,
        });
        return;
      }
      throw error;
    }
  }, [patch]);

  const markRead = useCallback(
    async (origin: number, id: number) => {
      const client = clientRef.current;
      if (!client) return;
      await client.markRead(origin, id);
      const page = await fetchMessageSnapshot(client);
      patch((prev) => ({ ...prev, messages: page.messages, messageRevision: page.revision, messageTotal: page.total, selectedMessage: prev.selectedMessage?.origin === origin && prev.selectedMessage.id === id ? { ...prev.selectedMessage, unread: false } : prev.selectedMessage }));
    },
    [patch],
  );

  const markAllRead = useCallback(async () => {
    const client = clientRef.current;
    if (!client) return;
    await client.markAllRead();
    const page = await fetchMessageSnapshot(client);
    patch((prev) => ({ ...prev, messages: page.messages, messageRevision: page.revision, messageTotal: page.total, selectedMessage: prev.selectedMessage ? { ...prev.selectedMessage, unread: false } : null }));
  }, [patch]);

  useEffect(() => {
    let cancelled = false;
    void (async () => {
      const last: BondedDevice | null = await loadBondedDevice();
      if (cancelled) return;
      if (last) patch({ lastDeviceName: last.name || `CL3-${last.address}` });
      if (!bleSupported()) {
        patch({ bleAvailable: false, connection: 'unavailable' });
      }
    })();
    return () => {
      cancelled = true;
      scanStopRef.current?.();
      if (gpsTimerRef.current) clearTimeout(gpsTimerRef.current);
      gpsTimerRef.current = null;
    };
  }, [patch]);

  const api = useMemo<SessionApi>(
    () => ({
      ...snap,
      startScan,
      stopScan,
      connectDevice,
      connectMock,
      disconnect,
      refreshAll,
      loadPeer,
      loadMessage,
      send,
      markRead,
      markAllRead,
    }),
    [
      snap,
      startScan,
      stopScan,
      connectDevice,
      connectMock,
      disconnect,
      refreshAll,
      loadPeer,
      loadMessage,
      send,
      markRead,
      markAllRead,
    ],
  );

  return <SessionContext.Provider value={api}>{children}</SessionContext.Provider>;
}

export function useLc3(): SessionApi {
  const value = useContext(SessionContext);
  if (!value) throw new Error('useLc3 must be used inside Lc3SessionProvider');
  return value;
}
