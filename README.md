# Shongket

**Beyond the mobile coverage**

Shongket is a small crew communication system for places where mobile service or internet access is unavailable. ESP32 handhelds exchange encrypted text and location updates over LoRa radio. A nearby phone can connect to its handheld over Bluetooth to read messages, view the crew, and compose a message. The handheld sends the radio traffic; the phone is not a LoRa node.

```text
Phone app ⇄ Bluetooth ⇄ Handheld A ⇄ LoRa ⇄ Handheld B ⇄ LoRa ⇄ Handheld C
```

The handhelds can pass a new message onward for up to three hops. This helps a small crew communicate even when two members cannot hear each other directly. No server, internet connection, or central coordinator is needed.

## Start here

| If you want to… | Read |
| --- | --- |
| Review the complete project | [Project report](submission/Shongket-Project-Report.pdf) |
| Present the project | [Presentation slides](submission/Shongket-Presentation.pptx) |
| Understand the idea in a few minutes | [How Shongket works](docs/how-it-works.md) |
| Explore the phone experience | [Mobile app overview](docs/mobile-app.md) |
| Explore the handheld | [Device firmware overview](docs/device-firmware.md) |
| Run the software | [Mobile setup](mobile-app/README.md) and [firmware setup](device-firmware/README.md) |

## What it currently provides

- Direct and whole-crew text messages, peer discovery, and a small message history on the handheld.
- GPS sharing and a north-up crew radar when the hardware has a usable position fix.
- An optional phone companion for messages, crew, GPS, and radio status through bonded Bluetooth.
- Encrypted radio packets, duplicate suppression, and bounded multi-hop forwarding.

Shongket is a prototype. The code and automated tests document its intended behavior; measured field range and a completed hardware trial are **not claimed** in this repository. SOS, accounts, cloud backup, and key rotation are not part of the current version. See [limitations](docs/how-it-works.md#current-limits).

## Repository map

- [`device-firmware/`](device-firmware/) — ESP32 handheld code and detailed radio/Bluetooth references.
- [`mobile-app/`](mobile-app/) — Expo phone companion and app setup.
- [`docs/`](docs/) — short, nontechnical explanations.
- [`submission/`](submission/) — professor-facing report and slides.

The report and slides use the Shongket identity: Signal Orange `#F26A2E`, Warm White `#F5F3EE`, and Ink `#171717`.
