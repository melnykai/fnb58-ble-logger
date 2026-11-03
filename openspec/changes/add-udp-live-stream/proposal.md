# Proposal

> **Spec-first.** This change is written before any code. It is the proposal stage of the OpenSpec
> loop: review it, adjust the requirements, then implement against them.

## Why

The CSV files are written for later analysis. Some uses need the readings while they are measured:
a live plot on another machine, a script that reacts to current spikes, or a second logger. Today the
only way is to tail the CSV file on the same machine.

## What Changes

- New option `--udp HOST:PORT`. When set, the daemon also sends each accepted measurement as one UDP
  datagram containing one line of JSON.
- The stream is off by default. Without `--udp`, behaviour is unchanged.
- The CSV file stays the reference. Sending never delays or stops CSV logging; send errors are counted
  and reported to the journal at most once every 60 s.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `fnb58-ble-logger`: adds the live UDP stream. Existing requirements are unchanged.

## Impact

- `src/fnb58d.c`: option parsing, socket set-up at start, one send per accepted measurement.
- New `src/udp_out.{h,c}`: JSON formatting and sending, plain C without GLib so it can be unit-tested.
- `tests/test_udp_out.c`: formatting test.
- `systemd/fnb58d.service`: no change; `FNB58D_OPTS` carries `--udp`.
- `README.md`: option table and a receiving example.

## Out of scope

- TCP, MQTT, WebSocket or any connection-oriented transport.
- Authentication or encryption. The stream is meant for localhost or a trusted LAN.
- Sending raw frames or undecoded frame types.
- Buffering or re-sending datagrams that were lost.
