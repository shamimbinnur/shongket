# Shongket Firmware Design

## Theme direction

Shongket's visual identity is built around **Signal Orange `#F26A2E`**. It should be the first color people associate with the device: clear, energetic, and easy to find at a glance. The handheld interface uses a dark canvas so the orange can identify the current screen, selected item, and next available action without filling every panel. Warm white text and quiet gray surfaces keep messages and device information readable.

The source logo keeps its own colors on the boot screen. Once the interface opens, the same orange should tie the navigation, focus states, and key highlights together. Use it consistently across screens rather than assigning a different accent to each feature.

## Color palette

| Role | Color | Use |
| --- | --- | --- |
| Brand / active | Signal Orange `#F26A2E` | Screen headings, active icons, selected outlines, focus, and primary actions |
| Screen background | Ink `#171717` | Main background on the handheld display |
| Raised surface | Charcoal `#292929` | Header bands, cards, and selected row fills |
| Primary text | Warm White `#F5F3EE` | Message text, labels, and important values on dark surfaces |
| Secondary text | Soft Gray `#8C8C86` | Supporting details, timestamps, and quiet labels |
| Dividers / disabled | Dark Gray | Separators and unavailable controls; keep these visually quieter than active content |

For light-background materials such as documentation or a companion interface, use Warm White `#F5F3EE` as the canvas, Ink `#171717` for primary text, Stone `#77736D` for secondary text, Subtle Surface `#EBE7DF` for cards, and Border `#D8D2C8` for dividers. Signal Orange remains the accent in both versions. The firmware's compact TFT screens use the dark version above.

## How to use the orange

Give orange a specific job on each screen. It can mark the screen title, the selected row or focused control, and a small number of details that guide the next action. An orange outline around a selected card is easier to scan than an entire orange card, especially beside long message text. Use the same treatment for equivalent states throughout the interface.

Keep ordinary body text in Warm White and supporting information in gray. Avoid long paragraphs of orange text or large orange backgrounds: they compete with the selection cue and reduce readability. Pair an orange icon or border with a clear label so the meaning remains visible if color is hard to distinguish.

## Status colors

Status colors report what the device knows. They should keep their meanings even when orange is the brand accent:

| Color | Meaning | Example |
| --- | --- | --- |
| Green | Connected or confirmed success | Radio ready or a delivered message |
| Amber | Warning or pending state | Unread activity or delivery still in progress |
| Red | Error or critical condition | Failed delivery or address conflict |
| Gray | Inactive, unavailable, or unknown | No current GPS fix or an offline peer |
| Orange | Focus, selection, or active navigation | Selected list item or current screen |

Do not use orange to imply that a message was delivered or that a device is connected. State labels should say what happened in words, with color providing a quick secondary cue. This is especially important for pending, unknown, and failed states on the small display.

## Screen hierarchy and writing

The 320 × 240 display should make the current location and the next action obvious. Put the screen title in the header, keep the primary content area focused on one task, and reserve the bottom strip for short keypad hints. Cards and lists should have enough spacing to separate peers, messages, and device readings without heavy decoration.

Use short, direct labels that describe the actual state: “Delivered,” “Pending,” “Failed,” “GPS unavailable,” or “No messages.” Keep technical details such as timestamps and radio readings visually secondary unless the user opened a status screen to inspect them. When a value is unknown, say so rather than presenting a reassuring color or a stale value.
