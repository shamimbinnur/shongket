import { ReactNode } from 'react';
import { ScrollView, StyleSheet, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { ConnectionStrip } from '@/components/connection-strip';
import { BrandHeader } from '@/components/brand-header';
import { ThemedView } from '@/components/themed-view';
import { MaxContentWidth, Spacing } from '@/constants/theme';

export function Screen({
  children,
  scroll = true,
  showConnection = true,
  compact = false,
}: {
  children: ReactNode;
  scroll?: boolean;
  showConnection?: boolean;
  compact?: boolean;
}) {
  const body = (
    <View style={[styles.inner, compact && styles.compact]}>
      <BrandHeader />
      {showConnection ? <ConnectionStrip /> : null}
      {children}
    </View>
  );

  return (
    <ThemedView style={styles.root}>
      <SafeAreaView style={styles.safe} edges={['top', 'left', 'right']}>
        {scroll ? (
          <ScrollView
            style={styles.scroller}
            contentContainerStyle={styles.scroll}
            keyboardShouldPersistTaps="handled"
            contentInsetAdjustmentBehavior="automatic">
            {body}
          </ScrollView>
        ) : (
          body
        )}
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  root: { flex: 1 },
  safe: { flex: 1, alignItems: 'center' },
  scroller: { width: '100%' },
  scroll: {
    paddingHorizontal: 20,
    paddingTop: 18,
    paddingBottom: 40,
    maxWidth: MaxContentWidth,
    width: '100%',
    alignSelf: 'center',
    gap: 20,
  },
  inner: { gap: 20, width: '100%', minWidth: 0 },
  compact: { gap: Spacing.three },
});
