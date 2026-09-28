import { Link } from 'expo-router';
import { Pressable, StyleSheet, View } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Radius } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import type { PeerSummary } from '@/lc3/types';

export function PeerRow({ peer }: { peer: PeerSummary }) {
  const theme = useTheme();
  const initials = peer.name.slice(0, 2).toUpperCase();
  return (
    <Link href={`/peer/${peer.address}`} asChild>
      <Pressable style={({ pressed }) => pressed && styles.pressed}>
        <ThemedView type="backgroundElement" style={styles.row}>
          <View style={[styles.avatar, { backgroundColor: peer.online ? theme.accentMuted : theme.backgroundSelected }]}>
            <ThemedText type="smallBold" themeColor={peer.online ? 'accent' : 'textSecondary'}>{initials}</ThemedText>
          </View>
          <View style={styles.body}>
            <View style={styles.meta}>
              <ThemedText type="smallBold">{peer.name}</ThemedText>
              <ThemedText type="small" themeColor="textSecondary">#{peer.address}</ThemedText>
            </View>
            <ThemedText type="small" themeColor={peer.conflict ? 'danger' : peer.online ? 'accent' : 'textSecondary'}>
              {peer.conflict ? 'Address conflict' : peer.online ? `Online  ·  ${peer.hop === 1 ? 'direct' : `${peer.hop} hops`}` : 'Offline'}
              {peer.locationAvailable ? '  ·  location' : ''}
            </ThemedText>
          </View>
          <ThemedText themeColor="textSecondary">›</ThemedText>
        </ThemedView>
      </Pressable>
    </Link>
  );
}

const styles = StyleSheet.create({
  row: { padding: 14, borderRadius: Radius.medium, gap: 12, flexDirection: 'row', alignItems: 'center' },
  avatar: { width: 44, height: 44, borderRadius: 14, alignItems: 'center', justifyContent: 'center' },
  body: { flex: 1, gap: 2 },
  meta: { flexDirection: 'row', justifyContent: 'space-between' },
  pressed: { opacity: 0.7 },
});
