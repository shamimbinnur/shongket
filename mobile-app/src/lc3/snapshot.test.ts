import { Lc3Client } from '@/lc3/client';
import { MockHandheld } from '@/lc3/mock';
import { fetchSnapshots } from '@/lc3/snapshot';

describe('fetchSnapshots', () => {
  test('walks nextOffset for peers and messages', async () => {
    const handheld = new MockHandheld(185);
    const client = new Lc3Client(handheld.transport);
    const snap = await fetchSnapshots(client);
    expect(snap.peers).toHaveLength(handheld.peers.length);
    expect(snap.messages).toHaveLength(handheld.messages.length);
    expect(snap.hello.bootSession).toBe(handheld.bootSession);
    expect(snap.gps.state).toBe('FIX');
    await client.close();
  });
});
