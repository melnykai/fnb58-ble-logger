# Spec Delta

## ADDED Requirements

### Requirement: Live stream is off by default
Without `--udp`, the daemon SHALL NOT open any network socket, and its behaviour SHALL be unchanged.
Verify: review

#### Scenario: No option given
- **WHEN** the daemon runs without `--udp`
- **THEN** `ss -u -p` lists no socket owned by fnb58d

### Requirement: One datagram per measurement
With `--udp HOST:PORT`, the daemon SHALL send each measurement it writes to the CSV as one UDP
datagram to HOST:PORT, holding one JSON object and a newline: `epoch_s` (s, 3 decimals),
`voltage_v` (V), `current_a` (A) and `power_w` (W), each with 4 decimals, equal to the CSV line.
Verify: test:udp_out

#### Scenario: Formatting
- **WHEN** a measurement of 5.1613 V, 0.1054 A, 0.5442 W at epoch 1793699888.642 s is sent
- **THEN** the datagram is exactly `{"epoch_s":1793699888.642,"voltage_v":5.1613,"current_a":0.1054,"power_w":0.5442}` followed by a newline

#### Scenario: Receiving with standard tools
- **WHEN** the daemon runs with `--udp 127.0.0.1:5005` and the tester streams for 60 s
- **THEN** `nc -ul 5005` prints one JSON line for each line added to the CSV

### Requirement: The stream never disturbs CSV logging
Sending SHALL NOT block. A failed send SHALL NOT stop, delay or skip the CSV line, and SHALL NOT be
retried. The daemon SHALL count failed sends and log at most one warning per 60 s with that count.
Verify: hardware

#### Scenario: Receiver down
- **WHEN** the daemon runs with `--udp` to a host that refuses the port, for 5 min
- **THEN** the CSV grows as usual, and the journal shows at most 5 `udp: <n> send errors` lines

### Requirement: An invalid stream address stops the start
If HOST:PORT cannot be parsed or resolved at start, the daemon SHALL log the reason and exit with
code 2 before any Bluetooth activity.
Verify: hardware

#### Scenario: Unknown host
- **WHEN** the daemon starts with `--udp nowhere.invalid:9`
- **THEN** it logs `udp: cannot resolve nowhere.invalid` and exits with code 2, and the journal shows no `scanning` line
