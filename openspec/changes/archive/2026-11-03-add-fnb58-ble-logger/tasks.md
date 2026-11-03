# Tasks

## 1. Protocol and parser

- [x] 1.1 Find the GATT layout and the start commands on a real tester; verify: 50 notifications in 5 s after writing `aa8100f4`, `aa8200a7` to `ffe9` (docs/protocol.md)
- [x] 1.2 Frame parser and type 0x04 decoding in `src/fnb58_proto.c`; verify: `ctest --test-dir build -R proto` passes (chunks of 1 to 100 bytes, leading garbage, implausible voltage)

## 2. Daemon

- [x] 2.1 State machine, start sequence, notifications, daily CSV, `--raw`, retention, backoff, watchdogs, events, clean stop in `src/fnb58d.c`; verify: builds with `-Wall -Wextra` and no warnings
- [x] 2.2 Hardware, tester on: 40 s run in a terminal; verify: journal shows `found`, `connected`, `streaming`; 144 readings in the CSV; clean stop on SIGINT with `stopped: 144 measurements`
- [x] 2.3 Hardware, tester switched off for 58 s then on: 3 min run; verify: `disconnected` 6 s after the last reading, then `rescanning in 1 s`, `found`, `connected`, `streaming` 2 s after the tester reappeared; a single 57.7 s gap in the CSV
- [ ] 2.4 Hardware, link up but tester silent; verify: `no data for 10 s, dropping the link`, then a rescan
- [ ] 2.5 Hardware, tester off for more than 60 s; verify: `tester not seen for 60 s, restarting discovery`
- [ ] 2.6 Hardware, run of 12 h or more; verify: no gap over 15 s in the CSV that the tester's state does not explain

## 3. Service

- [x] 3.1 Run as a systemd service under a regular user; verify: journal shows the same lines with priorities (`journalctl -p warning` lists only `disconnected`), and the service reconnects after the tester is unplugged and replugged
- [ ] 3.2 Run the unit as shipped (`DynamicUser=yes`, `/var/lib/fnb58d`); verify: `systemctl status fnb58d` active, and today's CSV in `/var/lib/fnb58d` grows
