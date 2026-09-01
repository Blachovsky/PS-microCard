# PS-microCard technical documentation

These documents describe the behavior that is present in this repository. They distinguish the firmware that currently builds for a Raspberry Pi Pico 2 W from the unassembled custom RP2350 PCB, which is not yet a supported firmware target.

## Documents

- [Architecture](architecture.md) — runtime responsibilities, startup, data flow, and module boundaries.
- [PS1 memory-card protocol](ps1-protocol.md) — implemented commands, byte order, PIO transport, status flags, and timeouts.
- [Concurrency](concurrency.md) — core split, per-frame versioning, atomics, polling, and the image-switch pause handshake.
- [User interface](user-interface.md) — current button controls, menu states, timings, limits, and startup behavior.
- [Hardware](hardware.md) — development wiring, custom-PCB architecture, pin-map differences, and hardware-port status.

## Current project status

| Area | Current state |
| --- | --- |
| Firmware target | Builds for `pico2_w` with Pico SDK 2.2.0. |
| Development hardware | The README shows a breadboard prototype using a Pico 2 W, DFRobot DFR0650 SSD1306 OLED, and SPI microSD hardware. |
| PS1 emulation | READ (`0x52`), WRITE (`0x57`), and STATUS (`0x53`) are implemented; production electrical timing has not been measured in this repository. |
| Storage | Raw 128 KiB `.MCR` images, image creation/listing/selection/deletion, and asynchronous per-frame updates are implemented. |
| Automated tests | 61 unit tests and 14 host pipeline tests pass; the production PIO programs, menu loop, and physical hardware are outside host execution. |
| Custom PCB | Schematic and layout exist, but the board is not assembled or electrically validated. Its GPIO map and SH1106G OLED differ from the current Pico 2 W firmware configuration. |

## Important current limitations

- The custom PCB is not a drop-in target for the current binary. It needs a board definition, a different pin configuration/PIO mapping, active-low microSD detection, OLED reset handling, and an SH1106G-compatible display path.
- The selected image is not stored persistently. Startup and top-level storage recovery always load or create `0:/CARD000.MCR`.
- The save worker can requeue unconfirmed frame versions while RAM is preserved, but the production menu recovery path reloads `CARD000.MCR` and resets version tracking. Pending RAM-only writes are therefore not guaranteed to survive a detected SD error, removal, or reinsertion.
