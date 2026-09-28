import { useRouter } from 'expo-router';
import { StyleSheet, View } from 'react-native';

import { MessageRow } from '@/components/message-row';
import { PrimaryButton } from '@/components/primary-button';
import { Screen } from '@/components/screen';
import { ThemedText } from '@/components/themed-text';
import { EmptyState, PageHeading } from '@/components/ui';
import { Spacing } from '@/constants/theme';
import { useLc3 } from '@/store/lc3-session';

export default function MessagesScreen() {
  const session = useLc3();
  const router = useRouter();
  const ready = session.connection === 'ready';

  return (
    <Screen>
      <PageHeading
        eyebrow="LC3 mesh"
        title="Messages"
        description={ready ? `${session.messageTotal} stored on your handheld` : 'Your private field communications'}
        action={ready ? <PrimaryButton label="New message" compact onPress={() => router.push('/compose')} disabled={session.sendBusy} /> : undefined}
      />
      {session.error ? (
        <View style={styles.error}><ThemedText type="small" themeColor="danger">{session.error}</ThemedText></View>
      ) : null}
      {!ready ? (
        <EmptyState icon="⌁" title="Handheld not connected" body="Connect your LC3 to securely view messages and send over the mesh." />
      ) : session.messages.length === 0 ? (
        <EmptyState icon="✦" title="No messages yet" body="Start a direct conversation or send an update to your whole crew." />
      ) : (
        <View style={styles.list}>{session.messages.map((message) => <MessageRow key={`${message.origin}-${message.id}`} message={message} />)}</View>
      )}
      {ready && session.messages.some((message) => message.unread) ? (
        <PrimaryButton label="Mark all as read" variant="secondary" onPress={() => void session.markAllRead()} />
      ) : null}
    </Screen>
  );
}

const styles = StyleSheet.create({
  list: { gap: Spacing.two },
  error: { paddingHorizontal: Spacing.three },
});
