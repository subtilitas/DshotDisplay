# Design notes

These documents record the design decisions behind two features: protocol
details, pin choices, and what runs on which core. Both features are built,
and each document describes the code as it is. Release history is in
[CHANGELOG.md](../../CHANGELOG.md).

A document that no longer matches the code is a bug. When a feature changes,
update its document, or fold it into the README and delete it.
[STATUS.md](../../STATUS.md) holds the verification record for the whole
firmware; the status lines here summarise it per feature.

| Document | Status |
|---|---|
| [kiss-telemetry.md](kiss-telemetry.md) | Implemented on both boards. Working on hardware (owner report). Off by default on the 2.8" board. |
| [blackbox-logging.md](blackbox-logging.md) | Implemented on both boards. Hardware use recorded in commits on both boards. Buffer sizing unmeasured. |

## Shared context

KISS telemetry, the serial protocol of the KISS ESC (electronic speed
controller) line, sent on its own wire, supplies finer voltage and current
than EDT (Extended DShot Telemetry, carried in the replies of the DShot digital
ESC protocol). The log is the main use of that resolution: a voltage trace in
0.25 V steps hides a 200 mV sag.

SD (Secure Digital) logging runs on both boards: the 2.8" over four-bit PIO
(the RP2350's programmable input/output) SDIO (Secure Digital input/output) at
10 MHz, the 2.0" over one-bit hardware SPI (Serial Peripheral Interface) at
12 MHz. A USB (Universal Serial Bus) host can read the card as a read-only
drive.

KISS telemetry is implemented on both boards. The owner reports it working on
hardware, the display's own telemetry request included; which board that
report covers is not recorded. The receiver is a PIO state machine, so any
free GPIO (general-purpose input/output) can carry the wire. The 2.8" board
frees two GPIOs, GP28 and GP29. The ESC takes GP29 and KISS defaults to GP28,
switched off until the user turns it on.

What remains is measurement. BUF PEAK and WORST FLUSH under sustained logging
should size the ring buffer; 8 kB (`SD_LOG_BUFFER_BYTES`, 8192 bytes) is
unmeasured, and the SD LOG screen shows both numbers so they can be read off
the device. On the KISS side, the line voltage is not measured.
