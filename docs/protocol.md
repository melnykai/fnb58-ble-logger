# FNB58 Bluetooth LE protocol

FNIRSI does not document the Bluetooth protocol of the FNB58. Everything here was observed on
one unit on 2026-11-02 with the scripts in `tools/` and the daemon's `--raw` output. **Confirmed**
means it was seen working on hardware.

## GATT layout (confirmed)

| Service | Characteristic | Properties | Role |
|---|---|---|---|
| `0000ffe0-0000-1000-8000-00805f9b34fb` | `0000ffe4-…` | notify | data from the tester |
| `0000ffe5-0000-1000-8000-00805f9b34fb` | `0000ffe9-…` | write, write without response | commands to the tester |

The tester advertises as `FNB58-<serial>` with service UUID `ffe0`. The default MTU is 23 bytes,
so notifications carry at most 20 bytes.

## Starting the stream (confirmed)

The tester sends nothing until it receives two writes on `ffe9`, in this order:

```
aa 81 00 f4
aa 82 00 a7
```

Then enable notifications on `ffe4`. In the first test, 50 notifications arrived in 5 s. No
keep-alive has been needed in runs of up to 3 min; longer runs are not verified yet.

## Frames

Notifications carry frames back to back, and a frame can be split across two notifications:

```
AA  <type>  <len>  <payload: len bytes>  <check>
```

| Type | Length | Rate (20 s sample) | Meaning |
|---|---|---|---|
| `04` | 12 | 3.5 /s | **voltage, current, power**: three int32 little-endian ÷ 10000 → V, A, W (confirmed: V × A matches W) |
| `05` | 7 | 3.5 /s | unknown |
| `06` | 6 | 8.7 /s | unknown |
| `07` | 4 | 8.7 /s | unknown |
| `08` | 17 | 1.75 /s | unknown, probably charge and energy counters |

Example type `04` frame:

```
aa 04 0c   9d c9 00 00   1e 04 00 00   42 15 00 00   8c
           51613         1054          5442
           5.1613 V      0.1054 A      0.5442 W
```

## The check byte (unknown)

Identical frames always end in the same byte, so it is deterministic. Brute force found no match
among:
- CRC-8 with any polynomial and initial value, reflected or not, starting at byte 0, 1, 2 or 3;
- sums and XORs from byte 0 to 3, with or without negation and a constant offset.

The daemon therefore validates frames by structure (known type, length ≤ 64 bytes) and by value
range (0–30 V, ±10 A).

For comparison, the USB HID commands (64 bytes) of the same tester end in a CRC-8 with polynomial
`0x39`, initial value 0, over the first 63 bytes. That CRC does not produce the Bluetooth check bytes.

## What did not work

These probes produced no notification at all:
- subscribing to `ffe4` without writing anything;
- the 64-byte USB start commands written to `ffe9`, in one write or in 20-byte pieces, with and
  without response;
- short frames `AA cmd 00 crc` and `AA cmd crc` built with the USB CRC.

The working start commands came from [f4inu/FNB58](https://github.com/f4inu/FNB58), a web-based
monitor for this tester (MIT). Thanks to its author: those two writes were the missing piece.

## Exploring further

```
./build/fnb58d -o /tmp/fnb58 --raw              # every frame, hex, per type
python3 tools/fnb58_ble.py --raw raw.csv -d 60  # same with the Python prototype
```

Change one thing at a time on the tester (the load, the temperature, a counter reset) and see which
bytes of types `05`–`08` follow it.
