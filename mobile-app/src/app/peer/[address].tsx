import { Stack, useLocalSearchParams } from 'expo-router';
import { useEffect } from 'react';
import { StyleSheet, View } from 'react-native';

import { Screen } from '@/components/screen';
import { ThemedText } from '@/components/themed-text';
import { Card, Metric, PageHeading, StatusPill } from '@/components/ui';
import { Radius, Spacing } from '@/constants/theme';
import { useLc3 } from '@/store/lc3-session';
import { formatAge, formatDistance } from '@/utils/format';

export default function PeerDetailScreen() {
  const { address } = useLocalSearchParams<{ address: string }>();
  const session = useLc3();
  const addressN = Number(address);
  const peer = session.selectedPeer;
  const loadPeer = session.loadPeer;

  useEffect(() => {
    if (Number.isFinite(addressN)) void loadPeer(addressN);
  }, [addressN, loadPeer]);

  return (
    <Screen>
      <Stack.Title>{peer?.name ?? 'Peer'}</Stack.Title>
      <PageHeading eyebrow="Crew member" title={peer?.name ?? 'Loading peer…'} description={peer ? `Mesh node #${peer.address}` : undefined} action={peer ? <StatusPill label={peer.conflict ? 'Conflict' : peer.online ? 'Online' : 'Offline'} tone={peer.conflict ? 'danger' : peer.online ? 'positive' : 'neutral'} /> : undefined} />
      {!peer ? (
        <ThemedText themeColor="textSecondary">Loading peer from handheld…</ThemedText>
      ) : (
        <>
        {peer.conflict ? <Card tone="danger"><ThemedText themeColor="danger">More than one physical device is using this node address. Direct messages are disabled.</ThemedText></Card> : null}
        <Card style={styles.card}>
          <View style={styles.metrics}><Metric value={peer.hop === 1 ? 'Direct' : peer.hop} label={peer.hop === 1 ? 'radio path' : 'mesh hops'} /><Metric value={`${peer.rssi} dBm`} label="last-hop signal" /></View>
        </Card>
          {peer.location ? (
            <Card style={styles.location}>
              <View style={styles.locationTitle}><View><ThemedText type="eyebrow" themeColor="textSecondary">Position</ThemedText><ThemedText type="subtitle">{formatDistance(peer.location.distanceM)} away</ThemedText></View><StatusPill label={peer.location.current ? 'Current' : 'Last known'} tone={peer.location.current ? 'positive' : 'warning'} /></View>
              <ThemedText type="small" themeColor="textSecondary">
                {peer.location.age !== undefined ? `Updated ${formatAge(peer.location.age)} ago` : 'Fresh position'}
              </ThemedText>
              {peer.location.lat !== undefined && peer.location.lon !== undefined ? (
                <ThemedText type="code">
                  {peer.location.lat.toFixed(5)}, {peer.location.lon.toFixed(5)}
                </ThemedText>
              ) : null}
              {peer.location.distanceM !== undefined ? (
                <ThemedText>
                  Bearing {Math.round(peer.location.bearing ?? 0)}° true north
                </ThemedText>
              ) : (
                <ThemedText themeColor="textSecondary">
                  Distance and bearing only appear when both radios have coordinates.
                </ThemedText>
              )}
            </Card>
          ) : (
            <Card><ThemedText themeColor="textSecondary">No location has been shared by this peer.</ThemedText></Card>
          )}
        </>
      )}
    </Screen>
  );
}

const styles = StyleSheet.create({
  card: { padding: 20, borderRadius: Radius.large },
  metrics: { flexDirection: 'row', gap: Spacing.four, flexWrap: 'wrap' },
  location: { padding: 20, borderRadius: Radius.large, gap: Spacing.two },
  locationTitle: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'flex-start', gap: Spacing.two },
});
