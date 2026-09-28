# Mobile app

The Shongket phone app is a companion to one nearby handheld. It gives a crew member a comfortable way to read and compose messages, see who is online, and inspect location and radio status. The phone does not transmit LoRa packets itself.

## Main screens

- **Messages:** Read direct and whole-crew messages, see unread items, and compose text.
- **Crew:** See known members, online state, and reported location availability.
- **Radio:** Check the connected handheld's health, GPS, and diagnostics.
- **Connect:** Bond a phone to a handheld and manage the connection.

The handheld remains the source of truth. The app mirrors information over a bonded Bluetooth connection and asks the handheld to send messages. A mock handheld mode lets someone explore the interface without radio hardware; it does not prove radio delivery.

For installation, pairing, and tests, use the [mobile app README](../mobile-app/README.md). For the exact Bluetooth contract, use the [mobile API reference](../device-firmware/MOBILE_API.md).
