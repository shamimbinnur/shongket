import { Link } from 'expo-router';
import { Pressable, StyleSheet, View } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Radius, Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { useLc3 } from '@/store/lc3-session';

export function ConnectionStrip() {
  const session = useLc3();
  const theme = useTheme();
  const ready = session.connection === 'ready';
  const label = ready
    ? session.lastDeviceName ?? session.node?.name ?? 'Handheld'
    : session.connection === 'scanning'
      ? 'Scanning…'
      : session.connection === 'connecting' || session.connection === 'pairing'
        ? 'Pairing…'
        : 'Not connected';

  return (
    <Link href="/pair" asChild>
      <Pressable style={({ pressed }) => pressed && styles.pressed}>
        <ThemedView type={ready ? 'successMuted' : 'warningMuted'} style={[styles.row, { borderColor: theme.border }]}>
          <View style={styles.left}>
            <View style={[styles.deviceIcon, { backgroundColor: ready ? theme.success : theme.warning }]}>
              <ThemedText style={[styles.radioGlyph, { color: theme.inverse }]}>⌁</ThemedText>
            </View>
            <View style={styles.copy}>
              <ThemedText type="smallBold" numberOfLines={1}>{label}</ThemedText>
              <ThemedText type="small" themeColor="textSecondary" numberOfLines={1}>
                {ready
                  ? `${session.node?.onlineCount ?? 0} crew online  ·  ${session.node?.unreadCount ?? 0} unread`
                  : 'Connect your LC3 handheld'}
              </ThemedText>
            </View>
          </View>
          <ThemedText type="smallBold" themeColor={ready ? 'success' : 'warning'}>
            {ready ? 'Manage  ›' : 'Connect  ›'}
          </ThemedText>
        </ThemedView>
      </Pressable>
    </Link>
  );
}

const styles = StyleSheet.create({
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: 14,
    borderRadius: Radius.medium,
    gap: Spacing.two,
    borderWidth: 1,
  },
  left: { flexDirection: 'row', alignItems: 'center', gap: Spacing.two, flex: 1 },
  copy: { flex: 1, minWidth: 0 },
  deviceIcon: { width: 38, height: 38, borderRadius: 12, alignItems: 'center', justifyContent: 'center' },
  radioGlyph: { fontSize: 22, lineHeight: 24, fontWeight: '800' },
  pressed: { opacity: 0.75 },
});
