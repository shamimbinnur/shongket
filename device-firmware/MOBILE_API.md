# LC3 Mobile BLE API v1

This is the complete client contract for building an Android or iOS companion
app for an LC3 handheld. The transport is Bluetooth Low Energy (BLE), not HTTP.
One securely bonded phone can read the handheld's node, crew directory, local
message history, GPS, and diagnostics, and can originate mesh messages through
the handheld.

The ESP32 handheld is the source of truth. The phone does not join the LoRa
mesh directly and never receives the crew AES key or raw LC3 radio frames.

## Contract at a glance

| Item | Value |
| --- | --- |
| API version | `1` |
| Transport | BLE GATT with encrypted, MITM-protected characteristics |
| Application encoding | UTF-8 JSON, fragmented with an 8-byte binary header |
| Maximum JSON document | 512 bytes |
| Request frame IDs | `1..32767` (`0x0001..0x7FFF`) |
| Event frame IDs | `32768..65535` (`0x8000..0xFFFF`) |
| Node addresses | `1..65534`; `65535` (`0xFFFF`) means all crew |
| Message text | 1–80 printable ASCII bytes (`0x20..0x7E`) |
| Peer directory | At most 11 other nodes; self is not in `list_peers` |
| Message history | Newest-first, at most 20 records, RAM only |
| Pairing window | 60 seconds, opened physically on the handheld |
| Trusted phones | One |

## 1. BLE discovery, pairing, and reconnection

### Peripheral identity

The advertised device name is:

```text
CL3-<decimal node address>-<device name>
```

For example, node 42 named `ALPHA` advertises as `CL3-42-ALPHA`.

Do not identify a device only by its BLE MAC address. Mobile operating systems
may hide it, and bonded devices may reconnect with private addresses. Discover
the LC3 service UUID and use `get_node` after authentication for stable app
identity. A useful application identity is `crewId + hardwareId`; the node
address alone can be reconfigured or conflicted.

### First pairing

Pairing requires physical access to the handheld:

1. On the handheld, open `4 Bluetooth`.
2. Select `Pair new phone` and press `D`.
3. The handheld advertises for 60 seconds and displays a random six-digit
   passkey on its TFT.
4. In the app, scan for the LC3 service, connect, discover characteristics,
   and enable Event notifications.
5. Complete the operating system's BLE Secure Connections passkey flow using
   the code displayed on the handheld.
6. Store only the OS-managed bond. Do not store or request the crew key.

Pairing uses BLE Secure Connections, MITM/passkey protection, a 16-byte key,
and persistent bonding. The passkey is never written to the serial log.

The peripheral does not advertise when it has no bond and no pairing window.
An app therefore cannot remotely open first-time pairing. When one bond exists,
the device advertises whenever that phone is disconnected. A second phone is
not accepted.

### Replacing or recovering a phone

On the handheld, select `Forget trusted phone`, then press `D` twice within
five seconds. This disconnects the current phone and erases stored bonds. Open
a new pairing window afterward.

If the ESP32 bond store contains more than one bond, the handheld reports a
bond fault and does not advertise. Use the same confirmed forget action to
clear it.

### Platform notes

- Ask for the platform's required Bluetooth permissions at runtime. Android
  permission names vary by OS release; iOS provides its own pairing UI.
- Let the OS own encryption and bond keys. No application-layer encryption is
  required inside the BLE link.
- Negotiate/request the largest supported ATT MTU before exchanging frames.
  The firmware requests MTU 247, but the app must work at the default MTU 23.
- Enable Event notifications before sending `hello`; all responses also arrive
  on the Event characteristic.
- Use a GATT write with response for Command. Serialize writes and wait for the
  platform's write completion before writing the next fragment.
- On disconnect, discard partial incoming and outgoing frames and outstanding
  request promises. Reconnect, subscribe, call `hello`, and resynchronize.

## 2. GATT service

UUID comparison should be case-insensitive.

| Item | UUID | Required properties/security |
| --- | --- | --- |
| LC3 mobile service | `6c433000-7d2e-4f65-9f3b-2a1c00000001` | Primary service |
| Command | `6c433001-7d2e-4f65-9f3b-2a1c00000001` | Write; encrypted + MITM |
| Event | `6c433002-7d2e-4f65-9f3b-2a1c00000001` | Notify; encrypted + MITM |
| Event CCCD | Standard `0x2902` | Read/write; encrypted + MITM |

The phone writes request fragments to Command. The handheld sends both
correlated responses and unsolicited events as Event notifications. There is
no readable snapshot characteristic and no HTTP/cloud endpoint.

## 3. Binary fragment transport

One GATT write or notification contains exactly one fragment. Multi-byte
integers are unsigned and big-endian (network byte order).

| Offset | Bytes | Field | Rules |
| ---: | ---: | --- | --- |
| 0 | 1 | transport version | Must be `1` |
| 1 | 1 | flags | bit 0 `START`, bit 1 `END`; all other bits zero |
| 2 | 2 | frame ID | Nonzero; request/response or event range below |
| 4 | 2 | total length | Total UTF-8 JSON bytes, `1..512` |
| 6 | 2 | offset | Payload byte offset in the JSON document |
| 8 | remaining | payload | One non-empty contiguous slice of JSON bytes |

Frame ID ranges:

- The phone chooses request IDs `0x0001..0x7FFF` and should increment with
  wraparound back to `1`.
- A response uses exactly the request's frame ID.
- The device chooses event IDs `0x8000..0xFFFF` and wraps within that range.
- Frame ID `0` is invalid.

Frame construction rules:

- The first fragment has `START` and offset `0`.
- Every next fragment has the same frame ID and total length, and its offset is
  exactly the previous offset plus previous payload length.
- The final fragment has `END` and ends exactly at total length.
- A one-fragment frame has both `START | END` (`0x03`).
- Fragments for different frames cannot be interleaved in either direction.
- JSON size is measured in UTF-8 bytes, not characters.
- Zero-length JSON and zero-length fragment payloads are invalid.

The usable GATT value length is normally `ATT_MTU - 3`. Subtract the 8-byte
LC3 header to get fragment payload capacity:

```text
fragmentPayloadCapacity = (ATT_MTU - 3) - 8
```

At MTU 23, a 20-byte GATT value carries 12 JSON bytes. At MTU 247, a 244-byte
value carries 236 JSON bytes. The firmware caps its outgoing GATT value buffer
at 247 bytes even if a larger MTU is reported, so a client should accept values
up to 247 bytes and should not depend on MTUs above 247.

### Sender pseudocode

```text
jsonBytes = UTF8.encode(compactJson)
assert 1 <= jsonBytes.length <= 512
offset = 0

while offset < jsonBytes.length:
    count = min(gattValueCapacity - 8, jsonBytes.length - offset)
    flags = 0
    if offset == 0: flags |= 0x01
    if offset + count == jsonBytes.length: flags |= 0x02

    packet = [1, flags]
           + uint16BE(frameId)
           + uint16BE(jsonBytes.length)
           + uint16BE(offset)
           + jsonBytes[offset : offset + count]

    writeCommandWithResponse(packet)
    waitForWriteCompletion()
    offset += count
```

### Receiver pseudocode

```text
onNotification(packet):
    require packet.length > 8
    parse version, flags, frameId, totalLength, offset
    require version == 1 and unknown flag bits == 0
    require frameId != 0 and 1 <= totalLength <= 512

    if START:
        require offset == 0
        reset reassembler to (frameId, totalLength)

    require active frame matches frameId and totalLength
    require offset == bytesReceived
    append payload
    require END exactly when bytesReceived == totalLength

    if END:
        decode complete bytes as UTF-8 JSON
        if frameId <= 0x7FFF: resolve matching request
        else: process event
```

Malformed or out-of-order inbound fragments reset the handheld reassembler and
increment `bleRxDrops`; they do **not** produce a JSON error. A saturated input
queue behaves the same way. The app observes a request timeout.

The handheld has room for three complete pending requests and eight outbound
JSON frames. Avoid pipelining: one outstanding request at a time is the safest
portable client behavior. Notifications are sent FIFO, at most one fragment
about every 12 ms while connected and authorized.

## 4. JSON envelope and data conventions

Send compact JSON with no unnecessary whitespace. Every request has:

```json
{"v":1,"op":"get_node","args":{}}
```

`v` must be numeric `1`. `op` is case-sensitive. `args` should be an object;
use `{}` when an operation has no arguments. Unknown fields are ignored.

A successful response has:

```json
{"v":1,"ok":true,"data":{}}
```

An application error has:

```json
{"v":1,"ok":false,"error":{"code":"NOT_FOUND","message":"Peer not found"}}
```

Do not depend on human-readable `message` text. Branch on `error.code`.

### Shared types

| Type | JSON representation |
| --- | --- |
| `NodeAddress` | integer `1..65534`; `65535` only for all-crew destination |
| `MessageId` | unsigned 32-bit integer (`1..4294967295` in normal records) |
| `Revision` | unsigned 32-bit integer; opaque change counter |
| `BootSession` | 14 uppercase hex characters encoding 7 bytes |
| `HardwareId` | 12 uppercase hex characters encoding 6 bytes |
| `CrewId` | 8 uppercase hex characters; identifier, not a secret key |
| `Latitude` | JSON number in decimal degrees, nominally `-90..90` |
| `Longitude` | JSON number in decimal degrees, nominally `-180..180` |
| `RSSI` | signed integer in dBm |

All `uptime` and `time` fields are seconds since the relevant handheld boot.
They are not Unix timestamps. `ageSeconds`, `lastSeenAge`, and peer location
`age` are relative ages computed by the connected handheld and are suitable for
display. Treat unsigned counters as wrapping values.

Fields documented as optional are omitted, not set to JSON `null`.

## 5. Recommended connection and synchronization flow

After every secure connection:

1. Discover the service/characteristics and enable Event notifications.
2. Call `hello`.
3. Compare `session` with the cached boot session. If it changed, discard
   uptime-based assumptions and any cached revision/event-sequence state.
4. Fetch `get_node`, `get_gps`, the peer pages, and the message pages needed by
   the UI. Fetch `get_status` only when diagnostics are shown.
5. Record `eventSeq`, `peerRevision`, and `messageRevision` only after the
   corresponding snapshots are committed.
6. Process later events as invalidation hints and refetch the relevant data.

Snapshot pagination is not transactional. If a peer or message revision changes
while paging, discard the partly assembled list and restart at offset 0. Never
derive the next offset as `offset + requestedLimit`; use returned `nextOffset`.

On `resync_required`, unknown event name, event sequence gap, invalid event
payload, or reconnect, call `hello` and refetch snapshots. Event frame IDs only
separate transport traffic; use JSON `seq` plus `hello.session` for ordering.

## 6. Operations

### `hello`

Use immediately after subscribing. No arguments.

Request:

```json
{"v":1,"op":"hello","args":{}}
```

Response data:

| Field | Type | Meaning |
| --- | --- | --- |
| `api` | integer | `1` |
| `protocol` | string | `"LC3"` |
| `session` | `BootSession` | Changes on handheld reboot |
| `eventSeq` | integer | Most recently allocated event sequence |
| `peerRevision` | `Revision` | Current peer directory revision |
| `messageRevision` | `Revision` | Current message-history revision |
| `capabilities` | string[] | Currently `directory`, `messages`, `gps`, `status` |

Example:

```json
{"v":1,"ok":true,"data":{"api":1,"protocol":"LC3","session":"A1B2C3D4E5F607","eventSeq":12,"peerRevision":9,"messageRevision":4,"capabilities":["directory","messages","gps","status"]}}
```

Clients should tolerate unknown capabilities and missing future optional ones.

### `get_node`

Returns connected-handheld identity and summary state. No arguments.

```json
{"v":1,"op":"get_node","args":{}}
```

| Data field | Type | Meaning |
| --- | --- | --- |
| `address` | `NodeAddress` | Local mesh address |
| `name` | string | Printable name, 1–12 characters |
| `hardwareId` | `HardwareId` | Six-byte physical fingerprint |
| `crewId` | `CrewId` | Crew identifier; not the AES key |
| `uptime` | integer | Local uptime seconds |
| `radio` | boolean | LoRa initialization succeeded |
| `crypto` | boolean | AES-CCM initialized/self-tested |
| `config` | boolean | Local compile-time configuration valid |
| `addressConflict` | boolean | Another hardware fingerprint uses this address |
| `online` | integer | Online crew count including this node (`1..12`) |
| `unread` | integer | Unread local records (`0..20`) |
| `phoneConnected` | boolean | This BLE connection is authenticated/authorized |

Example response:

```json
{"v":1,"ok":true,"data":{"address":42,"name":"ALPHA","hardwareId":"102030405060","crewId":"89ABCDEF","uptime":3812,"radio":true,"crypto":true,"config":true,"addressConflict":false,"online":4,"unread":2,"phoneConnected":true}}
```

Disable message sending when `radio`, `crypto`, or `config` is false, or when
`addressConflict` is true.

### `list_peers`

Returns other crew nodes, online first and then by numeric address. Self is not
included.

Arguments:

| Argument | Type | Default | Behavior |
| --- | --- | ---: | --- |
| `offset` | nonnegative integer | 0 | Newest snapshot list index; past end returns no items |
| `limit` | integer | 3 | Clamped to `1..3`; `0` becomes 1 |

```json
{"v":1,"op":"list_peers","args":{"offset":0,"limit":3}}
```

Response data:

| Field | Type | Meaning |
| --- | --- | --- |
| `revision` | `Revision` | Directory revision represented by this page |
| `total` | integer | Total known peers (`0..11`) |
| `items` | `PeerSummary[]` | Zero to three entries |
| `nextOffset` | integer, optional | Pass unchanged to the next call |

`PeerSummary`:

| Field | Type | Meaning |
| --- | --- | --- |
| `address` | `NodeAddress` | Peer address |
| `name` | string | Peer name |
| `online` | boolean | Seen within the 120-second online window |
| `conflict` | boolean | Multiple physical nodes advertised this address |
| `hop` | integer | Most recently observed mesh hop count (`0..3`) |
| `rssi` | integer | RSSI of the most recent last-hop radio packet |
| `location` | boolean | Any current or last-known coordinate is stored |

Example response:

```json
{"v":1,"ok":true,"data":{"revision":9,"total":4,"items":[{"address":7,"name":"BRAVO","online":true,"conflict":false,"hop":1,"rssi":-78,"location":true},{"address":19,"name":"CHARLIE","online":true,"conflict":false,"hop":2,"rssi":-93,"location":false},{"address":55,"name":"DELTA","online":false,"conflict":false,"hop":1,"rssi":-86,"location":true}],"nextOffset":3}}
```

### `get_peer`

Argument: required `address` from `list_peers`.

```json
{"v":1,"op":"get_peer","args":{"address":7}}
```

Response data:

| Field | Type | Meaning |
| --- | --- | --- |
| `address` | `NodeAddress` | Peer address |
| `name` | string | Peer name |
| `online` | boolean | Current online classification |
| `conflict` | boolean | Address-conflict state |
| `lastSeenAge` | integer | Seconds since any authenticated traffic from peer |
| `hop` | integer | Last observed hop count |
| `lastRelay` | integer | Address of the last transmitter/relay |
| `rssi` | integer | Last-hop RSSI in dBm |
| `location` | object | Location object below |

Location always has `available`. Other fields are present only as described:

| Location field | Type | Presence/meaning |
| --- | --- | --- |
| `available` | boolean | Always; whether a coordinate is stored |
| `current` | boolean | When available; validity reported in last presence |
| `lat`, `lon` | number | When available; decimal degrees |
| `satellites` | integer | When available; last reported satellite count |
| `age` | integer | When available; coordinate age in seconds |
| `distanceM` | integer | When available **and local GPS is current** |
| `bearing` | integer | When available **and local GPS is current**; true-north degrees |

Example response with relative position:

```json
{"v":1,"ok":true,"data":{"address":7,"name":"BRAVO","online":true,"conflict":false,"lastSeenAge":18,"hop":1,"lastRelay":7,"rssi":-78,"location":{"available":true,"current":true,"lat":23.8103,"lon":90.4125,"satellites":9,"age":3,"distanceM":428,"bearing":71}}}
```

If no coordinate exists, `location` is exactly equivalent to
`{"available":false}`. A missing peer returns `NOT_FOUND`.

### `list_messages`

Returns local RAM history newest-first. The history combines incoming and
outgoing messages and overwrites the oldest record after 20 entries.

Arguments:

| Argument | Type | Default | Behavior |
| --- | --- | ---: | --- |
| `offset` | nonnegative integer | 0 | Newest-first list index |
| `limit` | integer | 2 | Clamped to `1..2`; `0` becomes 1 |

```json
{"v":1,"op":"list_messages","args":{"offset":0,"limit":2}}
```

Response data contains `revision`, `total`, `items`, and optional `nextOffset`
with the same pagination semantics as `list_peers`. `total` is `0..20`.

`MessageSummary`:

| Field | Type | Meaning |
| --- | --- | --- |
| `origin` | `NodeAddress` | Original sender; part of the stable lookup key |
| `id` | `MessageId` | Origin-generated ID; part of the stable lookup key |
| `dir` | enum string | `in` or `out` |
| `kind` | enum string | `direct` or `crew` |
| `peer` | integer | Counterparty address, or `65535` for all crew |
| `name` | string | Counterparty name, or `ALL CREW` |
| `preview` | string | First 15 message characters |
| `unread` | boolean | Normally true only for unread incoming records |
| `delivery` | enum string | `received`, `sending`, `delivered`, or `failed` |
| `ageSeconds` | integer | Time since stored locally |

Example response:

```json
{"v":1,"ok":true,"data":{"revision":14,"total":6,"items":[{"origin":42,"id":101,"dir":"out","kind":"direct","peer":7,"name":"BRAVO","preview":"Meet at gate 2","unread":false,"delivery":"delivered","ageSeconds":31},{"origin":19,"id":88,"dir":"in","kind":"crew","peer":19,"name":"CHARLIE","preview":"Weather turning","unread":true,"delivery":"received","ageSeconds":95}],"nextOffset":2}}
```

Always retain both `origin` and `id`. Message IDs are not globally unique by
themselves. Records can disappear when the ring buffer rolls over or the device
reboots.

### `get_message`

Arguments: required `origin` and `id` from a summary.

```json
{"v":1,"op":"get_message","args":{"origin":19,"id":88}}
```

Returns all `MessageSummary` fields plus:

| Field | Type | Meaning |
| --- | --- | --- |
| `text` | string | Full 1–80 character message |
| `source` | enum string | `radio`, `keypad`, or `mobile` |
| `time` | integer | Sender-origin uptime seconds, not wall-clock time |

```json
{"v":1,"ok":true,"data":{"origin":19,"id":88,"dir":"in","kind":"crew","peer":19,"name":"CHARLIE","preview":"Weather turning","unread":true,"delivery":"received","ageSeconds":95,"text":"Weather turning. Return to base.","source":"radio","time":7201}}
```

`time` for an incoming record belongs to the remote sender's boot and is not
comparable to local uptime. Prefer `ageSeconds` in the UI. Missing/overwritten
records return `NOT_FOUND`.

### `get_gps`

Returns the handheld hardware GPS. The API does not accept phone GPS.

```json
{"v":1,"op":"get_gps","args":{}}
```

| Data field | Type | Meaning |
| --- | --- | --- |
| `state` | enum string | `FIX`, `STALE`, or `SEARCH` |
| `valid` | boolean | Receiver has ever supplied a valid stored coordinate |
| `current` | boolean | Valid coordinate age is at most 10,000 ms |
| `satellites` | integer | Satellite count, or 0 when unavailable |
| `ageMs` | integer | Coordinate age; `4294967295` when invalid |
| `lat`, `lon` | number, optional | Present whenever `valid` is true |

Current fix example:

```json
{"v":1,"ok":true,"data":{"state":"FIX","valid":true,"current":true,"satellites":9,"ageMs":842,"lat":23.8103,"lon":90.4125}}
```

Search example:

```json
{"v":1,"ok":true,"data":{"state":"SEARCH","valid":false,"current":false,"satellites":0,"ageMs":4294967295}}
```

### `get_status`

Returns radio configuration and boot-lifetime diagnostics.

```json
{"v":1,"op":"get_status","args":{}}
```

| Data field | Type | Meaning |
| --- | --- | --- |
| `radio` | boolean | LoRa initialized |
| `frequency` | integer | Hertz; currently `433000000` |
| `syncWord` | integer | Currently `74` (`0x4A`) |
| `sf` | integer | Spreading factor; currently 9 |
| `bandwidth` | integer | Hertz; currently `125000` |
| `codingRate` | string | Currently `4/5` |
| `tx` | integer | Locally originated message count |
| `rx` | integer | Received message count |
| `relays` | integer | Relayed packet count |
| `retries` | integer | Message retry count |
| `duplicates` | integer | Duplicate flood count |
| `malformed` | integer | Malformed radio frames |
| `authFailures` | integer | AES-CCM authentication failures |
| `wrongCrew` | integer | Frames rejected for another crew ID |
| `queueDrops` | integer | Mesh queue saturation drops |
| `bleRxDrops` | integer | BLE framing/auth/input-queue drops |
| `bleTxDrops` | integer | BLE output queue/encoding drops |
| `lastPacket` | object | Most recent accepted packet diagnostics |

`lastPacket` has integer `type`, `origin`, `relay`, `hop`, and signed `rssi`.
Packet type values are `0` invalid/none, `1` presence, `2` direct message, `3`
crew message, and `4` delivery acknowledgment.

```json
{"v":1,"ok":true,"data":{"radio":true,"frequency":433000000,"syncWord":74,"sf":9,"bandwidth":125000,"codingRate":"4/5","tx":4,"rx":11,"relays":17,"retries":1,"duplicates":26,"malformed":0,"authFailures":0,"wrongCrew":3,"queueDrops":0,"bleRxDrops":0,"bleTxDrops":0,"lastPacket":{"type":1,"origin":7,"relay":7,"hop":0,"rssi":-76}}}
```

These are diagnostic counters, not delivery or analytics records.

### `send_message`

Queues one locally originated direct or all-crew message.

Arguments:

| Argument | Type | Rules |
| --- | --- | --- |
| `destination` | integer | Direct node `1..65534`, or `65535` for all crew |
| `text` | string | 1–80 bytes; every byte printable ASCII `0x20..0x7E` |

```json
{"v":1,"op":"send_message","args":{"destination":7,"text":"Meet at gate 2"}}
```

Accepted response:

```json
{"v":1,"ok":true,"data":{"messageId":101,"state":"sending"}}
```

Success means queued, not delivered. The outgoing record is added immediately.
Observe `messages_changed`, then refetch that record with lookup key
`(local node address, messageId)` until `delivery` is `delivered` or `failed`.

Only one locally originated message—from keypad or phone—can await delivery at
a time. Direct recipients must be known, online, non-conflicted, and not self.
An all-crew message is considered delivered when at least one other node gives
mesh delivery evidence; it does not prove every crew member received it. Both
direct and crew modes attempt at most three transmissions with a six-second
evidence window per attempt.

Do not blindly retry after a timeout. The request may have been accepted while
its response was dropped. First refetch the newest message summaries, call
`get_message` for plausible outgoing candidates, and look for a `source:
mobile` record with matching text/destination and plausible age. Only send
again after the application or user determines it was not accepted. The API
has no caller-supplied idempotency key in v1.

### `mark_read`

Marks one local record read. Arguments are required `origin` and `id` from the
message summary.

```json
{"v":1,"op":"mark_read","args":{"origin":19,"id":88}}
```

```json
{"v":1,"ok":true,"data":{"unread":1}}
```

The call is idempotent for an existing record. An overwritten/missing record
returns `NOT_FOUND`.

### `mark_all_read`

Marks every local history record read. No arguments.

```json
{"v":1,"op":"mark_all_read","args":{}}
```

```json
{"v":1,"ok":true,"data":{"changed":2,"unread":0}}
```

`changed` is the number of records changed by this call.

### `ping`

Health/round-trip check. Optional `echo` may be any small JSON value.

```json
{"v":1,"op":"ping","args":{"echo":"mobile-123"}}
```

```json
{"v":1,"ok":true,"data":{"uptime":3812,"echo":"mobile-123"}}
```

The returned `uptime` is local seconds. A large echo can exceed the 512-byte
response limit and return `RESPONSE_TOO_LARGE`.

## 7. Asynchronous events

Every event JSON document has:

```json
{"v":1,"event":"messages_changed","seq":13,"data":{}}
```

`seq` increases once for every successfully queued event during the current
handheld boot and can wrap as an unsigned 32-bit value. It is independent of
the event transport frame ID. Events are invalidation hints, not a second
database or a guaranteed event log.

### `peers_changed`

```json
{"v":1,"event":"peers_changed","seq":13,"data":{"revision":10,"online":5}}
```

| Field | Meaning |
| --- | --- |
| `revision` | New directory revision |
| `online` | Online count including the local node |

Restart/refetch peer pages. Revisions may jump when updates are coalesced.

### `messages_changed`

```json
{"v":1,"event":"messages_changed","seq":14,"data":{"revision":15,"count":7,"unread":3}}
```

| Field | Meaning |
| --- | --- |
| `revision` | New message history revision |
| `count` | Current record count (`0..20`) |
| `unread` | Current unread count |

Refetch at least the newest message page. Delivery changes and read-state
changes both advance the message revision.

### `gps_changed`

```json
{"v":1,"event":"gps_changed","seq":15,"data":{"state":"FIX","current":true,"satellites":9,"lat":23.8103,"lon":90.4125,"ageMs":842}}
```

The event is emitted at most once per second. Data always contains `state`,
`current`, and `satellites`. `lat`, `lon`, and `ageMs` are present only when a
valid stored coordinate exists. Unlike `get_gps`, this event has no `valid`
field. Refetch `get_gps` if the UI needs its complete schema.

### `node_changed`

```json
{"v":1,"event":"node_changed","seq":16,"data":{"online":5,"unread":3,"delivery":"Idle","radio":true,"conflict":false}}
```

| Field | Type/values |
| --- | --- |
| `online` | integer, includes self |
| `unread` | integer |
| `delivery` | `Idle`, `Sending`, `Delivered`, or `Failed` |
| `radio` | boolean |
| `conflict` | boolean |

Note that this event's delivery values are title-case, while message record
delivery values are lowercase. Treat the event as a summary and refetch
`get_node` and/or message data as needed.

### `resync_required`

```json
{"v":1,"event":"resync_required","seq":17,"data":{"reason":"event_queue_overflow"}}
```

The outbound event queue overflowed, so one or more invalidations may be
missing. Call `hello` and refetch all currently cached snapshots.

## 8. Error codes and retry policy

| Code | Meaning | Suggested client behavior |
| --- | --- | --- |
| `INVALID_JSON` | JSON parsing failed | Client bug; do not retry unchanged |
| `UNSUPPORTED_VERSION` | `v` is not numeric 1 | Stop and report incompatibility |
| `UNKNOWN_OPERATION` | `op` is not implemented | Feature/version mismatch |
| `NOT_FOUND` | Peer/message key does not exist | Remove stale cached item or refresh |
| `BUSY` | Another local message awaits delivery | Wait for terminal delivery state |
| `RADIO_UNAVAILABLE` | LoRa or crypto is unavailable | Show offline/diagnostic state |
| `ADDRESS_CONFLICT` | Local or destination address conflicts | Disable send and require device fix |
| `INVALID_DESTINATION` | Address reserved, invalid, or local | Correct selection; do not retry unchanged |
| `PEER_OFFLINE` | Direct recipient unknown or offline | Refresh peers or send later |
| `INVALID_TEXT` | Empty, over 80 bytes, or non-printable ASCII | Validate locally |
| `RESPONSE_TOO_LARGE` | Page/echo cannot fit 512 bytes | Reduce page/echo size |

Framing, authentication, queue saturation, disconnects, and lost notifications
have no JSON error response. Use a request timeout. A practical initial timeout
is 3 seconds after the final request fragment is written; make it configurable
and account for mobile BLE scheduling.

Retry policy:

- Safe to retry after reconnect/resync: `hello`, all `get_*`, `list_*`, and
  `ping` without a stateful echo.
- `mark_read` and `mark_all_read` are operationally idempotent while records
  still exist; retry after refreshing if their response was lost.
- Never automatically retry `send_message` without reconciliation as described
  in its operation section.

## 9. Client data and UI rules

- Treat handheld storage as authoritative but volatile. Peers and all 20
  messages live in RAM and disappear on reboot.
- Do not present message `time` as a date. LC3 v1 has no wall-clock timestamp.
- Do not invent delivery guarantees. `sending`, `delivered`, and `failed` are
  mesh evidence states with the all-crew limitation described above.
- Disable direct send for offline/conflicted peers and disable all send for a
  local conflict or unavailable radio/crypto/config.
- Validate text by UTF-8 byte content, not a UI character count. v1 accepts
  ASCII only, including spaces and punctuation, with no newline/tab/emoji.
- The 15-character `preview` is a convenience, not a unique identifier.
- A peer's `rssi` describes only the last radio hop; it is not end-to-end signal
  quality for a multi-hop peer.
- `bearing` is true north. The handheld has GPS but no compass, so it cannot
  provide phone/device heading.
- Coordinates may be last-known (`current:false`). Show age and stale status.
- Cache sensitive local data in protected app storage and avoid cloud/crash-log
  uploads by default.

## 10. Minimal client architecture

A robust app normally needs these components:

```text
BLE session
  -> scan/connect/bond/subscribe/MTU
  -> serialized Command writer
  -> Event notification receiver

Frame codec
  -> fragment JSON requests
  -> reassemble response/event JSON

Request broker
  -> allocate 1..0x7FFF IDs
  -> correlate response frame IDs
  -> timeout and cancel on disconnect

Repository/store
  -> boot session + revision tracking
  -> node/GPS/peer/message snapshots
  -> event-driven invalidation/refetch

UI
  -> connection/pairing state
  -> crew, messages, GPS/radar, diagnostics
  -> send reconciliation and read-state actions
```

Keep the frame codec independent of Android/iOS BLE APIs so it can be tested
with byte vectors. Keep JSON models tolerant of unknown fields and event names
for forward compatibility.

## 11. Acceptance checklist for a mobile client

- Pair only during the physical 60-second window and reconnect from the stored
  OS bond after both phone and handheld restarts.
- Operate correctly at MTU 23 and at MTU 247.
- Send and receive one-fragment and multi-fragment JSON documents.
- Reject invalid version/flags/offset/length and recover for the next frame.
- Correlate responses by transport frame ID while events arrive between them.
- Cancel partial frames and outstanding requests on disconnect.
- Detect boot-session changes, event-sequence gaps, and `resync_required`.
- Restart pagination when its revision changes midway.
- Render omitted location/GPS fields without assuming `null`.
- Preserve unsigned 32-bit IDs/counters on platforms with signed integer APIs.
- Reconcile a timed-out `send_message` before allowing a retry.
- Handle 11 peers, 20 messages, history rollover, offline peers, stale GPS,
  address conflicts, and every documented error code.
- Never log passkeys, message contents, bond keys, or sensitive coordinates in
  production diagnostics.

## 12. Intentionally unavailable in v1

There is no crew-key/configuration write, raw LoRa access, phone GPS injection,
mesh routing control, firmware update, persistent message archive, cloud
service, companion account, battery level, wall clock, SOS operation, or remote
pairing command.

The LoRa wire format is documented separately in [LC3_PROTOCOL.md](LC3_PROTOCOL.md),
but a normal mobile client does not implement or receive that protocol.
