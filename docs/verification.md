# Verification strategy

## Verification layers

PS-microCard currently uses three complementary verification layers:

1. **host-side unit tests** for individual firmware modules,
2. **host-side pipeline integration tests** that compile production modules together behind simulated hardware/filesystem boundaries,
3. **target firmware build in CI** for the RP2350 platform.

Physical hardware verification is the remaining layer and is intentionally tracked separately.

## Current automated coverage

The repository currently contains **75 host-side tests**:

- **61 unit tests**,
- **14 pipeline integration tests**.

The detailed test matrix and commands are maintained in [`firmware/tests/README.md`](../firmware/tests/README.md).

## Unit-test focus

### PS1 card emulator

Tests cover:

- valid/invalid frame addresses,
- checksum calculation,
- stable frame snapshots,
- multiple writes to the same frame,
- frame-version wraparound,
- rollback of unconfirmed versions,
- confirmation behavior.

### PS1 bus/protocol

Tests cover:

- complete READ, WRITE, and STATUS exchanges,
- first/last frame boundaries,
- bad addresses and checksums,
- ignored/invalid commands,
- `CS` aborts at byte and bit boundaries,
- clock timeout behavior,
- LSB-first transfer behavior and ACK generation in the host reference transport.

### Storage worker

Tests cover:

- frame offsets and exact 128-byte writes,
- batching and the 250 ms idle sync policy,
- `f_open`, `f_lseek`, `f_write`, `f_sync`, and `f_close` failures,
- short writes,
- card removal at different persistence stages,
- retry after storage recovery,
- replay of a partially written batch.

### Image management

Tests cover:

- exact 131,072-byte `.MCR` creation,
- blank-card formatting and checksums,
- format and size validation,
- image listing/filtering,
- automatic `CARDxxx.MCR` naming,
- image deletion behavior.

## Pipeline integration tests

The integration environment compiles the real:

- `ps1_card_bus`,
- `ps1_card_emulator`,
- `micro_sd_image`,
- `micro_sd_worker`

modules together. Hardware, time, and FatFs are simulated at their external boundaries.

Notable scenarios include:

| Scenario | Property under test |
| --- | --- |
| Normal WRITE → sync → READ | End-to-end persistence and protocol correctness |
| Immediate READ before SD worker | RAM update is visible before persistence |
| Restart after successful sync | Confirmed data survives reload |
| Restart before sync | `f_write()` alone is not treated as durable |
| Same frame written twice | Latest version wins |
| Slow SD with interleaved writes | Storage converges to latest RAM state |
| Failure mid-batch | All unconfirmed frames are replayed |
| Sync failure | Written data remains unconfirmed |
| SD removal during write | No durable partial save; PS1 interface disconnects safely |
| SD reinsert | Pending frame is remounted/retried/confirmed |
| Write during SD outage | RAM changes survive and later synchronize |
| Image swap | A is flushed and only complete B becomes visible |
| SD removal during swap | Partial replacement image is never exposed |

## CI

GitHub Actions runs two independent workflows.

### Firmware build

The build workflow:

- checks out submodules,
- installs the ARM embedded toolchain,
- fetches Pico SDK 2.2.0,
- configures the project with CMake/Ninja,
- builds the RP2350 firmware,
- uploads `.uf2`, `.elf`, and `.bin` files as artifacts.

### Host tests

The test workflow:

- installs the pinned Ruby/Ceedling environment,
- runs the complete host suite with `ceedling test:all`.

Both workflows run for pushes and pull requests to `main`.

## What automated tests do not prove

The existing suite gives strong coverage of logic and failure semantics, but it does **not** prove:

- electrical compatibility with a physical PlayStation,
- production PIO timing margins,
- ACK/DATA setup and hold timing,
- behavior across different PS1 console revisions,
- regulator behavior and board power integrity,
- signal integrity on the custom PCB,
- real microSD timing/pathological card behavior,
- long-duration endurance.

These must be validated on target hardware.

## Protocol timing

In progress...


