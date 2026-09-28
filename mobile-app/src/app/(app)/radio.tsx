import { StyleSheet, View } from 'react-native';

import { GpsCard } from '@/components/gps-card';
import { PrimaryButton } from '@/components/primary-button';
import { Screen } from '@/components/screen';
import { ThemedText } from '@/components/themed-text';
import { Card, Metric, PageHeading, StatusPill } from '@/components/ui';
import { Radius, Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { useLc3 } from '@/store/lc3-session';
import { formatUptime } from '@/utils/format';

export default function RadioScreen() {
  const session = useLc3();
  const node = session.node;

  return (
    <Screen>
      <PageHeading eyebrow="Handheld" title="Radio status" description="Health, position, and mesh diagnostics" />
      {node ? (
        <Card tone={node.conflict ? 'danger' : 'accent'} style={styles.hero}>
          <View style={styles.heroHeader}>
            <View style={styles.identity}>
              <ThemedText type="eyebrow" themeColor="textSecondary">Connected node</ThemedText>
              <ThemedText type="subtitle">{node.name} <ThemedText type="subtitle" themeColor="textSecondary">#{node.address}</ThemedText></ThemedText>
            </View>
            <StatusPill label={node.conflict ? 'Conflict' : 'Operational'} tone={node.conflict ? 'danger' : 'positive'} />
          </View>
          {node.conflict ? <ThemedText themeColor="danger">This address is also in use. Sending is disabled until the handheld is reconfigured.</ThemedText> : null}
          <View style={styles.metrics}>
            <Metric value={node.onlineCount} label="crew online" />
            <Metric value={node.unreadCount} label="unread" />
            <Metric value={formatUptime(node.uptime)} label="uptime" />
          </View>
          <View style={styles.healthRow}>
            <Health label="Radio" ok={isHealthy(node.radio)} />
            <Health label="Encryption" ok={isHealthy(node.crypto)} />
            <Health label="Config" ok={isHealthy(node.config)} />
          </View>
        </Card>
      ) : (
        <Card><ThemedText themeColor="textSecondary">Connect your handheld to view radio health.</ThemedText></Card>
      )}
      <GpsCard gps={session.gps} />
      {session.status && typeof session.status === 'object' ? (
        <Card style={styles.card}>
          <View style={styles.sectionTitle}>
            <View><ThemedText type="eyebrow" themeColor="textSecondary">Advanced</ThemedText><ThemedText type="smallBold">Diagnostics</ThemedText></View>
            <ThemedText type="code" themeColor="textSecondary">API v{session.hello?.apiVersion ?? 1}</ThemedText>
          </View>
          <StatusLines value={session.status} />
        </Card>
      ) : null}
      {session.connection === 'ready' ? (
        <PrimaryButton label="Refresh radio data" variant="secondary" onPress={() => void session.refreshAll()} />
      ) : null}
    </Screen>
  );
}

function StatusLines({ value }: { value: unknown }) {
  const entries = value && typeof value === 'object' ? Object.entries(value) : [];

  return (
    <View style={styles.block}>
      {entries.map(([key, nested]) => <View key={key} style={styles.diagnosticRow}><ThemedText type="small" themeColor="textSecondary">{prettyKey(key)}</ThemedText><ThemedText type="code" style={styles.diagnosticValue}>{nested && typeof nested === 'object' ? JSON.stringify(nested) : String(nested ?? '—')}</ThemedText></View>)}
    </View>
  );
}

function isHealthy(value: unknown) {
  return value === true || value === 'ready' || value === 'ok';
}

function Health({ label, ok }: { label: string; ok: boolean }) {
  const theme = useTheme();
  return <View style={styles.health}><View style={[styles.healthDot, { backgroundColor: ok ? '#32845B' : theme.danger }]} /><ThemedText type="small">{label}</ThemedText></View>;
}

function prettyKey(value: string) {
  return value.replace(/([A-Z])/g, ' $1').replace(/^./, (letter) => letter.toUpperCase());
}

const styles = StyleSheet.create({
  hero: { borderRadius: Radius.large, padding: 20, gap: Spacing.three },
  heroHeader: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'flex-start', gap: Spacing.two },
  identity: { gap: 2, flex: 1 },
  metrics: { flexDirection: 'row', gap: Spacing.three, flexWrap: 'wrap' },
  healthRow: { flexDirection: 'row', flexWrap: 'wrap', gap: 14 },
  health: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  healthDot: { width: 7, height: 7, borderRadius: 4 },
  card: { padding: 20, borderRadius: Radius.large, gap: Spacing.three },
  sectionTitle: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' },
  block: { gap: 0 },
  diagnosticRow: { flexDirection: 'row', justifyContent: 'space-between', gap: Spacing.three, paddingVertical: 10, borderBottomWidth: StyleSheet.hairlineWidth, borderBottomColor: 'rgba(128,128,128,0.25)' },
  diagnosticValue: { flex: 1, textAlign: 'right' },
});
