# PS-microCard technical documentation

This directory contains the engineering notes for PS-microCard. The goal is to document the design decisions that are difficult to infer from source code alone, while keeping the repository documentation readable directly on GitHub.

## Documents

- [Architecture](architecture.md) — system responsibilities, data flow, and module boundaries.
- [PS1 memory-card protocol](ps1-protocol.md) — implemented commands, frame geometry, PIO transport, and transaction behavior.
- [Persistence and recovery](persistence.md) — RAM-first writes, durable storage semantics, failure recovery, and image switching.
- [Concurrency](concurrency.md) — RP2350 core split, frame versioning, synchronization, and bus pausing.
- [Hardware](hardware.md) — board-level architecture, power tree, interfaces, and validation status.
- [Verification](verification.md) — automated test strategy, CI, current evidence, and hardware validation still to be completed.

## Current project status

The firmware builds for the RP2350-based Raspberry Pi Pico 2 W development target and the host-side test suite exercises the protocol, card-image state, persistence worker, and failure-recovery paths. The custom KiCad PCB has been designed but has not yet been assembled.

## Design goals

PS-microCard is designed around four main requirements:

1. service the PS1 memory-card bus without filesystem or UI latency entering the time-critical path,
2. expose writes to the console immediately from RAM while persisting them asynchronously,
3. never treat an `f_write()` alone as proof that save data is durable,
4. switch virtual card images without exposing a partially replaced RAM image to the console.
