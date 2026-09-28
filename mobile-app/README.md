# Shongket mobile app

**Beyond the mobile coverage**

Phone companion for a Shongket LC3 handheld. The app mirrors crew, messages, GPS, and radio status over the **LC3 Mobile BLE API v1** and queues outbound text through the handheld’s encrypted LoRa mesh. The ESP32 stays the source of truth. BLE never exposes the crew key or raw LC3 frames.

For a short explanation of the complete project, start with the [repository README](../README.md) or [mobile app overview](../docs/mobile-app.md).

This is not an Expo Go app. Bluetooth pairing needs a **development build** on a physical iPhone or Android phone.

## Requirements

- Node 22+
- Expo SDK 57 / Xcode 26.4+ (iOS) / Android SDK 36
- A physical phone (simulators have no usable BLE)
- An LC3 handheld advertising as `CL3-<address>-<name>`

## Development build

```bash
npm install
npx expo run:ios --device
# or
npx expo run:android --device
```

Then start Metro with `npm start` if the run command did not. Rebuild native code after changing `app.json` or adding native modules (`npx expo prebuild --clean`, then `run:ios` / `run:android` again).

## Pairing

1. On the handheld: `5 Status` → `A Phone API` → `A Pair`.
2. Shongket scans for 60 seconds for `CL3-…`.
3. Connect and enter the six-digit passkey shown on the TFT (never Serial).
4. Only one bonded phone is accepted. To replace this phone, press `C` twice within five seconds on the Phone API screen, then pair again.

The OS stores the BLE bond. Shongket stores only the last device id in SecureStore. Message bodies stay in RAM and are not copied to cloud logs.

## Simulator / no radio

The Connect screen has **Use mock handheld** so Messages, Crew, Radio, and Compose can be exercised without BLE.

## Tests

```bash
npm test
```

Protocol tests cover fragment framing, request IDs, pagination, `BUSY` send, and event sequence gaps.

## Intentionally unavailable

Crew-key writes, local config, raw LoRa, phone GPS injection, firmware update, SOS, accounts, and cloud archive are out of API v1.
