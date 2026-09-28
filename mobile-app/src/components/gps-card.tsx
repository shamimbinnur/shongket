import { StyleSheet, View } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { Radius, Spacing } from '@/constants/theme';
import type { GpsData } from '@/lc3/types';
import { Card, StatusPill } from '@/components/ui';
import { formatAge } from '@/utils/format';

export function GpsCard({ gps }: { gps: GpsData | null }) {
  if (!gps) {
    return (
      <Card style={styles.card}>
        <ThemedText type="smallBold">GPS</ThemedText>
        <ThemedText themeColor="textSecondary">No handheld GPS yet.</ThemedText>
      </Card>
    );
  }

  return (
    <Card style={styles.card}>
      <View style={styles.meta}>
        <View style={styles.titleBlock}>
          <ThemedText type="eyebrow" themeColor="textSecondary">Position</ThemedText>
          <ThemedText type="subtitle">Handheld GPS</ThemedText>
        </View>
        <StatusPill label={gps.state === 'FIX' ? 'Live fix' : gps.state === 'STALE' ? 'Last known' : 'Searching'} tone={gps.state === 'FIX' ? 'positive' : 'warning'} />
      </View>
      <ThemedText type="small" themeColor="textSecondary">
        {gps.satellites} satellites  ·  updated {formatAge(gps.fixAgeMs / 1000)} ago
      </ThemedText>
      {gps.valid && gps.lat !== undefined && gps.lon !== undefined ? (
        <ThemedText type="code" style={styles.coordinates}>
          {gps.lat.toFixed(5)}, {gps.lon.toFixed(5)}
        </ThemedText>
      ) : (
        <ThemedText themeColor="textSecondary">No valid coordinates.</ThemedText>
      )}
    </Card>
  );
}

const styles = StyleSheet.create({
  card: { padding: 20, borderRadius: Radius.large, gap: Spacing.two },
  meta: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'flex-start', gap: Spacing.two },
  titleBlock: { gap: 2 },
  coordinates: { fontSize: 15, lineHeight: 22 },
});
