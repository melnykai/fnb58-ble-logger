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
`add-udp-live-stream`, was spec-first: proposal, design, requirements and tasks were committed and
validated before any code.

## The spec-first change, as it went

- **One commit per task**, in task order, each naming its task number. A box was ticked only when
  that task's `verify:` check had passed.
- **Unit-testable parts first.** Tasks 1.1 and 1.2 built `src/udp_out.c` without GLib, so the
  datagram bytes, address parsing and non-blocking sends were tested without a tester.
- **The design was corrected, not ignored.** Writing the send test showed that on Linux an
  unconnected UDP socket never reports a refused port, so the "errors are counted" requirement could
  not be met as designed. The socket is now connected, and `design.md` was updated in the same commit
  as the code.
- **Hardware tasks waited for hardware.** Tasks 2.2 and 2.3 were committed with their boxes
  unticked. The first test session failed before any data arrived. The unchanged 0.1.0 binary failed
  the same way, which put the fault on the tester's side; a power cycle fixed it. Then both checks
  passed: 334 of 334 datagrams matched their CSV lines, and 5.4 min against a refused port left the
  CSV at 4.00 readings/s with exactly 5 warning lines.
- **Archive last.** `openspec archive` merged the 4 new requirements into the main spec (now 14).

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

A later unattended run under systemd logged 5 h 27 min (78,512 readings) without a gap; the
plots in the README come from it.

**7. Specify, validate, archive.** `openspec validate --strict`, then `openspec archive`, which
generated `openspec/specs/fnb58-ble-logger/spec.md`.

## Working with an AI coding agent

The project was built with [Claude Code](https://claude.com/claude-code), Anthropic's coding agent,
working in the terminal next to the hardware. The agent wrote most of the code, tests and docs, and
ran the probes and builds. The author set the scope and made each decision (Bluetooth over USB, C
for the daemon, CSV for data, what third-party code to trust), handled the tester for every
hardware test, and reviewed the results. OpenSpec fits this way of working: the spec and
`config.yaml` give the agent the same rules a human contributor gets, and the `/opsx:*` commands in
`.claude/` let it follow the propose, apply and archive loop.

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
