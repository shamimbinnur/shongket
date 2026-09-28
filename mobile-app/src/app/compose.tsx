import { Stack, useRouter } from 'expo-router';
import { useMemo, useState } from 'react';
import { Pressable, StyleSheet, TextInput, View } from 'react-native';

import { PrimaryButton } from '@/components/primary-button';
import { Screen } from '@/components/screen';
import { ThemedText } from '@/components/themed-text';
import { Card, PageHeading, StatusPill } from '@/components/ui';
import { Radius, Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { CREW_ADDRESS, MAX_MESSAGE_TEXT } from '@/lc3/constants';
import { isPrintableAscii } from '@/lc3/text';
import { useLc3 } from '@/store/lc3-session';

export default function ComposeScreen() {
  const session = useLc3();
  const theme = useTheme();
  const router = useRouter();
  const [destination, setDestination] = useState<number>(CREW_ADDRESS);
  const [text, setText] = useState('');
  const [error, setError] = useState<string | null>(null);

  const destinations = useMemo(
    () => [
      { address: CREW_ADDRESS, name: 'All crew' },
      ...session.peers
        .filter((peer) => peer.online && !peer.conflict && peer.address !== session.node?.address)
        .map((peer) => ({ address: peer.address, name: peer.name })),
    ],
    [session.node?.address, session.peers],
  );

  const valid = isPrintableAscii(text);
  const nodeReady = session.node && (session.node.radio === true || session.node.radio === 'ready') && (session.node.crypto === true || session.node.crypto === 'ready') && (session.node.config === true || session.node.config === 'ok') && !session.node.conflict;

  return (
    <Screen>
      <Stack.Title>Send</Stack.Title>
      <PageHeading eyebrow="New transmission" title="Send message" description="Choose a recipient, then your handheld will relay it over the mesh." />
      <View style={styles.section}>
        <ThemedText type="eyebrow" themeColor="textSecondary">Recipient</ThemedText>
        <View style={styles.destinations}>
        {destinations.map((item) => (
          <Pressable key={item.address} onPress={() => setDestination(item.address)} style={({ pressed }) => [styles.recipient, { backgroundColor: destination === item.address ? theme.accentMuted : theme.backgroundElement, borderColor: destination === item.address ? theme.accent : theme.border, opacity: pressed ? 0.7 : 1 }]}>
            <ThemedText type="smallBold" themeColor={destination === item.address ? 'accent' : 'text'}>{item.name}</ThemedText>
            <ThemedText type="small" themeColor="textSecondary">{item.address === CREW_ADDRESS ? 'Broadcast' : `#${item.address}`}</ThemedText>
          </Pressable>
        ))}
        </View>
      </View>
      <View style={styles.section}>
        <View style={styles.labelRow}><ThemedText type="eyebrow" themeColor="textSecondary">Message</ThemedText><StatusPill label={`${text.length} / ${MAX_MESSAGE_TEXT}`} tone={text.length > 70 ? 'warning' : 'neutral'} /></View>
        <TextInput value={text} onChangeText={setText} maxLength={MAX_MESSAGE_TEXT} autoCapitalize="sentences" autoCorrect={false} multiline placeholder="Type a short field message…" placeholderTextColor={theme.textSecondary} style={[styles.input, { color: theme.text, backgroundColor: theme.backgroundElement, borderColor: error ? theme.danger : theme.border }]} />
        <ThemedText type="small" themeColor="textSecondary">Printable English letters, numbers, spaces, and punctuation only.</ThemedText>
      </View>
      {error ? <ThemedText themeColor="danger">{error}</ThemedText> : null}
      {session.sendBusy ? (
        <Card tone="warning"><ThemedText type="small" themeColor="warning">Your handheld is still delivering the previous message. Wait for it to finish.</ThemedText></Card>
      ) : null}
      <PrimaryButton
        label={session.sendBusy ? 'Waiting for handheld…' : 'Send over mesh'}
        disabled={!valid || session.sendBusy || session.connection !== 'ready' || !nodeReady}
        onPress={async () => {
          setError(null);
          try {
            await session.send(destination, text);
            router.back();
          } catch (caught) {
            setError(caught instanceof Error ? caught.message : 'Send failed');
          }
        }}
      />
    </Screen>
  );
}

const styles = StyleSheet.create({
  section: { gap: Spacing.two },
  destinations: { flexDirection: 'row', flexWrap: 'wrap', gap: Spacing.two },
  recipient: { minWidth: 112, borderWidth: 1, borderRadius: Radius.medium, paddingVertical: 10, paddingHorizontal: 14 },
  labelRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  input: {
    borderWidth: 1,
    borderRadius: Radius.medium,
    padding: Spacing.three,
    minHeight: 140,
    fontSize: 17,
    lineHeight: 25,
    textAlignVertical: 'top',
  },
});
