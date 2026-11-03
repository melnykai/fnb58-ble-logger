# fnb58-ble-logger Specification

## Purpose
How fnb58d logs an FNIRSI FNB58 USB tester over Bluetooth LE: finding the tester, starting its
stream, decoding measurements, daily CSV files and their retention, recovery of the link, the events
it reports to the journal, and how it runs and stops as a service.

## Requirements

### Requirement: The daemon finds the tester by name or address
The daemon SHALL scan for a BLE device whose name starts with `--name` (default `FNB58`), or whose
address starts with `--address` when given (case-insensitive), then stop scanning and connect to the
first match.
Verify: hardware

#### Scenario: Default name
- **WHEN** the daemon starts without `--address` and an FNB58 is advertising
- **THEN** the journal shows `found FNB58-<serial> (<address>)`, then `connected to FNB58-<serial>`

#### Scenario: Address given
- **WHEN** the daemon starts with `-a aa:bb:cc:dd:ee:ff`
- **THEN** it connects only to the device with address `AA:BB:CC:DD:EE:FF`

### Requirement: The daemon starts the stream and decodes measurements
Once connected, the daemon SHALL write `aa8100f4`, then `aa8200a7`, to characteristic `ffe9` with
response, then enable notifications on `ffe4`. From frames `AA type len payload check` it SHALL decode
type 0x04 (12 bytes) as three int32 LE values / 10000: voltage in V, current in A, power in W.
Verify: test:proto

#### Scenario: Frames split across notifications
- **WHEN** captured notifications are fed cut at any size from 1 to 100 bytes
- **THEN** every frame is found once, and the first measurement decodes to 5.1613 V, 0.1054 A, 0.5442 W

#### Scenario: Implausible value
- **WHEN** a type 0x04 frame decodes to a voltage outside 0–30 V or a current outside ±10 A
- **THEN** it is not written to the CSV

### Requirement: Measurements go to one CSV file per day
The daemon SHALL append each measurement to `<dir>/<YYYY-MM-DD>_fnb58.csv` (local date), with the
header `time_iso,epoch_s,voltage_v,current_a,power_w`, local time with ms and UTC offset, epoch in s
with 3 decimals and values with 4 decimals. It SHALL flush at least every 1 s and switch file at
local midnight.
Verify: hardware

#### Scenario: Restart on the same day
- **WHEN** the daemon restarts and today's file already exists
- **THEN** it appends to that file without writing a second header

#### Scenario: Crash or power cut
- **WHEN** the daemon is killed while streaming
- **THEN** the file holds every measurement received more than 1 s before the kill

### Requirement: Raw frames on request
With `--raw`, the daemon SHALL also append every frame of every type to
`<dir>/<YYYY-MM-DD>_fnb58_raw.csv`, with the columns `time_iso,epoch_s,type,frame_hex`.
Verify: hardware

#### Scenario: Raw logging enabled
- **WHEN** the daemon runs with `--raw` and the tester streams
- **THEN** the raw file holds frames of types 04, 05, 06, 07 and 08 in hexadecimal

### Requirement: Old CSV files are deleted
When it opens the files of a new day, the daemon SHALL delete the files `<YYYY-MM-DD>_fnb58*.csv` in
its directory dated more than `--keep-days` days ago (default 14). With `--keep-days 0` it SHALL keep
all files. It SHALL NOT delete any other file.
Verify: review

#### Scenario: Day change with old files
- **WHEN** the day changes and the directory holds a file dated 20 days ago and an unrelated `notes.txt`
- **THEN** the old CSV is deleted, the deletion is logged, and `notes.txt` is untouched

### Requirement: A lost link is re-established
When the tester disconnects, the daemon SHALL forget the device in BlueZ and scan again after a backoff
of 1 s, doubling at each failed attempt up to 30 s, back to 1 s after the first measurement of a new
connection. The daemon SHALL never exit on a lost link.
Verify: hardware

#### Scenario: Tester switched off and on
- **WHEN** the tester is switched off while streaming, then switched on again
- **THEN** the journal shows `disconnected`, `rescanning`, `found`, `connected` and `streaming`, and the CSV continues in the same file with a gap equal to the off time

### Requirement: Watchdogs drop a silent or stuck link
The daemon SHALL drop the link and rescan after either of these:
- no measurement for `--watchdog` s (default 10 s, minimum 2 s) while streaming;
- no measurement 30 s after a connection started.

It SHALL also forget a device whose disconnect is not confirmed within 10 s, and SHALL restart a scan
that found nothing for 60 s.
Verify: hardware

#### Scenario: Link up but tester silent
- **WHEN** the link stays up and no measurement arrives for 10 s
- **THEN** the journal shows `no data for 10 s, dropping the link` and the daemon rescans

#### Scenario: Tester absent at start
- **WHEN** the daemon starts and the tester stays off for more than 60 s
- **THEN** the journal shows `restarting discovery` about every 60 s, and the daemon connects once the tester is on

### Requirement: The journal carries events only
The daemon SHALL report start, scan, connect, disconnect, watchdog and file events on stderr, with
syslog priority prefixes (`<3>`, `<4>`, `<6>`) when stderr is not a terminal, plus one stats line every
600 s. It SHALL NOT write one line per measurement.
Verify: hardware

#### Scenario: One hour of streaming
- **WHEN** the daemon runs as a service and streams for one hour without incident
- **THEN** `journalctl -u fnb58d` shows the connection lines and 6 stats lines, with no measurement lines

### Requirement: The service runs unattended
The unit `fnb58d.service` SHALL run `/usr/local/bin/fnb58d` after `bluetooth.service` as a dynamic
system user, writing to `/var/lib/fnb58d`, with extra options from `FNB58D_OPTS` in
`/etc/default/fnb58d`, and SHALL restart it 10 s after any exit.
Verify: review

#### Scenario: No Bluetooth adapter at boot
- **WHEN** the daemon starts and no adapter is available
- **THEN** it logs `no Bluetooth adapter found`, exits with code 1, and systemd starts it again 10 s later

### Requirement: The daemon stops cleanly
On SIGINT or SIGTERM, the daemon SHALL disconnect from the tester, close its files, log the number of
measurements and exit with code 0 within 1 s.
Verify: hardware

#### Scenario: Service stopped
- **WHEN** `systemctl stop fnb58d` is run while streaming
- **THEN** the journal shows `stopping (SIGTERM)` and `stopped: <n> measurements`, and the last CSV line is complete
