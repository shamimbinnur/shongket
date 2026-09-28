/** Protocol logs never include message bodies, passkeys, or JSON documents. */
export function lc3Log(op: string, details: Record<string, string | number | boolean | null | undefined>): void {
  if (!__DEV__) return;
  const parts = Object.entries(details)
    .filter(([, value]) => value !== undefined)
    .map(([key, value]) => `${key}=${value}`);
  console.log(`[lc3] ${op} ${parts.join(' ')}`.trim());
}
