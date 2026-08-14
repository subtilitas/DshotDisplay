#!/usr/bin/env python3
"""Assemble the documentation screenshots from the host suite's output.

    cd test && make          # writes shot_*.ppm
    python3 tools/make_previews.py

The README says the screens "are rendered by the host test suite from the real
UI code, so they cannot drift away from what the board shows". That was only
half true for a while: the frames came from the tests, but stitching them into
docs/*.png was a manual step nobody repeated, so the published images went stale
across several UI changes while the claim stayed put. This script is the missing
half -- one command, run after the tests, and the claim holds again.

Deliberately dependency-free. Pillow would be three lines shorter and one more
thing to install before you can update a picture, which is how the manual step
survived as long as it did.
"""

import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SHOTS = os.path.join(ROOT, "test")
OUT = os.path.join(ROOT, "docs")

SCALE = 2      # the panel is 240x320; 2x keeps the pixel font crisp
GAP = 28       # gutter between panels, in output pixels
BG = (0, 0, 0)

# Which frames go into which published image, and in what order. Names are the
# fakeDumpFrame() arguments in test/, minus the .ppm.
PREVIEWS = {
    "ui-preview": [
        "shot_splash",
        "shot_tester_disarmed",
        "shot_tester_armed",
        "shot_tester_stale",
    ],
    # Three states that actually differ. shot_settings is the same screen with
    # EDT off, so pairing it with shot_config_edt_off published the same
    # picture twice.
    "settings-preview": [
        "shot_config_edt_on",
        "shot_config_edt_off",
        "shot_config_beep_flash",
    ],
    "log-preview": [
        "shot_log_nocard",
        "shot_log_ready",
        "shot_log_screen",
    ],
    # The SETUP screen, and the same screen in high contrast beside it. The
    # pair is the point: a palette that reads well on its own tells you nothing
    # about whether the two are actually different enough to matter outdoors.
    "setup-preview": [
        "shot_setup",
        "shot_setup_contrast",
        "shot_settings_unsaved",
    ],
    # The screen that actually has to survive daylight.
    "contrast-preview": [
        "shot_tester_disarmed",
        "shot_tester_contrast",
    ],
    "am32-preview": [
        "shot_am32_list",
        "shot_am32_edit",
        "shot_am32_hex",
        "shot_am32_written",
    ],
}


def read_ppm(path):
    """Read a binary P6 PPM. Returns (width, height, rows of (r,g,b))."""
    with open(path, "rb") as f:
        data = f.read()

    # The header is "P6\n<w> <h>\n255\n" as written by fakeDumpFrame(), but
    # parse it properly rather than by offset -- a maxval or comment change
    # would otherwise shift every pixel silently.
    fields, pos = [], 0
    while len(fields) < 4:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while pos < len(data) and data[pos] != 0x0A:
                pos += 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    pos += 1  # the single whitespace byte after maxval

    if fields[0] != b"P6":
        raise ValueError("%s is not a binary PPM" % path)
    w, h, maxval = int(fields[1]), int(fields[2]), int(fields[3])
    if maxval != 255:
        raise ValueError("%s: only 8-bit PPMs are handled" % path)

    px = data[pos : pos + w * h * 3]
    if len(px) != w * h * 3:
        raise ValueError("%s: truncated pixel data" % path)
    rows = [
        [tuple(px[(y * w + x) * 3 : (y * w + x) * 3 + 3]) for x in range(w)]
        for y in range(h)
    ]
    return w, h, rows


def write_png(path, width, height, rows):
    """Write a non-interlaced 8-bit RGB PNG."""
    raw = b"".join(
        b"\x00" + b"".join(struct.pack("BBB", *px) for px in row) for row in rows
    )

    def chunk(tag, payload):
        body = tag + payload
        return (
            struct.pack(">I", len(payload))
            + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def build(name, shots):
    frames = []
    for shot in shots:
        path = os.path.join(SHOTS, shot + ".ppm")
        if not os.path.exists(path):
            raise SystemExit(
                "missing %s\n"
                "Run the host tests first:  cd test && make" % os.path.relpath(path, ROOT)
            )
        frames.append(read_ppm(path))

    ph = frames[0][1] * SCALE
    for w, h, _ in frames:
        if h * SCALE != ph:
            raise SystemExit("%s: frames differ in height" % name)

    total = sum(w * SCALE for w, _, _ in frames) + GAP * (len(frames) - 1)
    out = [[BG] * total for _ in range(ph)]

    x0 = 0
    for w, h, rows in frames:
        for y in range(h * SCALE):
            src = rows[y // SCALE]
            dst = out[y]
            for x in range(w * SCALE):
                dst[x0 + x] = src[x // SCALE]
        x0 += w * SCALE + GAP

    path = os.path.join(OUT, name + ".png")
    write_png(path, total, ph, out)
    print("%-20s %d frame(s)  %dx%d" % (name + ".png", len(frames), total, ph))


def main():
    wanted = sys.argv[1:] or sorted(PREVIEWS)
    for name in wanted:
        if name not in PREVIEWS:
            raise SystemExit("unknown preview %r; have: %s"
                             % (name, ", ".join(sorted(PREVIEWS))))
        build(name, PREVIEWS[name])


if __name__ == "__main__":
    main()
