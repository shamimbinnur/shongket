import { StyleSheet, View } from 'react-native';

import { Screen } from '@/components/screen';
import { PeerRow } from '@/components/peer-row';
import { ThemedText } from '@/components/themed-text';
import { EmptyState, Metric, PageHeading } from '@/components/ui';
import { Spacing } from '@/constants/theme';
import { useLc3 } from '@/store/lc3-session';

export default function CrewScreen() {
  const session = useLc3();
  const online = session.peers.filter((peer) => peer.online).length;

  return (
    <Screen>
      <PageHeading eyebrow="Directory" title="Your crew" description="Presence and position reported by the mesh" />
      {session.connection !== 'ready' ? (
        <EmptyState icon="⌁" title="Crew unavailable" body="Connect your handheld to load the live crew directory." />
      ) : session.peers.length === 0 ? (
        <EmptyState icon="◎" title="No peers discovered" body="Other crew members will appear after their next authenticated radio packet." />
      ) : (
        <>
          <View style={styles.metrics}>
            <Metric value={online} label="online" />
            <Metric value={session.peerTotal} label="known peers" />
            <Metric value={session.peers.filter((peer) => peer.locationAvailable).length} label="with location" />
          </View>
          <View style={styles.section}>
            <ThemedText type="eyebrow" themeColor="textSecondary">Crew members</ThemedText>
            <View style={styles.list}>{session.peers.map((peer) => <PeerRow key={peer.address} peer={peer} />)}</View>
          </View>
        </>
      )}
    </Screen>
  );
}

const styles = StyleSheet.create({
  metrics: { flexDirection: 'row', gap: Spacing.three, flexWrap: 'wrap' },
  section: { gap: Spacing.two },
  list: { gap: Spacing.two },
});
