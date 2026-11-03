# Proposal

> **Post-factum.** This change was written after the code, from behaviour that had already been
> tested. The protocol was unknown when work started, so there was nothing to specify yet.
> [docs/development-story.md](../../../../docs/development-story.md) explains why. Later changes in
> this repo are spec-first.

## Why

The FNIRSI FNB58 measures the voltage and current of a USB line and streams its readings over
Bluetooth LE, but only to its phone app. Long unattended measurements need a logger on a Linux
machine that keeps running when the tester is switched off, moved out of range or replugged.

## What Changes

- New daemon `fnb58d`:
  - finds the tester over Bluetooth LE, starts its data stream and decodes voltage, current and power;
  - appends the readings to one CSV file per day and deletes files older than a set number of days.
- It drops and re-establishes the link by itself when the tester disappears, goes silent or does not
  start streaming.
- Events go to stderr, and so to the journal under systemd. Measurements never do.
- A systemd unit runs it under a throwaway system user and restarts it after any exit.

## Capabilities

### New Capabilities
- `fnb58-ble-logger`: discovery, stream start, decoding, daily CSV files and retention, link recovery,
  journal events, service and shutdown.

### Modified Capabilities
<!-- none -->

## Impact

- New code: `src/fnb58d.c`, `src/fnb58_proto.{h,c}`, `tests/test_proto.c`, `systemd/fnb58d.service`,
  `CMakeLists.txt`.
- Vendored: `third_party/bluez_inc` (weliem/bluez_inc 157c604, MIT), unmodified.
- Prototype kept for reference: `tools/fnb58_ble.py` (Python, bleak).

## Out of scope

- Decoding frame types 0x05 to 0x08. They are kept raw with `--raw` only.
- Validating the frame check byte, whose algorithm is unknown.
- Live streaming of readings to other programs. That is the spec-first change `add-udp-live-stream`.
- The tester's USB HID interface.
