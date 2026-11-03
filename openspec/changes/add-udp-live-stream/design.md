# Design

## Context

Measurements are produced at about 3.6 per second, one at a time, on the GLib main loop in
`src/fnb58d.c`. Anything added on that path must not block, or notifications pile up and the
watchdog may drop a healthy link.

## Goals / Non-Goals

**Goals:**
- One datagram per measurement, readable with standard tools (`nc -ul`, `socat`, a few lines of Python).
- Zero effect on CSV logging, whatever the network does.

**Non-Goals:**
- Delivery guarantees. UDP loss is acceptable; the CSV file is the record.

## Decisions

- **UDP, one JSON object per datagram, newline-terminated** (`src/udp_out.c`). No connection to keep
  alive, so a missing receiver costs nothing. JSON is readable by any language without a schema, and
  the trailing newline makes `nc -ul` output line-based.
- **Field names match the CSV columns** (`epoch_s`, `voltage_v`, `current_a`, `power_w`), with the
  same decimals. One vocabulary for both outputs.
- **Address resolved once at start** with `getaddrinfo` (`src/fnb58d.c`). IPv4 and IPv6 both work.
  An unresolvable address is a configuration error: exit with code 2 before any Bluetooth activity.
- **Non-blocking socket, errors counted, not retried** (`src/udp_out.c`). `MSG_DONTWAIT`; on failure,
  increment a counter. The 1 s tick logs one warning with the count at most every 60 s, so a receiver
  that is down cannot flood the journal.
- **Formatting separate from sending** (`udp_out_format()`), so `tests/test_udp_out.c` can check the
  exact bytes without a network.

## Risks / Trade-offs

- [Unauthenticated stream on a LAN] → anyone on the network can read the readings if `HOST` is a
  broadcast address or a shared host. Mitigation: documented as trusted-network only; the default is off.
- [Loss under load] → datagrams can be dropped by the kernel or the network. Mitigation: the CSV is the
  record, and the stream is for live display only.
