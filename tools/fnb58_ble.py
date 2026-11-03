#!/usr/bin/env python3
"""
Bluetooth LE receiver for the FNIRSI FNB58 USB tester.

Connects to the tester (scans for a name containing "FNB58" unless --address
is given), sends the two start commands, then decodes the notification stream.

Protocol (observed on one FNB58 unit, 2026-11-02):
  GATT  write  0000ffe9-...  (service ffe5)   start: aa8100f4, aa8200a7
        notify 0000ffe4-...  (service ffe0)
  Frames are concatenated inside notifications:  AA <type> <len> <payload:len> <chk>
  The check byte is deterministic but is not a CRC-8/sum/xor we could identify,
  so frames are validated by structure + plausibility only.
  type 0x04 (len 12): int32 LE voltage, current, power, each /10000 -> V, A, W
  types 0x05/0x06/0x07/0x08: not decoded yet, kept as raw hex with --raw.

Usage:
  /usr/bin/python3 fnb58_ble.py                       # print live readings
  /usr/bin/python3 fnb58_ble.py --csv fnb58.csv       # also log to CSV
  /usr/bin/python3 fnb58_ble.py --raw raw.csv -d 60   # also dump every frame, stop after 60 s

Needs bleak (apt: python3-bleak). Outside any venv.
"""

import argparse
import asyncio
import csv
import struct
import sys
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner

NOTIFY_UUID = "0000ffe4-0000-1000-8000-00805f9b34fb"
WRITE_UUID = "0000ffe9-0000-1000-8000-00805f9b34fb"
START_COMMANDS = (bytes.fromhex("aa8100f4"), bytes.fromhex("aa8200a7"))
KNOWN_TYPES = {0x04, 0x05, 0x06, 0x07, 0x08}
MAX_FRAME_LEN = 64


class FrameParser:
    """Reassembles AA-framed packets from a byte stream (frames may span notifications)."""

    def __init__(self):
        self.buf = bytearray()
        self.dropped = 0

    def feed(self, data):
        self.buf += data
        frames = []
        while True:
            start = self.buf.find(0xAA)
            if start < 0:
                self.dropped += len(self.buf)
                self.buf.clear()
                break
            if start:
                self.dropped += start
                del self.buf[:start]
            if len(self.buf) < 3:
                break
            ftype, flen = self.buf[1], self.buf[2]
            if ftype not in KNOWN_TYPES or flen > MAX_FRAME_LEN:
                # Not a frame start: skip this AA and resync
                self.dropped += 1
                del self.buf[:1]
                continue
            total = 3 + flen + 1
            if len(self.buf) < total:
                break
            frames.append((ftype, bytes(self.buf[3:3 + flen]), bytes(self.buf[:total])))
            del self.buf[:total]
        return frames


def decode_measurement(payload):
    """Type 0x04 -> (voltage V, current A, power W) or None if implausible."""
    if len(payload) != 12:
        return None
    v, i, p = (x / 10000 for x in struct.unpack("<iii", payload))
    if not (0.0 <= v <= 30.0 and -10.0 <= i <= 10.0):
        return None
    return v, i, p


async def find_address(name, timeout):
    print(f"Scanning {timeout:.0f} s for '{name}'...", file=sys.stderr)
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: name in (adv.local_name or d.name or ""), timeout=timeout)
    if device is None:
        raise SystemExit(f"No BLE device named *{name}* found (is Bluetooth enabled on the tester?)")
    print(f"Found {device.name} at {device.address}", file=sys.stderr)
    return device.address


async def run(args):
    address = args.address or await find_address(args.name, args.scan_timeout)

    csv_file = raw_file = None
    csv_writer = raw_writer = None
    if args.csv:
        csv_file = open(args.csv, "a", newline="")
        csv_writer = csv.writer(csv_file)
        if csv_file.tell() == 0:
            csv_writer.writerow(["time_iso", "t_s", "voltage_v", "current_a", "power_w"])
    if args.raw:
        raw_file = open(args.raw, "a", newline="")
        raw_writer = csv.writer(raw_file)
        if raw_file.tell() == 0:
            raw_writer.writerow(["time_iso", "t_s", "type", "frame_hex"])

    t0 = time.monotonic()
    deadline = t0 + args.duration if args.duration else None
    count = 0
    last_print = 0.0

    def on_frame(ftype, payload, frame):
        nonlocal count, last_print
        now = time.monotonic() - t0
        stamp = datetime.now().isoformat(timespec="milliseconds")
        if raw_writer:
            raw_writer.writerow([stamp, f"{now:.3f}", f"{ftype:02x}", frame.hex()])
        if ftype != 0x04:
            return
        m = decode_measurement(payload)
        if m is None:
            return
        count += 1
        v, i, p = m
        if csv_writer:
            csv_writer.writerow([stamp, f"{now:.3f}", f"{v:.4f}", f"{i:.4f}", f"{p:.4f}"])
        if not args.quiet and now - last_print >= args.print_every:
            last_print = now
            print(f"{stamp}  {v:7.4f} V  {i:7.4f} A  {p:7.4f} W")

    while deadline is None or time.monotonic() < deadline:
        parser = FrameParser()
        disconnected = asyncio.Event()

        def on_notify(_, data):
            for frame in parser.feed(data):
                on_frame(*frame)

        try:
            async with BleakClient(address, timeout=20,
                                   disconnected_callback=lambda _: disconnected.set()) as client:
                print(f"Connected to {address}", file=sys.stderr)
                for cmd in START_COMMANDS:
                    await client.write_gatt_char(WRITE_UUID, cmd, response=True)
                    await asyncio.sleep(0.1)
                await client.start_notify(NOTIFY_UUID, on_notify)
                while not disconnected.is_set():
                    if deadline and time.monotonic() >= deadline:
                        break
                    await asyncio.sleep(0.5)
                    for f in (csv_file, raw_file):
                        if f:
                            f.flush()
        except Exception as e:  # BleakError, TimeoutError, DBus errors...
            if not (deadline and time.monotonic() >= deadline):  # errors while closing at the end are noise
                print(f"Connection error: {e!r}", file=sys.stderr)

        if deadline and time.monotonic() >= deadline:
            break
        print("Disconnected, reconnecting in 2 s...", file=sys.stderr)
        await asyncio.sleep(2)

    elapsed = time.monotonic() - t0
    print(f"{count} measurements in {elapsed:.1f} s ({count / elapsed:.1f}/s)", file=sys.stderr)
    for f in (csv_file, raw_file):
        if f:
            f.close()


def main():
    ap = argparse.ArgumentParser(description="FNIRSI FNB58 Bluetooth LE receiver")
    ap.add_argument("-a", "--address", help="BLE MAC address (default: scan by name)")
    ap.add_argument("-n", "--name", default="FNB58", help="name substring to scan for")
    ap.add_argument("--scan-timeout", type=float, default=10.0)
    ap.add_argument("--csv", help="append decoded V/A/W to this CSV")
    ap.add_argument("--raw", help="append every raw frame to this CSV (for decoding other types)")
    ap.add_argument("-d", "--duration", type=float, help="stop after N seconds")
    ap.add_argument("--print-every", type=float, default=0.0, help="min seconds between console lines")
    ap.add_argument("-q", "--quiet", action="store_true", help="no console readings")
    args = ap.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
