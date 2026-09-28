import type { Lc3ErrorCode } from '@/lc3/types';

export class Lc3Error extends Error {
  readonly code: Lc3ErrorCode | string;

  constructor(code: Lc3ErrorCode | string, message: string) {
    super(message);
    this.name = 'Lc3Error';
    this.code = code;
  }
}

export class Lc3ApiError extends Lc3Error {
  constructor(code: Lc3ErrorCode | string, message: string) {
    super(code, message);
    this.name = 'Lc3ApiError';
  }
}

export class Lc3TimeoutError extends Lc3Error {
  readonly op: string;
  readonly frameId: number;

  constructor(op: string, frameId: number) {
    super('TIMEOUT', `Request ${op} timed out`);
    this.name = 'Lc3TimeoutError';
    this.op = op;
    this.frameId = frameId;
  }
}

export class Lc3FramingError extends Lc3Error {
  constructor(message: string) {
    super('FRAMING', message);
    this.name = 'Lc3FramingError';
  }
}
