import { Stack, useRouter } from 'expo-router';
import { Pressable, StyleSheet, View } from 'react-native';

import { PrimaryButton } from '@/components/primary-button';
import { Screen } from '@/components/screen';
import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Card, PageHeading, StatusPill } from '@/components/ui';
import { Radius, Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';
import { useLc3 } from '@/store/lc3-session';

export default function PairScreen() {
  const session = useLc3();
  const router = useRouter();
  const theme = useTheme();

  return (
    <Screen showConnection={false}>
      <Stack.Title>Connect</Stack.Title>
      <PageHeading eyebrow="Secure setup" title="Connect handheld" description="Bluetooth stays between this phone and your trusted LC3." />

      <Card tone="accent" style={styles.guide}>
        <ThemedText type="eyebrow" themeColor="accent">On your handheld</ThemedText>
        <SetupStep number="1" title="Open Bluetooth" body="Choose 4 Bluetooth from the handheld menu." />
        <SetupStep number="2" title="Start pairing" body="Select Pair new phone and press D. You have 60 seconds." />
        <SetupStep number="3" title="Confirm the code" body="Choose your device below, then enter the six-digit code shown on its screen." />
      </Card>

      {session.error ? <Card tone="danger"><ThemedText themeColor="danger">{session.error}</ThemedText></Card> : null}
      {session.hint ? <Card tone="warning"><ThemedText type="small" themeColor="warning">{session.hint}</ThemedText></Card> : null}

      {session.bleAvailable ? (
        <PrimaryButton
          label={session.connection === 'scanning' ? 'Scanning for LC3…' : 'Scan for handhelds'}
          onPress={() => void session.startScan()}
          disabled={session.connection === 'scanning' || session.connection === 'connecting'}
        />
      ) : (
        <Card tone="warning"><ThemedText themeColor="warning">Bluetooth requires a development build on a physical phone. Expo Go and web cannot connect to the handheld.</ThemedText></Card>
      )}

      {session.discovered.length ? <View style={styles.devices}><ThemedText type="eyebrow" themeColor="textSecondary">Nearby handhelds</ThemedText>{session.discovered.map((device) => (
        <Pressable key={device.id} onPress={() => void session.connectDevice(device.id)} style={({ pressed }) => pressed && styles.pressed}>
          <ThemedView type="backgroundElement" style={styles.device}>
            <View style={[styles.radioIcon, { backgroundColor: theme.accentMuted }]}><ThemedText themeColor="accent" style={styles.radioGlyph}>⌁</ThemedText></View>
            <View style={styles.deviceCopy}><ThemedText type="smallBold">{device.name}</ThemedText><ThemedText type="small" themeColor="textSecondary">Node #{device.address}{device.rssi !== null ? `  ·  ${device.rssi} dBm` : ''}</ThemedText></View>
            <StatusPill label="Connect" tone="positive" />
          </ThemedView>
        </Pressable>
      ))}</View> : null}

      <View style={styles.mock}>
        <ThemedText type="small" themeColor="textSecondary">
          Simulator / no radio
        </ThemedText>
        <PrimaryButton
          label="Explore with demo handheld"
          variant="secondary"
          onPress={async () => {
            await session.connectMock();
            router.back();
          }}
        />
      </View>

      <Card>
        <ThemedText type="smallBold">Replacing a trusted phone?</ThemedText>
        <ThemedText type="small" themeColor="textSecondary">The handheld accepts one bonded phone. Use Forget trusted phone on the handheld, confirm twice with D, then open a new pairing window.</ThemedText>
      </Card>

      {session.connection === 'ready' ? (
        <PrimaryButton
          label="Disconnect handheld"
          variant="danger"
          onPress={() => void session.disconnect()}
        />
      ) : null}
    </Screen>
  );
}

function SetupStep({ number, title, body }: { number: string; title: string; body: string }) {
  return <View style={styles.step}><View style={styles.number}><ThemedText type="smallBold" themeColor="accent">{number}</ThemedText></View><View style={styles.stepCopy}><ThemedText type="smallBold">{title}</ThemedText><ThemedText type="small" themeColor="textSecondary">{body}</ThemedText></View></View>;
}

const styles = StyleSheet.create({
  guide: { borderRadius: Radius.large, padding: 20, gap: Spacing.three },
  step: { flexDirection: 'row', gap: 12 },
  number: { width: 28, height: 28, borderRadius: 14, backgroundColor: 'rgba(50,160,100,0.14)', alignItems: 'center', justifyContent: 'center' },
  stepCopy: { flex: 1, gap: 2 },
  devices: { gap: Spacing.two },
  device: { padding: 14, borderRadius: Radius.medium, gap: 12, flexDirection: 'row', alignItems: 'center' },
  radioIcon: { width: 42, height: 42, borderRadius: 14, alignItems: 'center', justifyContent: 'center' },
  radioGlyph: { fontSize: 24, fontWeight: '800' },
  deviceCopy: { flex: 1 },
  mock: { gap: Spacing.two, marginTop: Spacing.two, paddingTop: Spacing.three },
  pressed: { opacity: 0.7 },
});
