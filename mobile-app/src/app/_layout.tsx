import { DarkTheme, DefaultTheme, Stack, ThemeProvider } from 'expo-router';
import { useColorScheme } from 'react-native';

import { Colors } from '@/constants/theme';
import { Lc3SessionProvider } from '@/store/lc3-session';

export default function RootLayout() {
  const colorScheme = useColorScheme();
  const dark = colorScheme === 'dark';
  const palette = Colors[dark ? 'dark' : 'light'];
  const navigationTheme = {
    ...(dark ? DarkTheme : DefaultTheme),
    colors: {
      ...(dark ? DarkTheme.colors : DefaultTheme.colors),
      primary: palette.accent,
      background: palette.background,
      card: palette.background,
      text: palette.text,
      border: palette.border,
    },
  };

  return (
    <ThemeProvider value={navigationTheme}>
      <Lc3SessionProvider>
        <Stack screenOptions={{ headerShadowVisible: false, headerStyle: { backgroundColor: palette.background }, headerTintColor: palette.text, headerTitleStyle: { fontWeight: '700' }, contentStyle: { backgroundColor: palette.background } }}>
          <Stack.Screen name="(app)" options={{ headerShown: false }} />
          <Stack.Screen name="pair" options={{ title: 'Connect' }} />
          <Stack.Screen name="compose" options={{ title: 'Send' }} />
          <Stack.Screen name="message/[origin]/[id]" options={{ title: 'Message' }} />
          <Stack.Screen name="peer/[address]" options={{ title: 'Peer' }} />
        </Stack>
      </Lc3SessionProvider>
    </ThemeProvider>
  );
}
