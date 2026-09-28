import '@/global.css';

import { Platform } from 'react-native';

export const Colors = {
  light: {
    text: '#171717',
    background: '#F5F3EE',
    backgroundElement: '#EBE7DF',
    backgroundSelected: '#F9DDD0',
    textSecondary: '#77736D',
    accent: '#F26A2E',
    accentMuted: '#FBE8DF',
    success: '#26734D',
    successMuted: '#E2EFE6',
    border: '#D8D2C8',
    warning: '#A65B16',
    warningMuted: '#F6E9D2',
    danger: '#B43C35',
    dangerMuted: '#F8E4E1',
    inverse: '#171717',
  },
  dark: {
    text: '#F5F3EE',
    background: '#171717',
    backgroundElement: '#292622',
    backgroundSelected: '#43352D',
    textSecondary: '#B7B0A6',
    accent: '#F26A2E',
    accentMuted: '#4A2B20',
    success: '#77C69A',
    successMuted: '#1E3B2C',
    border: '#4B453F',
    warning: '#F0A45D',
    warningMuted: '#432C19',
    danger: '#F07A72',
    dangerMuted: '#44231F',
    inverse: '#171717',
  },
} as const;

export type ThemeColor = keyof typeof Colors.light & keyof typeof Colors.dark;

export const Fonts = Platform.select({
  ios: {
    sans: 'system-ui',
    serif: 'ui-serif',
    rounded: 'ui-rounded',
    mono: 'ui-monospace',
  },
  default: {
    sans: 'normal',
    serif: 'serif',
    rounded: 'normal',
    mono: 'monospace',
  },
  web: {
    sans: 'var(--font-display)',
    serif: 'var(--font-serif)',
    rounded: 'var(--font-rounded)',
    mono: 'var(--font-mono)',
  },
});

export const Spacing = {
  half: 2,
  one: 4,
  two: 8,
  three: 16,
  four: 24,
  five: 32,
  six: 64,
} as const;

export const Radius = { small: 10, medium: 16, large: 24, pill: 999 } as const;

export const MaxContentWidth = 720;
