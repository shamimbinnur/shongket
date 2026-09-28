# LC3 wire protocol

LC3 transports one binary frame per LoRa packet. Multi-byte integers are
unsigned big-endian unless marked signed. Text is printable ASCII without a
terminating NUL. Current limits are 12 name bytes, 80 text bytes, three hops,
and a 140-byte maximum frame.

## Complete frame

```text
clear authenticated header (20) | AES-CCM ciphertext (N) | CCM tag (8)
```

The clear header is authenticated as CCM additional authenticated data (AAD),
so changing a header field invalidates the tag. A receiver checks magic,
structural bounds, and crew ID before attempting decryption. It does not route,
display, acknowledge, or relay anything until CCM authentication and plaintext
validation both succeed.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | ASCII magic `L3` |
| 2 | 4 | Crew ID |
| 6 | 2 | Last transmitter address, 1-65534 |
| 8 | 7 | Last transmitter random boot session |
| 15 | 4 | Last transmitter frame sequence |
| 19 | 1 | Ciphertext/plaintext length `N`, 17-112 |
| 20 | N | Ciphertext |
| 20+N | 8 | CCM authentication tag |

AES-128-CCM uses the shared 16-byte crew key, tag length 8, and nonce length
13. The nonce is the exact concatenation below:

```text
transmitter address (2) | transmitter boot session (7) | frame sequence (4)
```

Every locally encrypted frame consumes the next global frame sequence. A relay
does not reuse an incoming frame: it authenticates/decrypts, updates the logical
hop, and encrypts a new frame under its own address/session/sequence nonce.

## Encrypted logical envelope

Every plaintext begins with this 17-byte envelope:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Packet type |
| 1 | 2 | Original address |
| 3 | 7 | Original boot session |
| 10 | 2 | Destination (`0xFFFF` means all crew) |
| 12 | 4 | Flood ID |
| 16 | 1 | Hop, 0-3 |

Packet type values are `1` Presence, `2` DirectMessage, `3` CrewMessage, and
`4` DeliveryAck. Presence and CrewMessage must target `0xFFFF`. DirectMessage
and DeliveryAck must target a valid individual address.

Routing deduplicates `(type, original address, original boot session, flood
ID)`. A retry creates a new flood ID but preserves its logical message ID.

## Payloads

Payload offsets below begin immediately after the 17-byte envelope.

Presence:

| Relative offset | Size | Field |
| ---: | ---: | --- |
| 0 | 6 | eFuse hardware fingerprint |
| 6 | 1 | Name length `L`, 1-12 |
| 7 | L | Name |
| 7+L | 1 | GPS-valid flag, exactly 0 or 1 |
| 8+L | 4 | Latitude × 10^7, signed two's-complement |
| 12+L | 4 | Longitude × 10^7, signed two's-complement |
| 16+L | 1 | Satellites |
| 17+L | 2 | Reported fix age in seconds |

When GPS-valid is zero, coordinates are ignored. A directory may retain the
last previously valid location while marking it stale.

DirectMessage and CrewMessage:

| Relative offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Logical message ID |
| 4 | 4 | Sender timestamp or uptime seconds |
| 8 | 1 | Text length `L`, 1-80 |
| 9 | L | Text |

DeliveryAck:

| Relative offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Acknowledged logical message ID |

A direct destination creates a DeliveryAck with itself as the ACK origin and
the message origin as destination. The ACK is a new controlled flood and can
return over relays. A CrewMessage has no ACK storm; its origin treats an
authenticated relayed copy of its own announcement as acceptance evidence.

## Compatibility and trust

LC3 deliberately has no LC2 compatibility mode. A different crew ID is ignored
before decryption; a matching ID with the wrong key fails authentication.
Possession of the shared crew key permits decryption and forgery of every LC3
packet, including direct traffic, so the protocol protects against outsiders
but not a modified trusted member device.
