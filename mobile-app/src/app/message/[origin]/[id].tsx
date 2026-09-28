import { Stack, useLocalSearchParams } from 'expo-router';
import { useEffect } from 'react';
import { StyleSheet, View } from 'react-native';

import { PrimaryButton } from '@/components/primary-button';
import { Screen } from '@/components/screen';
import { ThemedText } from '@/components/themed-text';
import { Card, PageHeading, StatusPill } from '@/components/ui';
import { Radius, Spacing } from '@/constants/theme';
import { useLc3 } from '@/store/lc3-session';

export default function MessageDetailScreen() {
  const { origin, id } = useLocalSearchParams<{ origin: string; id: string }>();
  const session = useLc3();
  const originN = Number(origin);
  const idN = Number(id);
  const message = session.selectedMessage;
  const loadMessage = session.loadMessage;
  const markRead = session.markRead;

  useEffect(() => {
    if (!Number.isFinite(originN) || !Number.isFinite(idN)) return;
    void (async () => {
      const detail = await loadMessage(originN, idN);
      if (detail?.unread) await markRead(originN, idN);
    })();
  }, [originN, idN, loadMessage, markRead]);

  return (
    <Screen>
      <Stack.Title>Message</Stack.Title>
      <PageHeading eyebrow="Message detail" title={message ? (message.dir === 'out' ? `To ${message.peerName}` : `From ${message.peerName}`) : 'Message'} description={message ? (message.kind === 'crew' ? 'Crew broadcast' : 'Direct transmission') : undefined} />
      {!message ? (
        <ThemedText themeColor="textSecondary">Loading message from handheld…</ThemedText>
      ) : (
        <>
          <Card style={styles.messageCard}>
            <View style={styles.messageMeta}><StatusPill label={message.dir === 'out' ? 'Outgoing' : 'Incoming'} tone={message.dir === 'out' ? 'neutral' : 'positive'} /><StatusPill label={message.delivery === 'failed' ? 'Failed' : message.delivery === 'sending' ? 'Sending' : message.delivery === 'delivered' ? 'Delivered' : 'Received'} tone={message.delivery === 'failed' ? 'danger' : message.delivery === 'sending' ? 'warning' : 'positive'} /></View>
            <ThemedText style={styles.messageText}>{message.text}</ThemedText>
          </Card>
          <Card style={styles.detailCard}>
            <Detail label="Source" value={message.source} />
            <Detail label="Message ID" value={`${message.origin}:${message.id}`} mono />
            <Detail label="Handheld time" value={`${message.time}s uptime`} mono />
          </Card>
        </>
      )}
      {message?.unread ? (
        <PrimaryButton label="Mark read" onPress={() => void session.markRead(originN, idN)} />
      ) : null}
    </Screen>
  );
}

function Detail({ label, value, mono = false }: { label: string; value: string; mono?: boolean }) {
  return <View style={styles.detail}><ThemedText type="small" themeColor="textSecondary">{label}</ThemedText><ThemedText type={mono ? 'code' : 'smallBold'}>{value}</ThemedText></View>;
}

const styles = StyleSheet.create({
  messageCard: { padding: 20, borderRadius: Radius.large, gap: Spacing.three },
  messageMeta: { flexDirection: 'row', gap: Spacing.two, flexWrap: 'wrap' },
  messageText: { fontSize: 20, lineHeight: 30, fontWeight: '500' },
  detailCard: { gap: 0 },
  detail: { flexDirection: 'row', justifyContent: 'space-between', gap: Spacing.three, paddingVertical: 10 },
});
