import { Link } from 'expo-router';
import { Pressable, StyleSheet, View } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Radius, Spacing } from '@/constants/theme';
import type { MessageSummary } from '@/lc3/types';
import { useTheme } from '@/hooks/use-theme';
import { formatAge } from '@/utils/format';

export function MessageRow({ message }: { message: MessageSummary }) {
  const theme = useTheme();
  const outbound = message.dir === 'out';
  return (
    <Link href={`/message/${message.origin}/${message.id}`} asChild>
      <Pressable style={({ pressed }) => pressed && styles.pressed}>
        <ThemedView type="backgroundElement" style={styles.row}>
          <View style={[styles.avatar, { backgroundColor: outbound ? theme.backgroundSelected : theme.accentMuted }]}>
            <ThemedText type="smallBold" themeColor={outbound ? 'textSecondary' : 'accent'}>
              {message.kind === 'crew' ? 'ALL' : message.peerName.slice(0, 2).toUpperCase()}
            </ThemedText>
          </View>
          <View style={styles.body}>
            <View style={styles.meta}>
              <ThemedText type="smallBold" numberOfLines={1} style={styles.name}>
                {message.kind === 'crew' ? 'All crew' : message.peerName}
              </ThemedText>
              <ThemedText type="small" themeColor="textSecondary">{formatAge(message.ageSeconds)}</ThemedText>
            </View>
            <View style={styles.previewRow}>
              <ThemedText themeColor={message.unread ? 'text' : 'textSecondary'} numberOfLines={1} style={styles.preview}>
                {outbound ? 'You: ' : ''}{message.preview}
              </ThemedText>
              {message.unread ? <View style={[styles.unread, { backgroundColor: theme.accent }]} /> : null}
            </View>
            {outbound ? (
              <ThemedText type="small" themeColor={message.delivery === 'failed' ? 'danger' : message.delivery === 'sending' ? 'warning' : 'textSecondary'}>
                {message.delivery === 'delivered' ? 'Delivered' : message.delivery === 'sending' ? 'Sending…' : message.delivery === 'failed' ? 'Delivery failed' : 'Sent'}
              </ThemedText>
            ) : null}
          </View>
        </ThemedView>
      </Pressable>
    </Link>
  );
}

const styles = StyleSheet.create({
  row: {
    padding: 14,
    borderRadius: Radius.medium,
    gap: 12,
    flexDirection: 'row',
    alignItems: 'center',
  },
  avatar: { width: 46, height: 46, borderRadius: 15, alignItems: 'center', justifyContent: 'center' },
  body: { flex: 1, minWidth: 0, gap: 2 },
  meta: { flexDirection: 'row', justifyContent: 'space-between', gap: Spacing.two },
  name: { flex: 1 },
  previewRow: { flexDirection: 'row', alignItems: 'center', gap: Spacing.two },
  preview: { flex: 1 },
  unread: { width: 8, height: 8, borderRadius: 4 },
  pressed: { opacity: 0.7 },
});
