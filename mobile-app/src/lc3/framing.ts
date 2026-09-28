import {
  ATT_OPCODE_HANDLE_OVERHEAD,
  DEFAULT_ATT_MTU,
  FLAG_END,
  FLAG_START,
  HEADER_SIZE,
  MAX_JSON_BYTES,
  TRANSPORT_VERSION,
} from '@/lc3/constants';
import { Lc3FramingError } from '@/lc3/errors';

export function jsonPayloadCapacity(attMtu: number = DEFAULT_ATT_MTU): number {
  return Math.max(0, attMtu - ATT_OPCODE_HANDLE_OVERHEAD - HEADER_SIZE);
}

function writeHeader(
  view: DataView,
  flags: number,
  frameId: number,
  total: number,
  offset: number,
): void {
  view.setUint8(0, TRANSPORT_VERSION);
  view.setUint8(1, flags);
  view.setUint16(2, frameId, false);
  view.setUint16(4, total, false);
  view.setUint16(6, offset, false);
}

export function encodeFrame(
  json: string,
  frameId: number,
  attMtu: number = DEFAULT_ATT_MTU,
): Uint8Array[] {
  if (!Number.isInteger(frameId) || frameId < 1 || frameId > 0xffff) {
    throw new Lc3FramingError('Frame ID must be 1..65535');
  }

  const payload = new TextEncoder().encode(json);
  if (payload.length > MAX_JSON_BYTES) {
    throw new Lc3FramingError(`JSON exceeds ${MAX_JSON_BYTES} bytes`);
  }
  if (payload.length === 0) {
    throw new Lc3FramingError('JSON payload is empty');
  }

  const capacity = jsonPayloadCapacity(attMtu);
  if (capacity <= 0) {
    throw new Lc3FramingError('ATT MTU too small for LC3 framing');
  }

  const fragments: Uint8Array[] = [];
  let offset = 0;
  while (offset < payload.length) {
    const end = Math.min(offset + capacity, payload.length);
    const slice = payload.subarray(offset, end);
    let flags = 0;
    if (offset === 0) flags |= FLAG_START;
    if (end === payload.length) flags |= FLAG_END;

    const packet = new Uint8Array(HEADER_SIZE + slice.length);
    writeHeader(new DataView(packet.buffer), flags, frameId, payload.length, offset);
    packet.set(slice, HEADER_SIZE);
    fragments.push(packet);
    offset = end;
  }
  return fragments;
}

export type AssembledFrame = {
  frameId: number;
  json: string;
};

export class FrameAssembler {
  private current: {
    frameId: number;
    total: number;
    received: number;
    bytes: Uint8Array;
  } | null = null;

  reset(): void {
    this.current = null;
  }

  push(packet: Uint8Array): AssembledFrame | null {
    try {
      return this.pushInner(packet);
    } catch (error) {
      this.reset();
      throw error;
    }
  }

  private pushInner(packet: Uint8Array): AssembledFrame | null {
    if (packet.byteLength < HEADER_SIZE) {
      throw new Lc3FramingError('Fragment shorter than header');
    }

    const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
    const version = view.getUint8(0);
    const flags = view.getUint8(1);
    const frameId = view.getUint16(2, false);
    const total = view.getUint16(4, false);
    const offset = view.getUint16(6, false);
    const payload = packet.subarray(HEADER_SIZE);

    if (version !== TRANSPORT_VERSION) {
      throw new Lc3FramingError(`Unsupported transport version ${version}`);
    }
    if ((flags & ~0x03) !== 0) {
      throw new Lc3FramingError('Reserved flag bits must be zero');
    }
    if (frameId === 0) {
      throw new Lc3FramingError('Frame ID zero is invalid');
    }
    if (total === 0 || total > MAX_JSON_BYTES) {
      throw new Lc3FramingError('Invalid total JSON length');
    }

    const start = (flags & FLAG_START) !== 0;
    const end = (flags & FLAG_END) !== 0;

    if (start) {
      if (offset !== 0) {
        throw new Lc3FramingError('START fragment must have offset 0');
      }
      if (this.current) {
        throw new Lc3FramingError('Interleaved frame rejected');
      }
      this.current = {
        frameId,
        total,
        received: 0,
        bytes: new Uint8Array(total),
      };
    } else if (!this.current || this.current.frameId !== frameId) {
      throw new Lc3FramingError('Fragment does not continue the current frame');
    } else if (this.current.total !== total) {
      throw new Lc3FramingError('Total length changed mid-frame');
    }

    if (offset !== this.current.received) {
      throw new Lc3FramingError('Fragments must be contiguous and in order');
    }
    if (offset + payload.length > this.current.total) {
      throw new Lc3FramingError('Fragment overruns total length');
    }

    this.current.bytes.set(payload, offset);
    this.current.received += payload.length;

    if (end) {
      if (this.current.received !== this.current.total) {
        throw new Lc3FramingError('END fragment did not reach total length');
      }
      const json = new TextDecoder().decode(this.current.bytes);
      const assembled = { frameId: this.current.frameId, json };
      this.reset();
      return assembled;
    }

    return null;
  }
}
