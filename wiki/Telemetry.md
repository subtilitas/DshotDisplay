**English** | [Deutsch](Telemetrie)

# Telemetry

Everything on the tester screen except the pack voltage comes from the ESC. This
page is about where each number came from and how much to trust it.

<img src="img/tester-armed.png" width="240" alt="Telemetry tiles with EDT arriving">

---

## Two and a half sources

**eRPM** rides on the DShot signal wire itself and works on every ESC that
supports bidirectional DShot. If the link works at all, you get RPM.

**EDT** — Extended DShot Telemetry — carries voltage, current, temperature,
stress and a status byte on that same wire, interleaved between eRPM frames. It
needs support in the ESC firmware and it needs to be switched on. The firmware
does that for you; see below.

**KISS telemetry** is an optional third wire from the ESC's telemetry pad. It is
finer (0.01 V and 0.01 A against EDT's 0.25 V and 1 A steps) and it carries
consumed mAh, which EDT has no room for at all. See
[the third wire](#the-optional-third-wire).

---

## The tiles

| Tile | Source | Notes |
|---|---|---|
| **VOLTAGE** | KISS if fresh, else EDT | The tag on the label row says which |
| **CURRENT** | KISS if fresh, else EDT | Same |
| **ESC TEMP** | KISS if fresh, else EDT | Turns red at 90 °C and above |
| **STRESS** | EDT only | 0–255, the ESC's own load figure. Amber above 200 |
| **ESC STATUS** | EDT only | `OK` / `WARN` / `ERROR` / `ALERT`. Exact meaning is ESC-firmware specific |
| **LINK** | Neither — this is our own count | Good packets per second, and the checksum error rate over the last second |

Consumed **mAh** appears in the corner of the temperature tile, and only with
KISS.

<img src="img/tester-live.png" width="240" alt="Every tile populated">

### The small `KISS` / `EDT` tag

The voltage and current tiles carry a tag on their label row: cyan `KISS`, dim
`EDT`. It is not decoration. `12.25 V` from EDT means "somewhere in a 0.25 V
bucket"; the same reading from KISS means "within 0.01 V". If a tag drops from
`KISS` back to `EDT`, the telemetry wire has gone quiet and the readout just got
twenty-five times coarser without the number visibly changing.

---

## Why a tile reads `--`

Every reading expires. A value that has not been refreshed within its window
blanks to `--` rather than holding its last number, and the RPM digits go dark
red.

This matters more than it sounds. Unplug the ESC, or swap it for a different
one, and a display that keeps the old figures is not showing stale data — it is
showing a plausible, confident reading from hardware that is not attached. `--`
is the honest answer.

<img src="img/tester-stale.png" width="240" alt="Telemetry expired">

So `--` means one of:

- the ESC does not support EDT at all (you will still get RPM);
- EDT is supported but is not switched on yet — watch the chip on the settings
  screen and give it a moment;
- the ESC stopped answering.

An **expired status tile never reads `OK`**. It reads `--`. A warning indicator
is not allowed to fail in the direction of "all clear".

---

## Switching EDT on

You do not. The firmware does, and keeps doing it.

The rule is "is it working", not "have we asked": while an ESC is answering eRPM
and no EDT frame has arrived for a second, the enable goes out again every
second. Lose the link and the state resets, so an ESC connected later,
power-cycled, or swapped for a different one gets its own enable rather than
silently running without telemetry for the rest of the session.

An attempt is only made when the ESC could actually act on one — the link has
been up long enough for the ESC to have finished booting, the tester is
disarmed, the motor is stopped, and no other command is on the wire. This
matters because a DShot command is only executed after arriving in a run of
identical frames, and an attempt made at the wrong moment is one thrown away.

<img src="img/config-edt-on.png" width="240" alt="EDT ON"> <img src="img/config-edt-off.png" width="240" alt="EDT OFF">

If the chip stays red with an ESC that you know supports EDT, see
[Troubleshooting](Troubleshooting#edt-stays-off).

---

## The optional third wire

<img src="img/tester-kiss.png" width="240" alt="Tiles fed from KISS telemetry">

Most ESCs have a telemetry pad that streams a 10-byte frame on request at
115200 baud. Wiring it up buys you finer voltage and current, and consumed mAh.

| | 2.0" board | 2.8" board |
|---|---|---|
| **Telemetry RX** | GP5 — P1 header, pin 10 | GP28 — J4 pin 11 |

It is receive-only — the board never drives that line — and **any free GPIO will
do**, on either board. Enable it under **CFG → SETUP → KISS TELEM** and set the
pin below it. The pin stepper skips past whichever pin the ESC signal is on, so
the two cannot collide.

**It is on by default on the 2.0" and off on the 2.8".** That is a default, not
a limitation: the 2.8" brings out exactly two free pins, and starting with KISS
on would spend the spare one on a wire most people have not soldered. One tap
turns it on, and it picks the free pin itself.

If the wire is unplugged mid-session, the tags drop back to `EDT` and the
readouts get coarser. They do not freeze on the last fine value, for the same
reason the tiles blank: a steady, precise number from a sensor that stopped
reporting is the worst of both.

---

## LINK, and what a bad one looks like

`998/S` with `0% ERR` is a healthy 1 kHz link. The error percentage turns amber
above 5 %.

A poor link usually means the signal wire: too long, unshielded, or run beside
something noisy. Shorten it first. If that is not possible, drop **DSHOT
KBAUD** to 300 in SETUP — the LINK row on that screen updates live, so you can
see the effect without leaving the page.
