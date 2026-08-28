# Verification strategy and current evidence

## Evidence layers

The repository currently provides:

1. host unit tests for four logic/storage modules;
2. host pipeline tests that combine four production modules with a simulated environment;
3. a target firmware build for `pico2_w` in CI.

It does not yet provide physical PlayStation, PIO waveform, or custom-PCB validation evidence.

## Automated test count

| Group | Files | Tests |
| --- | ---: | ---: |
| Unit | 4 | 61 |
| Pipeline integration | 14 | 14 |
| Total | 18 | 75 |

The executable test matrix and commands are maintained in [`firmware/tests/README.md`](../firmware/tests/README.md).

## What the unit tests execute

| Production module | Main coverage |
| --- | --- |
| `ps1_card_emulator.c` | frame bounds, checksums, stable snapshots, versions, wraparound, rollback, confirmation |
| `ps1_card_bus.c` under `UNIT_TEST` | READ/WRITE/STATUS parsing, results, aborts, reference bit-bang transfer |
| `micro_sd_worker.c` | offsets, batches, idle sync, FatFs failures, rollback and direct worker reconnect |
| `micro_sd_image.c` | creation, size/format checks, catalog limits, naming, deletion |

Unit tests replace Pico SDK, GPIO, FatFs, time, display, and SD-library interfaces.

## What the pipeline tests execute

Each pipeline executable compiles the real:

- `ps1_card_bus.c` under `UNIT_TEST`,
- `ps1_card_emulator.c`,
- `micro_sd_image.c`,
- `micro_sd_worker.c`.

The integration support supplies:

- a scripted byte transport instead of PIO,
- a RAM filesystem model with separate written and durable buffers,
- simulated time and storage delays,
- fake card-presence/mount/path/result functions,
- automatic pause acknowledgement instead of a second core,
- display/logging stubs.

It does not compile `main.c`, `menu/*.c`, `micro_sd.c`, `hardware_config.c`, `oled.c`, or the PIO assembly path. It is therefore a module pipeline, not a complete firmware simulation.

## Pipeline scenarios and interpretation

| Scenario | What it establishes | Important boundary |
| --- | --- | --- |
| normal WRITE → sync → READ | parser, RAM, worker offset, modeled durability, and readback agree | scripted transport and RAM filesystem |
| immediate READ before worker | RAM is authoritative during the session | no real bus timing |
| restart after modeled sync | durable model reloads confirmed bytes | simulated restart |
| restart before sync | `f_write` buffer is not treated as durable by the model | model controls durability semantics |
| same frame twice / slow storage | latest RAM version converges in the worker model | no real multicore scheduling |
| partial batch/write/sync failure | rollback tables permit replay while RAM is retained | direct harness reconnect, not menu recovery |
| removal during write | partial fake write is not confirmed and logical card is disabled | fake card/removal boundary |
| write while physically absent | writes in the modeled pre-detection window remain in RAM | harness leaves logical presence true |
| image swap/removal during swap | UNIT_TEST pause gating prevents a partial RAM image from being read | automatic pause acknowledgement |
| reinsertion | direct worker remount/reinit can persist retained RAM | production menu reload path is not executed |

The tests do not prove that pending RAM-only writes survive the actual `menu_task_run()` recovery flow. That flow reloads `CARD000.MCR` and resets version state.

## Firmware build

The firmware build compiles all application C sources and generates the PIO header for a Pico 2 W target. It establishes source/toolchain integration, including the production PIO assembly, but does not execute the binary.

The current CMake configuration records:

- Pico SDK 2.2.0,
- board `pico2_w`,
- C11 for project C code,
- C++17 because the linked SD/FatFs library includes C++,
- UART stdio enabled and USB stdio disabled,
- output target name `main`.

A successful build produces `main.uf2`, `main.elf`, `main.bin`, and other toolchain outputs in the selected build directory.

## CI

### Firmware build workflow

On pushes and pull requests to `main`, and on manual dispatch, GitHub Actions:

- checks out submodules,
- installs CMake, Ninja, and Ubuntu's ARM embedded packages,
- clones Pico SDK 2.2.0,
- configures `-S firmware -B build/firmware -G Ninja`,
- builds and uploads UF2, ELF, and BIN artifacts.

The local VS Code configuration names ARM toolchain `14_2_Rel1`; the Linux CI workflow uses the version supplied by the Ubuntu package and should not be described as the same pinned toolchain.

### Host-test workflow

The second workflow uses Ruby 4.0 with the locked bundle and runs:

```console
bundle exec ceedling test:all
```

## Status matrix

| Property | Current evidence | Status |
| --- | --- | --- |
| C build and link for Pico 2 W | local/CI build configuration | covered by build |
| READ/WRITE/STATUS logic | host unit and pipeline tests | covered in simulation |
| frame version and modeled persistence logic | host unit and pipeline tests | covered in simulation |
| menu state machine and storage orchestration | no automated test | not covered |
| production PIO execution | only assembled/linked | not executed |
| ACK/DATA waveform and timing margin | no committed capture | pending |
| real PlayStation compatibility | no recorded console matrix | pending |
| real microSD removal/power loss | simulated only | pending |
| DFR0650 display behavior | breadboard photo but no repeatable test record | informal only |
| custom PCB firmware build | no custom board target | pending |
| custom PCB electrical bring-up | board unassembled | pending |
| endurance and long-duration behavior | no record | pending |

## Required physical validation

A future hardware report should record, at minimum:

- board/console revision and power source,
- firmware commit and build configuration,
- CMD/SCK/CS/DATA/ACK captures with measured setup, hold, ACK delay, and pulse width,
- READ, WRITE, STATUS, abort, startup, and image-swap traces,
- behavior under simultaneous SD writes, OLED refreshes, and UART logging,
- multiple microSD cards and removal at controlled points,
- power interruption during write/sync/close,
- regulator rails, current, temperature, and source switching,
- pass/fail criteria and raw artifacts.

Until those artifacts exist, the production transport and custom hardware should be described as unvalidated.
