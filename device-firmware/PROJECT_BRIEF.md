# Shongket Project Brief

## What this project is

**Shongket** is firmware for a small private crew communication network. Up to 12 ESP32 handheld devices exchange encrypted messages and presence updates over LoRa radio, without a central server, internet connection, or coordinator. Devices can discover one another automatically, send a direct message or a message to the whole crew, share GPS position, and show nearby crew members on a north-up radar. A securely bonded phone can use BLE to read the handheld's information or ask it to originate a mesh message.

Each handheld combines an ESP32, SX1278 LoRa radio, ST7789 display, keypad, GPS receiver, and buzzer. The firmware also has a 20-record RAM message history, a peer directory, device status screens, and a small BLE GATT API. SOS is explicitly not implemented.

## The core idea: a mesh without routes

In a traditional routed network, nodes learn paths such as “A reaches C through B.” Shongket does not maintain those route tables. It uses **controlled flooding**: a new packet is rebroadcast by eligible devices that hear it, so it can travel through several radio hops even when the sender cannot reach the destination directly.

Think of a person passing a sealed note to everyone nearby. Each person passes it on once, waits a short random time to reduce collisions, and stops when the note has traveled three hops. If another person has already passed along the same note, they cancel their scheduled copy. This lets the network work without a coordinator or fixed topology, at the cost of extra transmissions.

“Mesh transition” can be described as **multi-hop forwarding/relay** in this project. It is not a handoff between networks or a route-selection transition.

## How a packet moves

1. **Create:** The origin builds a logical packet with its address and boot session, destination, a new flood ID, and hop count 0. The logical message ID is separate from the flood ID.
2. **Protect:** The packet is serialized, then encrypted and authenticated with AES-128-CCM using the shared crew key. The 20-byte header is authenticated too. The nonce is transmitter address + random boot session + frame sequence.
3. **Receive and validate:** Every listener checks the frame shape and crew ID, verifies the CCM authentication tag, and validates the decoded packet before taking action. Wrong-crew, malformed, or unauthenticated frames are discarded.
4. **Deduplicate:** A node identifies a flood by `(packet type, original address, original boot session, flood ID)`. A 64-entry seen cache tracks floods for up to 120 seconds. A duplicate is not processed or relayed again.
5. **Act locally:** Presence updates the peer directory. A direct message is stored only by its addressed destination; a crew message is stored by every other member. A destination of a direct message schedules an ACK back to the origin.
6. **Schedule relay:** If the packet is eligible and has fewer than three hops, the node schedules one relay after a randomized delay. ACK relays wait 120–450 ms, ordinary message relays 350–1100 ms, and presence relays 700–1800 ms. If two alternate transmitters are heard for that flood while waiting, the relay is cancelled.
7. **Forward:** At transmit time, the node increments the logical hop count and encrypts a new radio frame using its own transmitter identity, boot session, and next frame sequence. The frame is not copied byte-for-byte because the radio transmitter and nonce must change.
8. **Stop:** No node relays beyond hop 3. Direct messages and ACKs also stop at their addressed destination. A sender retries a message at most three times, using a **new flood ID each attempt** but the same logical message ID, so a receiving device can avoid duplicate history entries.

### Pseudocode for a receiving relay

```text
receive radio frame
if malformed or wrong crew: discard
authenticate and decrypt with AES-CCM
if authentication or plaintext validation fails: discard
if this is my own recently transmitted frame: ignore

key = (type, origin, origin_boot_session, flood_id)
if key is already in seen_cache:
    record alternate transmitter
    if at least 2 alternate transmitters: cancel pending relay
    stop
remember key

process packet if it is relevant to this node
if packet is not terminal here and hop < 3:
    schedule relay after type-specific random delay

when relay is due:
    if already relayed, cancelled, stale, or hop >= 3: stop
    increment hop
    encrypt/authenticate a fresh frame as this transmitter
    transmit once
```

## Delivery semantics (important to explain)

- **Direct message:** “Delivered” means the addressed handheld accepted it and an authenticated ACK returned to the origin. Relays alone are not proof of delivery. After each six-second evidence window, the origin waits a randomized 700–1800 ms before another attempt; it makes at most three attempts.
- **Crew message:** There is no ACK storm. “Delivered” means at least one other node accepted and relayed the announcement. It does **not** mean every crew member received it.
- The receiver stores a repeated logical message only once by checking origin + message ID, even though a retry has a fresh flood ID.

## Security and design limits

- The shared crew AES-128 key protects packets from outsiders who do not have the key. AES-CCM provides encryption and tamper detection; the authenticated header prevents an attacker from changing routing metadata without detection.
- This is a **shared-key group**, not end-to-end private messaging: any trusted device with the crew key can decrypt direct traffic. The firmware does not rotate or persist crew keys.
- The network is limited to 12 devices and three relay hops. Controlled flooding is simple and route-free, but can consume airtime and collide more as traffic or node density grows. Random delays, duplicate suppression, alternate-copy cancellation, hop limits, bounded queues, and message priorities help control this.
- The implementation uses fixed-size caches and queues for predictable embedded memory use. Queue saturation is counted; it can mean a packet cannot be relayed.
- Current radio profile is 433 MHz, SF9, 125 kHz bandwidth, coding rate 4/5. Regional radio regulations and antenna requirements apply to field deployment.
- Configuration matters: every node in a crew must share one crew ID/key, and each physical node needs a unique address. The checked-in development key must be replaced before operational use.

## What teammates should know about the code

| Area | Main files | Role |
| --- | --- | --- |
| Application and hardware loop | `src/main.cpp` | ESP32 setup, UI, GPS, BLE integration, receive and transmit scheduling |
| Packet layout and validation | `include/mesh_protocol.h`, `src/mesh_protocol.cpp` | Envelope, frame fields, payload serialization and validation |
| Encryption | `include/secure_frame.h`, `src/secure_frame.cpp` | AES-CCM seal/open, nonce, authenticated header and crypto self-tests |
| Flood/ACK/relay state | `include/mesh_state.h`, `src/mesh_state.cpp` | Seen cache, bounded ACK/relay queues, retry state machine |
| Peer and message state | `src/crew_data.cpp` | Peer directory, online expiry, address conflicts, message history |
| Mobile interface | `MOBILE_API.md`, `src/mobile_ble.cpp` | BLE companion API; phone is a client through the handheld, not a LoRa node |
| Protocol reference | `LC3_PROTOCOL.md` | Exact LC3 byte-level wire format |

The central receive-to-relay path is in `src/main.cpp`: `serviceRadioReceive`, `handleAuthenticatedFrame`, `processNewPacket`, `scheduleRelay`, and `serviceRelayTransmit`. `serviceRadioTransmit` establishes priority: target ACK, due ACK/user relay, local user message, presence relay, local presence.

## Suggested presentation outline (8 slides)

1. **Problem and goal:** communicate among a small crew when infrastructure/internet is unavailable.
2. **System overview:** ESP32 handhelds, LoRa for mesh, GPS for location, screen/keypad for operation, BLE for an optional nearby phone.
3. **What “mesh” means here:** no coordinator or route table; each node can relay a new packet up to three hops.
4. **Packet journey:** origin → authenticated encrypted frame → deduplication → delayed relay → destination/ACK.
5. **Relay algorithm:** show the pseudocode above and explain flood ID, seen cache, random backoff, alternate copies, and hop limit.
6. **Reliability:** direct ACK and retry versus crew-message acceptance evidence; make clear what “delivered” does and does not promise.
7. **Security and limitations:** AES-CCM/shared key, 12-node target, 3-hop cap, airtime/queue tradeoffs, no internet and no SOS.
8. **Demo and next steps:** two nodes for discovery/direct messaging; three in a line for relay + return ACK; then crew broadcast and GPS/radar. Refer to the README bench rollout for the full validation sequence.

## Short explanation to say to the teacher

> “Shongket is a small infrastructure-free crew messenger built on ESP32 and LoRa. It does not calculate routes. It forwards each authenticated packet using controlled flooding: every node processes a new flood once, waits a randomized delay, and relays it only if it has not heard enough alternate copies and the packet is still below the three-hop limit. A seen cache suppresses duplicates. Direct messages use a returning ACK and up to three retries; crew messages use a weaker acceptance signal, meaning at least one peer relayed them. AES-CCM protects the packet contents and header with a shared crew key.”

## Questions the team should be ready to answer

- **Why flooding instead of routing?** It avoids route discovery and coordinator dependence for a small, changing crew. It trades airtime efficiency for simpler multi-hop reach.
- **How are loops stopped?** A node relays a flood at most once, identified by origin/session/flood ID; relays stop at hop 3.
- **Why random relay delay?** It lowers the chance that nearby nodes transmit the same packet at the same time and lets alternate copies suppress redundant relays.
- **Why do retries have a new flood ID?** A new attempt must be forwarded as a new flood, while the same logical message ID lets the receiver avoid storing or beeping twice.
- **What does encryption guarantee?** Confidentiality and tamper detection against outsiders lacking the crew key; not privacy from another trusted key-holding device.
- **What is still needed before a real field trial?** Replace development credentials, configure unique node addresses, check local radio rules, then run the staged bench rollout in `README.md`.

## Source files scanned

This brief is based on `README.md`, `LC3_PROTOCOL.md`, `MOBILE_API.md`, `platformio.ini`, the mesh/protocol/security/state/crew-data source and headers, `src/main.cpp`, and `test/mesh_logic_tests.cpp`. It describes the checked-in implementation and documented behavior; it does not claim field performance measurements.
