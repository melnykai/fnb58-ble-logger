# Tasks

## 1. Formatting and sending

- [x] 1.1 `src/udp_out.{h,c}`: `udp_out_format()` writes `{"epoch_s":…,"voltage_v":…,"current_a":…,"power_w":…}\n` with 3 and 4 decimals; verify: `ctest -R udp_out` checks the exact bytes for 5.1613 V, 0.1054 A, 0.5442 W
- [x] 1.2 Non-blocking send with an error counter; verify: unit test sends to a closed local port and the counter increases while the call returns at once

## 2. Daemon

- [x] 2.1 `--udp HOST:PORT` in `src/fnb58d.c`, resolved once with `getaddrinfo`; verify: `--udp nowhere.invalid:9` exits with code 2 and a clear message before scanning
- [ ] 2.2 Send one datagram per accepted measurement, after the CSV line is written; verify: hardware, tester on, `nc -ul 5005` prints one JSON line per CSV line over 60 s
- [ ] 2.3 Rate-limited warning in the 1 s tick; verify: hardware, `--udp` to a host that refuses the port, journal shows at most one `udp: <n> send errors` line per 60 s and the CSV keeps growing

## 3. Docs

- [x] 3.1 README: option row and a receiving example with `nc` and Python; verify: review
