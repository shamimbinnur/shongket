import { MAX_REQUEST_FRAME_ID } from '@/lc3/constants';
import { Lc3Client } from '@/lc3';
import { MockHandheld } from '@/lc3/mock';
import type { FragmentHandler, Lc3Transport } from '@/lc3/transport';

class SlowWriteTransport implements Lc3Transport {
  readonly kind = 'mock' as const;

  getAttMtu(): number {
    return 247;
  }

  async writeFragments(): Promise<void> {
    await new Promise((resolve) => setTimeout(resolve, 50));
  }

  onFragment(_handler: FragmentHandler): () => void {
    return () => undefined;
  }

  async close(): Promise<void> {}
}

async function waitFor(predicate: () => boolean, timeoutMs = 1000): Promise<void> {
  const start = Date.now();
  while (!predicate()) {
    if (Date.now() - start > timeoutMs) throw new Error('timeout waiting for condition');
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}

describe('Lc3Client against mock handheld', () => {
  test('handles a response timeout that fires while the BLE write is pending', async () => {
    const client = new Lc3Client(new SlowWriteTransport(), 10);
    await expect(client.hello()).rejects.toMatchObject({ code: 'TIMEOUT' });
    await client.close();
  });

  test('hello then paginated snapshots', async () => {
    const handheld = new MockHandheld();
    const client = new Lc3Client(handheld.transport);
    const hello = await client.hello();
    expect(hello.apiVersion).toBe(1);
    expect(hello.bootSession).toHaveLength(14);

    const peers = await client.listPeers(0, 3);
    expect(peers.peers).toHaveLength(3);
    expect(peers.nextOffset).toBe(3);
    const rest = await client.listPeers(peers.nextOffset, 3);
    expect(rest.peers).toHaveLength(1);
    expect(rest.nextOffset).toBeUndefined();

    const messages = await client.listMessages(0, 2);
    expect(messages.messages).toHaveLength(2);
    expect(messages.nextOffset).toBe(2);
    await client.close();
  });

  test('request ids wrap 1..0x7FFF and never use 0', async () => {
    const handheld = new MockHandheld(512);
    const client = new Lc3Client(handheld.transport);
    (client as unknown as { nextId: number }).nextId = MAX_REQUEST_FRAME_ID;
    const ping = await client.ping({ n: 1 });
    expect(ping.uptime).toBeGreaterThan(0);
    const again = await client.ping({ n: 2 });
    expect(again.echo).toEqual({ n: 2 });
    await client.close();
  });

  test('send_message returns sending then delivered via events; BUSY does not enqueue a second send', async () => {
    const handheld = new MockHandheld(512);
    handheld.inflight = handheld.messages[1];
    const client = new Lc3Client(handheld.transport);

    await expect(client.sendMessage(2, 'Meet at gate 2')).rejects.toMatchObject({
      code: 'BUSY',
    });

    handheld.inflight = null;
    const events: string[] = [];
    client.onEvent((event) => events.push(event.event));
    const sent = await client.sendMessage(2, 'Meet at gate 2');
    expect(sent.state).toBe('sending');
    await waitFor(() => events.includes('messages_changed') && handheld.inflight === null);
    const record = await client.getMessage(1, sent.messageId);
    expect(record.delivery).toBe('delivered');
    expect(record.text).toBe('Meet at gate 2');
    await client.close();
  });

  test('event seq gap notifies listeners', async () => {
    const handheld = new MockHandheld(512);
    const client = new Lc3Client(handheld.transport);
    const gaps: number[][] = [];
    client.onSeqGap((expected, actual) => gaps.push([expected, actual]));
    handheld.emit('node_changed', { unreadCount: 1 });
    await waitFor(() => client.lastEventSeq === 1);
    handheld.eventSeq = 4;
    handheld.emit('resync_required', {});
    await waitFor(() => gaps.length === 1);
    expect(gaps[0][0]).toBe(2);
    expect(gaps[0][1]).toBe(5);
    await client.close();
  });

  test('adapts the documented v1 wire field names for the UI', async () => {
    const handheld = new MockHandheld();
    const client = new Lc3Client(handheld.transport);
    const request = jest.spyOn(client, 'request');

    request.mockResolvedValueOnce({ api: 1, protocol: 'LC3', session: 'A1B2C3D4E5F607', eventSeq: 12, peerRevision: 9, messageRevision: 4, capabilities: ['directory', 'messages'] });
    await expect(client.hello()).resolves.toMatchObject({ apiVersion: 1, protocolVersion: 'LC3', bootSession: 'A1B2C3D4E5F607' });

    request.mockResolvedValueOnce({ revision: 9, total: 1, items: [{ address: 7, name: 'BRAVO', online: true, conflict: false, hop: 1, rssi: -78, location: true }] });
    await expect(client.listPeers()).resolves.toMatchObject({ peers: [{ address: 7, locationAvailable: true }] });

    request.mockResolvedValueOnce({ revision: 14, total: 1, items: [{ origin: 19, id: 88, dir: 'in', kind: 'crew', peer: 19, name: 'CHARLIE', preview: 'Weather turning', unread: true, delivery: 'received', ageSeconds: 95 }] });
    await expect(client.listMessages()).resolves.toMatchObject({ messages: [{ peerName: 'CHARLIE', peerAddress: 19, ageSeconds: 95 }] });

    await client.close();
  });
});
