import type { ReactNode } from 'react';
import { StyleSheet, View, type ViewStyle } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Radius, Spacing, type ThemeColor } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';

export function PageHeading({
  eyebrow,
  title,
  description,
  action,
}: {
  eyebrow?: string;
  title: string;
  description?: string;
  action?: ReactNode;
}) {
  return (
    <View style={styles.headingRow}>
      <View style={styles.headingCopy}>
        {eyebrow ? <ThemedText type="eyebrow" themeColor="accent">{eyebrow}</ThemedText> : null}
        <ThemedText type="title">{title}</ThemedText>
        {description ? <ThemedText themeColor="textSecondary">{description}</ThemedText> : null}
      </View>
      {action}
    </View>
  );
}

export function Card({
  children,
  style,
  tone = 'default',
}: {
  children: ReactNode;
  style?: ViewStyle | ViewStyle[];
  tone?: 'default' | 'accent' | 'warning' | 'danger';
}) {
  const type = tone === 'accent' ? 'accentMuted' : tone === 'warning' ? 'warningMuted' : tone === 'danger' ? 'dangerMuted' : 'backgroundElement';
  const theme = useTheme();
  return <ThemedView type={type} style={[styles.card, { borderColor: theme.border }, style]}>{children}</ThemedView>;
}

export function StatusPill({
  label,
  tone = 'neutral',
}: {
  label: string;
  tone?: 'positive' | 'warning' | 'danger' | 'neutral';
}) {
  const theme = useTheme();
  const backgroundColor = tone === 'positive' ? theme.successMuted : tone === 'warning' ? theme.warningMuted : tone === 'danger' ? theme.dangerMuted : theme.backgroundSelected;
  const color: ThemeColor = tone === 'positive' ? 'success' : tone === 'warning' ? 'warning' : tone === 'danger' ? 'danger' : 'textSecondary';
  return (
    <View style={[styles.pill, { backgroundColor }]}>
      <ThemedText type="eyebrow" themeColor={color} style={styles.pillText}>{label}</ThemedText>
    </View>
  );
}

export function EmptyState({ icon, title, body }: { icon: string; title: string; body: string }) {
  return (
    <Card style={styles.empty}>
      <View style={styles.emptyIcon}><ThemedText style={styles.emptyIconText}>{icon}</ThemedText></View>
      <ThemedText type="smallBold">{title}</ThemedText>
      <ThemedText type="small" themeColor="textSecondary" style={styles.emptyBody}>{body}</ThemedText>
    </Card>
  );
}

export function Metric({ value, label }: { value: string | number; label: string }) {
  return (
    <View style={styles.metric}>
      <ThemedText type="metric">{value}</ThemedText>
      <ThemedText type="small" themeColor="textSecondary">{label}</ThemedText>
    </View>
  );
}

const styles = StyleSheet.create({
  headingRow: { flexDirection: 'row', alignItems: 'flex-end', justifyContent: 'space-between', gap: Spacing.three },
  headingCopy: { flex: 1, gap: 5, minWidth: 0 },
  card: { borderRadius: Radius.medium, padding: Spacing.three, gap: Spacing.two, overflow: 'hidden', borderWidth: 1 },
  pill: { alignSelf: 'flex-start', paddingVertical: 5, paddingHorizontal: 9, borderRadius: Radius.pill },
  pillText: { fontSize: 10, lineHeight: 12, letterSpacing: 0.8 },
  empty: { alignItems: 'center', paddingVertical: Spacing.five },
  emptyIcon: { width: 52, height: 52, borderRadius: 26, backgroundColor: 'rgba(128,128,128,0.12)', alignItems: 'center', justifyContent: 'center', marginBottom: Spacing.one },
  emptyIconText: { fontSize: 23 },
  emptyBody: { maxWidth: 320, textAlign: 'center' },
  metric: { flex: 1, minWidth: 80, gap: 2 },
});
