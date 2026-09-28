import { MESSAGE_PAGE_LIMIT, PEER_PAGE_LIMIT } from '@/lc3/constants';
import type { Lc3Client } from '@/lc3/client';
import { Lc3TimeoutError } from '@/lc3/errors';
import type { GpsData, HelloData, MessageSummary, NodeData, PeerSummary, StatusData } from '@/lc3/types';

export type Snapshots = {
  hello: HelloData;
  node: NodeData;
  peers: PeerSummary[];
  peerRevision: number;
  peerTotal: number;
  messages: MessageSummary[];
  messageRevision: number;
  messageTotal: number;
  gps: GpsData;
  status: StatusData;
};

export async function fetchAllPages<T>(
  page: (offset: number) => Promise<{ nextOffset?: number; items: T[] }>,
): Promise<T[]> {
  const items: T[] = [];
  let offset = 0;
  for (let i = 0; i < 32; i += 1) {
    const result = await page(offset);
    const pageItems = Array.isArray(result.items) ? result.items : [];
    items.push(...pageItems);
    if (result.nextOffset === undefined) break;
    offset = result.nextOffset;
  }
  return items;
}

export async function fetchPeerSnapshot(client: Lc3Client): Promise<{
  peers: PeerSummary[];
  revision: number;
  total: number;
}> {
  let revision = 0;
  let total = 0;
  const peers = await fetchAllPages(async (offset) => {
    const page = await client.listPeers(offset, PEER_PAGE_LIMIT);
    revision = page.revision;
    total = page.total;
    return { nextOffset: page.nextOffset, items: page.peers };
  });
  return { peers, revision, total };
}

export async function fetchMessageSnapshot(client: Lc3Client): Promise<{
  messages: MessageSummary[];
  revision: number;
  total: number;
}> {
  let revision = 0;
  let total = 0;
  const messages = await fetchAllPages(async (offset) => {
    const page = await client.listMessages(offset, MESSAGE_PAGE_LIMIT);
    revision = page.revision;
    total = page.total;
    return { nextOffset: page.nextOffset, items: page.messages };
  });
  return { messages, revision, total };
}

export async function fetchSnapshots(client: Lc3Client): Promise<Snapshots> {
  let hello: HelloData;
  try {
    hello = await client.hello();
  } catch (error) {
    if (!(error instanceof Lc3TimeoutError)) throw error;
    // Android can report the GATT link as connected just before the handheld's
    // notification channel is ready. Give that channel one chance to settle.
    await new Promise((resolve) => setTimeout(resolve, 350));
    hello = await client.hello();
  }
  const [node, peers, messages, gps, status] = await Promise.all([
    client.getNode(),
    fetchPeerSnapshot(client),
    fetchMessageSnapshot(client),
    client.getGps(),
    client.getStatus(),
  ]);
  return {
    hello,
    node,
    peers: peers.peers,
    peerRevision: peers.revision,
    peerTotal: peers.total,
    messages: messages.messages,
    messageRevision: messages.revision,
    messageTotal: messages.total,
    gps,
    status,
  };
}
