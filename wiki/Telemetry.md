**English** | [Deutsch](Telemetrie)

# Telemetry

Everything on the tester screen comes from the ESC (electronic speed
controller), except the battery voltage in the top row, which the board
measures itself. This page explains where each value comes from and how
precise it is.

<img src="img/tester-armed.png" width="240" alt="Telemetry tiles with EDT arriving">

---

## Two and a half sources

**eRPM** (electrical revolutions per minute) comes back on the DShot signal
wire. Every ESC that supports bidirectional DShot sends it. If the link works,
RPM works.

**EDT** (Extended DShot Telemetry) carries voltage, current, temperature,
stress and a status byte on the same wire, interleaved with eRPM frames. It
needs support in the ESC firmware (BLHeli_32, Bluejay, AM32), and the ESC must
receive an enable command. The firmware sends it; see below.

Bidirectional DShot and EDT work on hardware. No defect is known as of v2.3.

**KISS telemetry** is an optional third wire from the ESC's telemetry pad. Its
steps are 0.01 V and 0.01 A, against EDT's 0.25 V and 1 A, and it carries
consumed mAh, which EDT does not. See
[the third wire](#the-optional-third-wire).

---

## The tiles

| Tile | Source | Notes |
|---|---|---|
| **VOLTAGE** | KISS if fresh, else EDT | The tag on the label row names the source |
| **CURRENT** | KISS if fresh, else EDT | Same |
| **ESC TEMP** | KISS if fresh, else EDT | Red at 90 °C and above |
| **STRESS** | EDT only | 0–255, the ESC's own load figure. Amber above 200 |
| **ESC STATUS** | EDT only | `OK` / `WARN` / `ERROR` / `ALERT`. The meaning of each is defined by the ESC firmware |
| **LINK** | The tester's own count | Good packets per second, and the checksum error rate over the last second. Amber above 5 % |

Consumed **mAh** appears in the corner of the temperature tile, with KISS only.

<img src="img/tester-live.png" width="240" alt="Every tile populated">

### The small `KISS` / `EDT` tag

The voltage and current tiles carry a tag on their label row: cyan `KISS` or
dim `EDT`. `12.25 V` from EDT means a value somewhere in a 0.25 V step. The
same reading from KISS is within 0.01 V. When a tag changes from `KISS` to
`EDT`, the telemetry wire has stopped and the reading is 25 times coarser,
although the digits may not change visibly.

---

## Why a tile reads `--`

Every reading expires. An EDT value not refreshed for 1 s shows `--`. After
500 ms without eRPM, the RPM digits show a dark red zero. An ESC that is
unplugged or replaced therefore leaves no old values on screen.

<img src="img/tester-stale.png" width="240" alt="Telemetry expired">

`--` means one of:

- the ESC does not support EDT (RPM still works);
- EDT is supported but not enabled yet. Watch the chip on the settings screen;
- the ESC stopped answering.

An **expired status tile reads `--`, never `OK`**.

---

## Switching EDT on

The firmware does it, with no button.

While an ESC answers with eRPM and no EDT frame has arrived for 1 s, the
firmware sends the enable again every 1 s. After 500 ms without eRPM the state
resets, so an ESC connected later, power-cycled or replaced gets its own
enable.

Each enable is 10 identical frames. It is sent only when all of these hold:

- the link has been up for 300 ms, so the ESC has finished booting;
- the tester is disarmed and the motor is stopped (eRPM is zero);
- no other command, such as BEEP, is being sent.

An ESC executes a DShot command only after a run of identical frames, so an
enable sent at another time would be lost.

<img src="img/config-edt-on.png" width="240" alt="EDT ON"> <img src="img/config-edt-off.png" width="240" alt="EDT OFF">

If the chip stays red with an ESC that supports EDT, see
[Troubleshooting](Troubleshooting#edt-stays-off).

---

## The optional third wire

<img src="img/tester-kiss.png" width="240" alt="Tiles fed from KISS telemetry">

An ESC with KISS telemetry sends a 10-byte frame at 115200 baud from its
telemetry pad when requested. The tester requests one every 20th DShot frame,
which is 50 Hz. The wire adds finer voltage and current, and consumed mAh.

**Status:** reported working by a user on the 2.0" board; not tested by the
project.

| | 2.0" board | 2.8" board |
|---|---|---|
| **Telemetry RX** | GP5, P1 header pin 10 | GP28, J4 pin 11 |

The board only receives on this line. The receiver is a UART (universal
asynchronous receiver-transmitter) built from a PIO (programmable I/O) state
machine, so **any free GPIO works** on either board. GPIO means
general-purpose input/output pin. Turn it on under
**CFG → SETUP → KISS TELEM** and set the pin in the row below. The pin
stepper skips the ESC signal pin.

**Default: on for the 2.0", off for the 2.8".** The 2.8" has two free pins,
GP28 and GP29, and the ESC uses one of them. Turning KISS on moves the KISS pin
off the ESC pin if they coincide.

> **Voltage.** The KISS specification puts this line at 3.6 V, which equals the
> RP2350's absolute-maximum GPIO voltage. The level a given ESC drives is not
> measured by this project. Measure yours before connecting it.

If the wire is unplugged during a session, the tags change to `EDT` 500 ms
after the last KISS frame, and the readings get coarser. They do not hold the
last KISS value.

---

## LINK, and what a bad one looks like

`998/S` with `0% ERR` is a clean 1 kHz link. The error percentage turns amber
above 5 %.

A poor link usually comes from the signal wire: too long, unshielded, or routed
near motor phase wires. Shorten it first. Otherwise set **DSHOT KBAUD** to 300
in SETUP. The `LINK` row on the SETUP screen updates live.
