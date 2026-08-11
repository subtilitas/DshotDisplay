#!/usr/bin/env python3
"""Decode a generated blackbox log and check every value survived the trip.

Run by `make bbcheck`. Generates a log with test/gen_blackbox_log.cpp, decodes
it with the upstream `blackbox_decode`, and compares the decoded CSV against the
values the generator put in.

This is the test that matters. The unit tests in test_blackbox.cpp only prove
the encoder agrees with my reading of the format; this proves it agrees with the
tool people will actually open the log in. The two are not the same thing -- the
I-frame history-rotation bug passed every unit test and produced a file that
decoded without complaint, but with a time column that stalled every 32 frames.

Usage: check_blackbox.py <blackbox_decode> <generator>
"""

import math
import subprocess
import sys
import tempfile
import os

FRAMES = 2000
PERIOD_US = 2000


def expected(i):
    """Mirror of sample() in gen_blackbox_log.cpp. Must stay in step with it."""
    t = i / 100.0
    s = math.sin(t)
    # C truncates toward zero when casting a negative double to int; Python's
    # int() does the same, but math.floor() would not. Keep int().
    erpm = 40000 + int(20000.0 * s)
    vbat = 1650 - int(120.0 * s)
    amps = int(800.0 + 700.0 * s)
    return {
        "loopIteration": i,
        "time (us)": i * PERIOD_US,
        "motor[0]": 1000 + int(900.0 * s),
        "eRPM[0]": erpm,
        "eRPMkiss[0]": (erpm // 100) * 100 if erpm >= 0 else -((-erpm // 100) * 100),
        "vbatLatest (V)": vbat,          # decoder rescales; compared as centivolts
        "amperageLatest (A)": amps,      # likewise, centiamps
        "vbatEdt": (vbat // 25) * 25,
        "amperageEdt": (amps // 100) * 100,
        "escTemperature[0]": -5 + i // 100,
        "escConsumption": i // 4,
        "escStress": (i // 37) % 256,
    }


def main():
    if len(sys.argv) < 3:
        print("usage: check_blackbox.py <blackbox_decode> <generator>", file=sys.stderr)
        return 2
    decoder, generator = sys.argv[1], sys.argv[2]

    tmpdir = tempfile.mkdtemp()
    log = os.path.join(tmpdir, "test.BFL")

    gen = subprocess.run([generator, log], capture_output=True, text=True)
    if gen.returncode != 0:
        print("generator failed:", gen.stderr, file=sys.stderr)
        return 1
    print("  generated:", gen.stderr.strip())

    out = subprocess.run([decoder, "--stdout", log], capture_output=True, text=True)
    if out.returncode != 0:
        print("decoder failed:", out.stderr, file=sys.stderr)
        return 1

    lines = [l for l in out.stdout.splitlines() if l.strip()]
    header = [h.strip() for h in lines[0].split(",")]

    # The decoder prints its statistics on stdout too, after the CSV rows.
    rows = []
    for line in lines[1:]:
        parts = [p.strip() for p in line.split(",")]
        if len(parts) != len(header) or not parts[0].lstrip("-").isdigit():
            break
        rows.append(parts)

    failures = 0

    if len(rows) != FRAMES:
        print(f"  FAIL: decoded {len(rows)} rows, expected {FRAMES}")
        failures += 1
    else:
        print(f"  decoded {len(rows)} rows")

    # Any frame the decoder could not read shows up here.
    for bad in ("corrupt", "desync", "Failed"):
        for line in out.stdout.splitlines():
            if bad.lower() in line.lower() and "0" not in line.split()[-1:]:
                print("  FAIL:", line.strip())
                failures += 1

    idx = {name: i for i, name in enumerate(header)}
    checked = 0
    for r, parts in enumerate(rows):
        exp = expected(r)
        for name, want in exp.items():
            if name not in idx:
                continue
            raw = parts[idx[name]]
            # vbatLatest and amperageLatest are well-known names, so the decoder
            # converts them to volts and amps. Undo that to compare against the
            # centivolt/centiamp values that went in.
            if name in ("vbatLatest (V)", "amperageLatest (A)"):
                got = int(round(float(raw) * 100))
            else:
                got = int(raw)
            if got != want:
                if failures < 10:
                    print(f"  FAIL: row {r} {name}: got {got}, want {want}")
                failures += 1
            checked += 1

    print(f"  compared {checked} values across {len(rows)} frames")
    if failures:
        print(f"  {failures} MISMATCH(ES)")
        return 1
    print("  every value round-tripped exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
