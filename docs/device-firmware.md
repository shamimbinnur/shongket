# Device firmware

Each Shongket handheld is an ESP32-based device with a LoRa radio, screen, keypad, GPS receiver, and buzzer. It can work independently of the mobile app.

## What the handheld does

- Discovers other crew devices through regular presence updates.
- Sends direct messages or announcements to the whole crew.
- Passes on new, valid packets to extend reach across a small changing group.
- Shows messages, crew presence, GPS positions, a north-up radar, and device status.
- Offers a Bluetooth connection to one bonded phone companion.

The display uses a dark background and Signal Orange for focus and selected actions. Green, amber, red, and gray indicate distinct status meanings.

The [firmware README](../device-firmware/README.md) covers hardware wiring, configuration, build steps, and bench tests. The [project brief](../device-firmware/PROJECT_BRIEF.md) explains the relay design in greater detail.
