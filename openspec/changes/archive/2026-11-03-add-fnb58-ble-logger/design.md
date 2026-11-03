# Design

## Context

The tester is a BLE peripheral with a UART-like GATT service:
- writes go to characteristic `ffe9` in service `ffe5`;
- notifications come from `ffe4` in service `ffe0`.

It streams nothing until it has received two start commands. [docs/protocol.md](../../../../docs/protocol.md)
describes how those commands and the frame format were found. BlueZ owns the adapter and is
reached over the system D-Bus.

## Goals / Non-Goals

**Goals:**
- Unattended logging for weeks: survive the tester being switched off, out of range or silent.
- Bounded disk use.
- Build without network access.

**Non-Goals:**
- Real-time consumers of the readings.
- Decoding every frame type.

## Decisions

- **C on bluez_inc, vendored unmodified** (`third_party/bluez_inc`, `CMakeLists.txt`). Upstream
  already provides asynchronous writes, notifications and discovery on one GLib main loop. A fork
  with extra "async" helpers was reviewed and not used: its write function passes a UUID where
  BlueZ expects a D-Bus object path.
- **One state machine** (`src/fnb58d.c`): idle → scanning → connecting → connected → streaming,
  plus disconnecting. A 1 s tick flushes files and runs the watchdogs. On any failure the device is
  removed from BlueZ (`RemoveDevice`, which also cancels a pending connect) and a fresh scan starts
  after a backoff of 1 s to 30 s.
- **Frame parser in plain C without GLib** (`src/fnb58_proto.c`), so `tests/test_proto.c` can replay
  captured notifications cut at every chunk size from 1 to 100 bytes.
- **Validation by structure and range.** The check byte matches no CRC-8 (any polynomial or initial
  value), sum or XOR. A frame is accepted when its type is known (0x04–0x08), its length is at most
  64 bytes, and its values are plausible (0–30 V, ±10 A).
- **CSV for data, journal for events.** About 3.6 readings/s is roughly 300 000 lines/day, which
  would flood the journal. Daily files are easy to analyse, and deleting old ones bounds disk use.
- **Retention inside the daemon** (`src/fnb58d.c`, at each day change) rather than a separate timer.
- **`DynamicUser=yes` with `StateDirectory=fnb58d`** (`systemd/fnb58d.service`): no account to
  create, and `/var/lib/fnb58d` is the only writable path.

## Risks / Trade-offs

- [No check-byte validation] → a corrupted frame with plausible values is logged. Mitigation:
  range checks.
- [No keep-alive] → if the tester stops streaming after some time, the 10 s watchdog reconnects,
  leaving a gap of 12 s or more.
- [Disk] → about 15 MB/day, about 210 MB with the default 14 days.
- [D-Bus policy] → distributions whose BlueZ policy allows only root or a `bluetooth` group need
  `SupplementaryGroups=bluetooth` or a fixed user.
