export const LC3_SERVICE_UUID = '6c433000-7d2e-4f65-9f3b-2a1c00000001';
export const LC3_COMMAND_UUID = '6c433001-7d2e-4f65-9f3b-2a1c00000001';
export const LC3_EVENT_UUID = '6c433002-7d2e-4f65-9f3b-2a1c00000001';

export const TRANSPORT_VERSION = 1;
export const PROTOCOL_VERSION = 1;
export const FLAG_START = 1 << 0;
export const FLAG_END = 1 << 1;
export const HEADER_SIZE = 8;
export const ATT_OPCODE_HANDLE_OVERHEAD = 3;
export const MAX_JSON_BYTES = 512;
export const DEFAULT_ATT_MTU = 23;
export const REQUEST_TIMEOUT_MS = 8000;

export const MIN_REQUEST_FRAME_ID = 1;
export const MAX_REQUEST_FRAME_ID = 0x7fff;
export const MIN_EVENT_FRAME_ID = 0x8000;
export const MAX_EVENT_FRAME_ID = 0xffff;

export const CREW_ADDRESS = 65535;
export const MIN_NODE_ADDRESS = 1;
export const MAX_NODE_ADDRESS = 65534;

export const MAX_MESSAGE_TEXT = 80;
export const MESSAGE_PREVIEW_LENGTH = 24;
export const PEER_PAGE_LIMIT = 3;
export const MESSAGE_PAGE_LIMIT = 2;

export const ADVERTISED_NAME_RE = /^CL3-(\d+)-(.*)$/;

export const PRINTABLE_ASCII_RE = /^[\x20-\x7E]+$/;
