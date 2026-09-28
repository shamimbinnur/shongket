import { TabList, TabSlot, TabTrigger, TabTriggerSlotProps, Tabs } from 'expo-router/ui';
import { type ReactNode } from 'react';
import { Pressable, StyleSheet, View, useColorScheme, useWindowDimensions } from 'react-native';
import { Image } from 'expo-image';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Colors, MaxContentWidth, Spacing } from '@/constants/theme';
import { useLc3 } from '@/store/lc3-session';

export default function AppTabs() {
  return (
    <Tabs>
      <TabSlot style={{ height: '100%', paddingTop: 78 }} />
      <TabList asChild>
        <WebTabList>
          <TabTrigger name="home" href="/" asChild>
            <TabButton glyph="▰">Messages</TabButton>
          </TabTrigger>
          <TabTrigger name="crew" href="/crew" asChild>
            <TabButton glyph="●●">Crew</TabButton>
          </TabTrigger>
          <TabTrigger name="radio" href="/radio" asChild>
            <TabButton glyph="⌁">Radio</TabButton>
          </TabTrigger>
        </WebTabList>
      </TabList>
    </Tabs>
  );
}

function TabButton({ children, isFocused, glyph, ...props }: TabTriggerSlotProps & { glyph: string }) {
  return (
    <Pressable {...props} style={({ pressed }) => pressed && styles.pressed}>
      <ThemedView type={isFocused ? 'backgroundSelected' : 'backgroundElement'} style={styles.tabButtonView}>
        <ThemedText style={styles.glyph} themeColor={isFocused ? 'accent' : 'textSecondary'}>{glyph}</ThemedText>
        <ThemedText type="small" themeColor={isFocused ? 'text' : 'textSecondary'}>
          {children}
        </ThemedText>
      </ThemedView>
    </Pressable>
  );
}

function WebTabList(props: { children?: ReactNode } & React.ComponentProps<typeof View>) {
  const unread = useLc3().node?.unreadCount ?? 0;
  const { width } = useWindowDimensions();
  const scheme = useColorScheme();
  const colors = Colors[scheme === 'unspecified' || scheme == null ? 'light' : scheme];

  return (
    <View {...props} style={styles.tabListContainer}>
      <ThemedView type="backgroundElement" style={styles.innerContainer}>
        <View style={styles.brandMark}><Image source={require('@/shongket-logo.png')} contentFit="contain" style={styles.brandImage} accessibilityLabel="Shongket" /></View>
        {width >= 680 ? <ThemedText type="small" style={[styles.brandText, { color: colors.textSecondary }]}>Beyond the mobile coverage</ThemedText> : null}
        {props.children}
        {unread > 0 ? <View style={[styles.badge, { backgroundColor: colors.accent }]}><ThemedText type="smallBold" style={{ color: colors.inverse }}>{unread}</ThemedText></View> : null}
      </ThemedView>
    </View>
  );
}

const styles = StyleSheet.create({
  tabListContainer: {
    position: 'absolute',
    zIndex: 10,
    width: '100%',
    paddingHorizontal: Spacing.three,
    paddingVertical: 12,
    justifyContent: 'center',
    alignItems: 'center',
    flexDirection: 'row',
  },
  innerContainer: {
    paddingVertical: 8,
    paddingHorizontal: 12,
    borderRadius: 18,
    flexDirection: 'row',
    alignItems: 'center',
    flexGrow: 1,
    gap: 6,
    maxWidth: MaxContentWidth,
  },
  brandMark: { width: 128, height: 40, borderRadius: 10, alignItems: 'center', justifyContent: 'center', backgroundColor: '#171717', overflow: 'hidden' },
  brandImage: { width: 214, height: 107 },
  brandText: { marginRight: 'auto', fontSize: 12 },
  badge: { minWidth: 25, height: 25, borderRadius: 13, paddingHorizontal: 7, alignItems: 'center', justifyContent: 'center', marginLeft: 5 },
  pressed: { opacity: 0.7 },
  tabButtonView: {
    paddingVertical: 8,
    paddingHorizontal: 12,
    borderRadius: Spacing.three,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 7,
  },
  glyph: { fontSize: 14, fontWeight: '800' },
});
