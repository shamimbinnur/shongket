import { Tabs } from 'expo-router';
import { type ColorValue, StyleSheet, Text, useColorScheme, View } from 'react-native';

import { Colors } from '@/constants/theme';
import { useLc3 } from '@/store/lc3-session';

export default function AppTabs() {
  const scheme = useColorScheme();
  const colors = Colors[scheme === 'unspecified' || scheme == null ? 'light' : scheme];
  const unread = useLc3().node?.unreadCount ?? 0;

  return (
    <Tabs
      screenOptions={{
        headerShown: false,
        sceneStyle: { backgroundColor: colors.background },
        tabBarActiveTintColor: colors.accent,
        tabBarInactiveTintColor: colors.textSecondary,
        tabBarHideOnKeyboard: true,
        tabBarLabelStyle: { fontSize: 13, fontWeight: '700' },
        tabBarStyle: {
          backgroundColor: colors.backgroundElement,
          borderTopColor: colors.border,
          height: 76,
          paddingTop: 7,
          paddingBottom: 10,
        },
        tabBarItemStyle: { gap: 2 },
      }}>
      <Tabs.Screen
        name="index"
        options={{ title: 'Messages', tabBarIcon: ({ color, focused }) => <TabGlyph glyph="▰" color={color} focused={focused} focusColor={colors.backgroundSelected} />, tabBarBadge: unread > 0 ? unread : undefined, tabBarBadgeStyle: { backgroundColor: colors.accent, color: colors.inverse } }}
      />
      <Tabs.Screen name="crew" options={{ title: 'Crew', tabBarIcon: ({ color, focused }) => <TabGlyph glyph="●●" color={color} focused={focused} focusColor={colors.backgroundSelected} /> }} />
      <Tabs.Screen name="radio" options={{ title: 'Radio', tabBarIcon: ({ color, focused }) => <TabGlyph glyph="⌁" color={color} focused={focused} focusColor={colors.backgroundSelected} /> }} />
    </Tabs>
  );
}

function TabGlyph({ glyph, color, focused, focusColor }: { glyph: string; color: ColorValue; focused: boolean; focusColor: ColorValue }) {
  return <View style={[styles.glyph, focused && { backgroundColor: focusColor }]}><Text style={{ color, fontSize: glyph === '●●' ? 11 : 20, fontWeight: '800' }}>{glyph}</Text></View>;
}

const styles = StyleSheet.create({ glyph: { width: 34, height: 25, borderRadius: 12, alignItems: 'center', justifyContent: 'center' } });
