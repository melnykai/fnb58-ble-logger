# How this was built

The project started as one question: can a program read an FNB58 over Bluetooth? Two days later it
was a tested daemon with a specification. The order of the work is worth showing, because the first
half ran opposite to spec-driven development, and the repo shows both ways.

## Two routes

```
Spec-first (what OpenSpec intends)
  proposal → spec delta → design → tasks → code → tests → archive

What happened here (first change)
  question → probe the device → reverse-engineer → Python prototype → decide storage
           → C daemon → tests on hardware → spec, post-factum → archive
```

A spec-first proposal needs to know what the system can do. Here the main unknown was the device:
its commands and frame format were undocumented. A SHALL statement about a frame no one has seen
would be a guess. So the first change, `add-fnb58-ble-logger`, was written from tested behaviour and
archived as the baseline.

Once that baseline exists, the reason for working backwards is gone. The next change,
`add-udp-live-stream`, is spec-first: proposal, design, requirements and tasks, with no code yet.

## Steps

**1. Shape the task by asking.** Each answer fixed one decision: Bluetooth LE rather than USB,
Python to explore, C for the daemon, CSV for data, the journal for events, no live stream at first.

**2. Probe the device.** Scan, read the GATT table, subscribe, try commands. Most probes failed.
[protocol.md](protocol.md) lists them, because the failures explain the parser's design: the check
byte could not be identified, so frames are checked by structure and range instead.

**3. Read third-party code before running it.** Two projects were reviewed:
- A community web monitor for this tester had the two missing start commands. It also ran a debug
  web server on all network interfaces, and exposed an API that tells the tester to request up to
  20 V from the charger. Only the two commands were taken.
- A fork of the BLE library added "async" helpers. Its write function passed a UUID where BlueZ
  expects an object path, so it could never work. The original library already did async writes
  correctly and was used unmodified.

**4. Prototype in Python** (`tools/fnb58_ble.py`, bleak). This confirmed the stream end to end
(70 readings in 20 s) before writing any C.

**5. Write the daemon in C.** One GLib main loop, a six-state connection machine, a parser with no
GLib so it can be tested without a radio, daily CSV files, and a journal that carries events only.

**6. Test from bytes to hardware.**

| Level | What | Result |
|---|---|---|
| Unit | captured notifications cut at every size from 1 to 100 bytes, garbage, impossible values | pass |
| Build | `-Wall -Wextra` | 0 warnings, after one fix |
| Hardware, 40 s | find, connect, stream, stop | 144 readings, clean stop |
| Hardware, 3 min | tester off for 58 s | disconnect after 6 s, streaming 2 s after it came back |
| Service | systemd, tester unplugged and replugged | journal priorities correct, reconnect automatic |
| Spec check | does a same-day restart write a second header? | no: append mode starts at end of file |

The last row is the useful side effect of writing a spec after the code: every SHALL had to be
checked, and that one had not been tested.

**7. Specify, validate, archive.** `openspec validate --strict`, then `openspec archive`, which
generated `openspec/specs/fnb58-ble-logger/spec.md`.

## Skills shown

- Bluetooth LE reverse-engineering with a real device: GATT discovery, notification capture,
  checksum brute force, and keeping a record of what failed.
- Security reading of third-party code before use.
- C on Linux: GLib main loop, D-Bus through BlueZ, a state machine with watchdogs and backoff,
  systemd service design.
- Testing at every level, from a pure-function unit test to hardware fault injection.
- OpenSpec: a post-factum baseline, archived with the CLI, then a spec-first change, both validated
  in CI.

## Lessons

- Probe before you specify. Specifying unknown hardware produces fiction.
- Once behaviour is known and specified, go spec-first. The baseline makes the next change cheap.
- Write down what failed. The record of failed probes is what justified the "structure and range"
  validation.
- Writing a spec is a test plan. Every claim either points to a test or reveals a missing one.
