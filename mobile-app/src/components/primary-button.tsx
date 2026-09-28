import { Pressable, StyleSheet, ViewStyle } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { Radius, Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';

export function PrimaryButton({
  label,
  onPress,
  disabled,
  variant = 'primary',
  compact = false,
  style,
}: {
  label: string;
  onPress: () => void;
  disabled?: boolean;
  variant?: 'primary' | 'secondary' | 'danger';
  compact?: boolean;
  style?: ViewStyle;
}) {
  const theme = useTheme();
  const backgroundColor = variant === 'primary' ? theme.accent : variant === 'danger' ? theme.dangerMuted : theme.backgroundSelected;
  const labelColor = variant === 'primary' ? theme.inverse : variant === 'danger' ? theme.danger : theme.text;
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      style={({ pressed }) => [
        styles.button,
        compact && styles.compact,
        style,
        { backgroundColor, opacity: disabled ? 0.4 : pressed ? 0.72 : 1 },
      ]}>
      <ThemedText type="smallBold" style={{ color: labelColor }}>
        {label}
      </ThemedText>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  button: {
    minHeight: 52,
    paddingVertical: Spacing.two,
    paddingHorizontal: Spacing.three,
    borderRadius: Radius.medium,
    alignItems: 'center',
    justifyContent: 'center',
  },
  compact: { minHeight: 42, paddingVertical: Spacing.two, paddingHorizontal: Spacing.three },
});
