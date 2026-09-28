# How Shongket works

Shongket helps a small crew exchange information when mobile coverage is missing. Each person carries a handheld radio. A phone can provide a more familiar screen, but it talks to a nearby handheld over Bluetooth; the handheld remains responsible for radio communication.

## One message's journey

1. A crew member writes a direct message on the handheld or paired phone.
2. The handheld protects the message with the crew's shared encryption key and transmits it over LoRa.
3. Nearby crew handhelds check the message. A handheld that has not seen it before may pass it on after a short random wait.
4. A message can move through at most three relay hops. Repeated copies are ignored, so it does not circulate indefinitely.
5. The addressed handheld stores a direct message and sends an acknowledgement back. The sender shows delivery only after that acknowledgement returns.

Whole-crew messages are different: they are offered to everyone, but a delivery indication means at least one other handheld accepted and relayed the message. It does **not** confirm that every crew member received it.

```text
Sender → nearby handheld → relay handheld → recipient
                           ← acknowledgement ←
```

## Why the phone is optional

The device has its own screen and keypad, so the crew can send and read messages without a phone. With a bonded phone, the app can show messages, crew presence, GPS, and radio status and ask the handheld to send text. Bluetooth reaches only the paired handheld; LoRa connects the handhelds.

## Current limits

- Designed for one crew of up to 12 handhelds and at most three relay hops.
- Messages are held in a small RAM history rather than a cloud archive.
- GPS and radar depend on a current position fix and suitable hardware reception.
- All trusted handhelds share one crew key. This protects against outsiders without that key, but does not hide direct messages from another trusted key-holding device.
- SOS, automatic key rotation, and measured field-range claims are outside the current version.

For implementation details, see the [firmware project brief](../device-firmware/PROJECT_BRIEF.md), [radio protocol](../device-firmware/LC3_PROTOCOL.md), and [mobile Bluetooth API](../device-firmware/MOBILE_API.md).
