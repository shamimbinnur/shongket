# Shongket — LC3 Encrypted Crew Link

**Beyond the mobile coverage.**

For a short project introduction, start with the [repository README](../README.md) or [device overview](../docs/device-firmware.md).

The supplied Shongket logo is stored at [assets/shongket-logo.png](assets/shongket-logo.png).
The firmware displays a scaled RGB565 version with the source image's colors on its boot screen. Screen colors
follow [design.md](design.md); status colors retain their connected, warning, and
error meanings. To regenerate the display bitmap after changing the source logo,
export it as a 32-bit BMP with `sips -s format bmp assets/shongket-logo.png --out /tmp/shongket-logo.bmp`,
then run `python3 tools/generate_logo.py /tmp/shongket-logo.bmp include/logo_bitmap.h`.

Shongket is firmware for up to 12 ESP32 handhelds on one private LoRa crew channel.
It provides encrypted automatic discovery, direct and whole-crew messages,
three-hop controlled flooding, a 20-message RAM inbox/outbox, shared GPS
locations, and a north-up radar. There is no coordinator or internet service.
SOS is intentionally not included yet.

One securely bonded phone can also use the BLE GATT API to mirror the crew
directory, messages, GPS, and status or originate a message through this node.
The complete client contract is in [MOBILE_API.md](MOBILE_API.md).

LC3 is incompatible with the earlier readable LC2 protocol. Reflash every
device in a crew before testing LC3.

## Configuration before field use

Two local headers are deliberately ignored by Git:

- `include/crew_config.h`: the same random 32-bit `CREW_ID` and random 16-byte
  AES key on every device in one crew.
- `include/device_config.h`: a unique address from 1 through 65534 and a
  printable name of at most 12 characters for one physical device.

Copy the tracked examples, then edit the copies:

```sh
cp include/crew_config.example.h include/crew_config.h
cp include/device_config.example.h include/device_config.h
```

Generate a field key with a cryptographically secure tool, for example
`openssl rand -hex 16`, and translate the 32 hexadecimal characters to the 16
byte values in `CREW_KEY`. Do not commit or share the real local headers. The
development key currently in this workspace is transcript-visible and must be
replaced before operational use.

The build rejects zero/reserved addresses, zero crew IDs, a zero key, empty or
oversized names, and the `CHANGE_ME` name. Devices detect two hardware
fingerprints advertising the same manual address; a conflict is shown in red
and disables sending from a locally conflicted address.

## Hardware

| Peripheral | Signal | ESP32 GPIO |
| --- | --- | ---: |
| Shared SPI | SCK / MISO / MOSI | 18 / 19 / 23 |
| ST7789 | CS / DC / RST | 15 / 2 / 4 |
| SX1278 | NSS / RST / DIO0 | 5 / 16 / 22 |
| GPS | GPS TX to RX / GPS RX to TX | 34 / 17 |
| Keypad rows | R1 / R2 / R3 / R4 | 32 / 33 / 25 / 26 |
| Keypad columns | C1 / C2 / C3 / C4 | 27 / 14 / 12 / 13 |
| Active buzzer | Signal | 21 |

The display uses a 320x240 landscape layout at rotation 1. The radio uses 433 MHz, sync word
`0x4A`, SF9, 125 kHz bandwidth, coding rate 4/5, and payload CRC. UART2 GPS is
9600 baud. Serial diagnostics are 115200 baud.

GPIO12 is an ESP32 boot strap pin. Do not hold a keypad key while powering or
resetting a unit. Always connect a suitable 433 MHz antenna before transmitting
and follow the radio rules for the country where the units will operate.

## Shared-SPI protection

The TFT and SX1278 share VSPI. The firmware keeps both CS pins OUTPUT/HIGH
before `SPI.begin()`, initializes the TFT before LoRa, deselects LoRa before
every display operation, deselects the TFT before every radio operation, and
calls `LoRa.receive()` after every transmit attempt. A whole screen is drawn
only on navigation; later changes redraw only its body region.

## Feature-phone controls

The 320x240 interface uses a black/orange card-and-list theme with lightweight
procedural icons. Its status strip shows uptime, truthful LoRa activity, GPS,
Bluetooth, and unread state; no battery percentage or wall clock is invented.

Home uses numbered shortcuts:

- `1` Messages
- `2` Radar
- `3` Crew
- `4` Bluetooth
- `5` Device Status
- `6` New message

Outside composition, `A`/`B` move, `C` goes back, `D` selects, `*` returns
Home, and `#` opens contextual options. Lists remember their selection.
Recipient selection starts with `ALL CREW`; offline or conflicted peers remain
visible but cannot be selected for direct messaging.

Messages is a hub for Inbox, Sent, New Direct Message, Broadcast, and the one
RAM draft. A received message produces one beep and an unread update; Home also
shows a three-second sender/preview banner without changing screens.

Compose follows classic button-phone behavior:

- `2`-`9`: classic multi-tap letter groups with an 800 ms window
- `0`: space; `1`: common punctuation
- `A`: cycle `Abc`, `abc`, `ABC`, `123`
- `*`: symbol palette; move with `2/8/4/6`, insert with `5`, close with `D`
- `C`: delete; `#`: send; `D`: save the draft in RAM and leave
- `B`: options to change the draft recipient or clear the draft

`Abc` capitalizes the first letter and the first letter after sentence
punctuation plus spaces. `123` inserts digits immediately. Text is printable
ASCII and limited to 80 characters.

## Discovery, messages, and radar

Each device announces encrypted presence every 30 seconds plus or minus three
seconds after a randomized initial delay. Any authenticated origin traffic
refreshes a known peer. A peer becomes offline after 120 seconds; when the
11-peer table is full, only the oldest offline entry can be evicted. A beacon
without a GPS fix preserves the last valid coordinates but marks them as last
known.

Direct messages appear only on the destination device and are delivered only
after its ACK returns through the mesh. A retry uses a new flood ID but retains
the message ID, so a recipient ACKs the new attempt without creating another
history record or beep. Whole-crew messages appear on every member; `Delivered`
means at least one other node accepted and relayed the announcement, not that
all members received it. Both modes make at most three attempts with a six
second end-to-end evidence window.

The radar screen remains visible while the local GPS has no current fix and
shows a small "GPS not fixed" label. With a current fix, it plots online peers with current
or last-known coordinates, north at the top, on an automatically selected
100 m, 250 m, 500 m, 1 km, 2 km, 5 km, or 10 km scale. More distant markers
clamp to the rim. Selection shows distance, true-north bearing, coordinate age,
hops, and last-hop RSSI. It cannot rotate with the user because this hardware
has no compass.

## LC3 security and routing

The exact byte layout for a future mobile bridge is documented in
[LC3_PROTOCOL.md](LC3_PROTOCOL.md).

The clear 20-byte radio header is:

| Field | Bytes |
| --- | ---: |
| Magic `L3` | 2 |
| Crew ID | 4 |
| Last transmitter address | 2 |
| Last transmitter boot session | 7 |
| Last transmitter frame sequence | 4 |
| Encrypted plaintext length | 1 |

The encrypted logical envelope contains packet type, original address and
seven-byte boot session, destination, flood ID, and hop count. Payload types
are Presence, DirectMessage, CrewMessage, and DeliveryAck. AES-128-CCM uses an
eight-byte authentication tag; the complete clear header is authenticated as
additional data. Its unique 13-byte nonce is transmitter address (2), random
boot session (7), and transmitter frame sequence (4).

Different crew IDs are discarded before decryption. Authentication and strict
length/type/address/hop validation happen before any packet is processed or
relayed. Relays decrypt, increment the hop, then create a fresh authenticated
frame with their own nonce. Flood deduplication uses packet type, original
address/session, and flood ID. Each node relays a unique flood at most once,
never beyond hop three, and cancels a pending relay after two alternate copies.

Traffic priority is target ACK, due ACK/user relay, local user message, presence
relay, then local presence. All queues and caches are fixed-size. Saturation is
counted on Status rather than overwriting a live job or allocating heap memory.

The shared-key model prevents outsiders without the key from reading or
forging packets. It does not prevent a deliberately modified trusted crew
device from decrypting direct radio traffic. Keys are not persisted or rotated
by this firmware.

## Build and test

Platform and Arduino dependency versions are pinned in `platformio.ini`.

```sh
platformio run
platformio run --target upload
platformio device monitor
```

Run the hardware-independent deterministic suite on a development machine:

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
  src/mesh_protocol.cpp src/mesh_state.cpp src/crew_data.cpp \
  src/feature_input.cpp src/radar_math.cpp src/mobile_framing.cpp \
  src/ui_model.cpp \
  test/mesh_logic_tests.cpp \
  -o /tmp/lora_lc3_tests
/tmp/lora_lc3_tests
```

The suite covers serialization, malformed frames, nonce construction,
recipient filtering, cache/queue/hop/retry behavior, wraparound, directory and
history behavior, mobile fragmentation, UI navigation/filtering, input modes,
and radar math. Every ESP32 boot additionally
runs AES-CCM RFC known-answer, round-trip, wrong-key, tamper, authenticated
header, wrong-crew, and malformed-length tests against its built-in Mbed TLS.
LoRa starts only when configuration and cryptographic tests pass.

## Bench rollout

1. Replace the development crew ID/key and assign a different address/name for
   every physical unit. Save each resulting firmware binary or rebuild per unit.
2. Repeatedly power-cycle one unit and confirm TFT, CCM, and LoRa show OK.
3. Confirm wrong-key and wrong-crew units remain absent.
4. With two units, verify discovery, direct target ACKs, crew delivery evidence,
   offline expiry, draft behavior, and 20-record rollover.
5. With three units in a line, verify a direct message and its ACK traverse the
   middle unit while the destination stores the message only once.
6. Test overlapping/ring coverage, simultaneous messages, queue saturation,
   hop-three limits, and cache expiry.
7. Outdoors, validate real coordinates, cardinal radar placement, scale changes,
   stale fixes, and edge clamping.
8. Run as many of the intended 12 devices as available for several hours and
   confirm presence traffic does not blank the TFT or starve user traffic.

Do not add SOS until encrypted discovery, direct delivery, radar, GPS, and
multi-hour shared-SPI operation pass this rollout.
