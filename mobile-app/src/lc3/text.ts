import {
  CREW_ADDRESS,
  MAX_MESSAGE_TEXT,
  MAX_NODE_ADDRESS,
  MIN_NODE_ADDRESS,
  PRINTABLE_ASCII_RE,
} from '@/lc3/constants';

export function isPrintableAscii(text: string): boolean {
  return text.length > 0 && text.length <= MAX_MESSAGE_TEXT && PRINTABLE_ASCII_RE.test(text);
}

export function isValidDestination(address: number, localAddress?: number): boolean {
  if (address === CREW_ADDRESS) return true;
  if (!Number.isInteger(address) || address < MIN_NODE_ADDRESS || address > MAX_NODE_ADDRESS) {
    return false;
  }
  if (localAddress !== undefined && address === localAddress) return false;
  return true;
}

export function parseAdvertisedName(name: string | null | undefined): { address: number; nodeName: string } | null {
  if (!name) return null;
  const match = /^CL3-(\d+)-(.*)$/.exec(name);
  if (!match) return null;
  const address = Number(match[1]);
  if (!Number.isInteger(address) || address < MIN_NODE_ADDRESS || address > MAX_NODE_ADDRESS) {
    return null;
  }
  return { address, nodeName: match[2] };
}
