#!/usr/bin/env python3
"""Plot fnb58d CSV files: voltage, current and power against time.

Readings are grouped into bins (default 10 s); each panel shows the bin mean
as a line and the bin min-max as a shaded band, so short spikes stay visible
on a plot that spans hours. Gaps longer than 3 bins break the line.
With --bin 0 every reading is plotted as it is, for short windows.

    python3 tools/plot_csv.py ~/fnb58/2026-11-03_fnb58.csv -o session.png
    python3 tools/plot_csv.py ~/fnb58/2026-11-03_fnb58.csv -b 0 -s 15:25 -d 120 -o zoom.png
"""
import argparse
import csv
import datetime as dt

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402

FIELDS = (("voltage_v", "Voltage (V)"), ("current_a", "Current (A)"), ("power_w", "Power (W)"))


def read_rows(paths):
    rows = []
    for path in paths:
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                rows.append((float(r["epoch_s"]),) + tuple(float(r[k]) for k, _ in FIELDS))
    rows.sort()
    return rows


def window(rows, start, seconds, tz):
    """Keep the rows from local time START (HH:MM[:SS], day of the first row) for SECONDS."""
    t0 = dt.datetime.fromtimestamp(rows[0][0], tz)
    parts = [int(p) for p in start.split(":")] + [0]
    begin = t0.replace(hour=parts[0], minute=parts[1], second=parts[2], microsecond=0).timestamp()
    return [r for r in rows if begin <= r[0] < begin + seconds]


def raw_rows(rows):
    """One 'bin' per reading, so the plotting code is shared; gaps over 3 s break the line."""
    out, prev = [], None
    for row in rows:
        if prev is not None and row[0] - prev > 3:
            out.append(None)
        out.append((row[0], [(v, v, v) for v in row[1:]]))
        prev = row[0]
    return out


def bin_rows(rows, width):
    """Return a list of bins: (start_epoch, [(mean, min, max) per field]), None at gaps."""
    bins, cur, start = [], [], None
    for row in rows:
        b = row[0] - row[0] % width
        if start is not None and b != start:
            bins.append((start, cur))
            if b - start > 3 * width:
                bins.append(None)
            cur = []
        start = b
        cur.append(row[1:])
    if cur:
        bins.append((start, cur))
    out = []
    for item in bins:
        if item is None:
            out.append(None)
            continue
        start, vals = item
        cols = list(zip(*vals))
        out.append((start + width / 2, [(sum(c) / len(c), min(c), max(c)) for c in cols]))
    return out


def segments(binned):
    seg = []
    for item in binned:
        if item is None:
            if seg:
                yield seg
            seg = []
        else:
            seg.append(item)
    if seg:
        yield seg


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", nargs="+", help="fnb58d CSV file(s), in any order")
    ap.add_argument("-o", "--out", default="fnb58.png", help="output image (default fnb58.png)")
    ap.add_argument("-b", "--bin", type=float, default=10.0, help="bin width in seconds; 0 = every reading (default 10)")
    ap.add_argument("-s", "--start", help="start of a window, local HH:MM[:SS]")
    ap.add_argument("-d", "--duration", type=float, default=600, help="window length in seconds (default 600)")
    ap.add_argument("-t", "--title", default=None, help="plot title")
    args = ap.parse_args()

    tz = dt.datetime.now().astimezone().tzinfo
    rows = read_rows(args.csv)
    if rows and args.start:
        rows = window(rows, args.start, args.duration, tz)
    if not rows:
        ap.error("no readings in the input or the window")
    binned = raw_rows(rows) if args.bin <= 0 else bin_rows(rows, args.bin)

    fig, axes = plt.subplots(3, 1, sharex=True, figsize=(10, 6.5), constrained_layout=True)
    for i, (ax, (_, label)) in enumerate(zip(axes, FIELDS)):
        for seg in segments(binned):
            x = [dt.datetime.fromtimestamp(s[0], tz) for s in seg]
            ax.fill_between(x, [s[1][i][1] for s in seg], [s[1][i][2] for s in seg],
                            color="C%d" % i, alpha=0.25, linewidth=0)
            ax.plot(x, [s[1][i][0] for s in seg], color="C%d" % i, linewidth=1)
        ax.set_ylabel(label)
        ax.grid(True, alpha=0.3)
    span = rows[-1][0] - rows[0][0]
    axes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M" if span > 1800 else "%H:%M:%S", tz=tz))

    hours = span / 3600
    title = args.title or "fnb58d: %d readings over %.1f h" % (len(rows), hours)
    how = "every reading" if args.bin <= 0 else "line: %g s mean, band: min-max" % args.bin
    axes[0].set_title("%s  (%s)" % (title, how), fontsize=10)
    fig.savefig(args.out, dpi=110)
    print("%s: %d readings, %.2f h, %d bins" % (args.out, len(rows), hours, sum(b is not None for b in binned)))


if __name__ == "__main__":
    main()
