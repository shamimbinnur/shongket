import { Image } from 'expo-image';
import { Platform, StyleSheet, View } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { Spacing } from '@/constants/theme';

export function BrandHeader() {
  if (Platform.OS === 'web') return null;
  return (
    <View style={styles.header}>
      <View style={styles.logoClip}>
        <Image source={require('@/shongket-logo.png')} contentFit="contain" style={styles.logo} accessibilityLabel="Shongket" />
      </View>
      <ThemedText type="small" themeColor="textSecondary" style={styles.tagline}>Beyond the mobile coverage</ThemedText>
    </View>
  );
}

const styles = StyleSheet.create({
  header: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: Spacing.two, minHeight: 58 },
  logoClip: { width: 126, height: 50, backgroundColor: '#171717', borderRadius: 12, overflow: 'hidden', alignItems: 'center', justifyContent: 'center' },
  logo: { width: 214, height: 107 },
  tagline: { flex: 1, textAlign: 'right', fontSize: 12, lineHeight: 17, maxWidth: 172 },
});
