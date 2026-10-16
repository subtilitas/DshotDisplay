# Design notes

These documents record the design decisions behind each feature: protocol
details, pin choices, and what runs on which core. A document is written
before the code. While its feature is planned it describes the system as it
will be; once the feature is built it describes the code as it is and says
where it diverged from the plan.

A document that no longer matches the code is a bug. When a feature changes,
update its document, or fold it into the README and delete it.
[STATUS.md](../../STATUS.md) holds the verification record for the whole
firmware; the status lines here summarise it per feature.

| Document | Status |
|---|---|
| [kiss-telemetry.md](kiss-telemetry.md) | Implemented on both boards. Working on hardware (owner report). Off by default on the 2.8" board. |
| [blackbox-logging.md](blackbox-logging.md) | Implemented on both boards. Hardware use recorded in commits on both boards. Buffer sizing unmeasured. |
| [sdio-driver.md](sdio-driver.md) | Implemented on the 2.8" board, not tested on hardware. MIT SDIO driver written from the SD specification; replaces the SD library's GPL-derived one. Host tests against a simulated card. |
| [pin-claim.md](pin-claim.md) | Planned. A screen takes the ESC and KISS pins from DShot while it is open; shared hardware facts and the slice plan. |
| [servo-tester.md](servo-tester.md) | Planned. Servo PWM output, manual position, sweep, two servos. Depends on the pin claim. |
| [kst-programming.md](kst-programming.md) | Planned. KST and Chaservo servo programming, ported from rcbench. Depends on the pin claim. |

## Shared context

KISS (the serial telemetry protocol of the KISS ESC, electronic speed
controller, line, sent on its own wire) supplies finer voltage and current than
EDT (Extended DShot Telemetry, carried in the replies of the DShot digital ESC
protocol), and
the log is the main reason that resolution is worth having: a trace in
0.25 V steps is not worth plotting.

Both features are on `main` and first shipped as follows. The KISS decoder and
request path were built on `dev/telemetry-logging` and released in v1.1.0,
where nothing displayed or logged the values yet. The SD (Secure Digital) card
writer was built on `dev/pico-sdk-native`; the logging screen, the KISS
readout and the 2.8" SDIO (Secure Digital input/output) path on
`dev/multiboard-sdio`; the PIO (programmable input/output) KISS receiver on
`feat/runtime-config`. All of that first shipped in v2.0. The USB (Universal
Serial Bus) card reader came from `feat/usb-mass-storage` in v2.2.

SD logging runs on both boards: the 2.8" over four-bit PIO SDIO at 10 MHz, the
2.0" over one-bit hardware SPI (Serial Peripheral Interface) at 12 MHz. The
SDIO path runs on the driver in `src/sdio/` ([sdio-driver.md](sdio-driver.md)),
which has no hardware record yet.

KISS telemetry is implemented on both boards; a hardware record exists only
for the 2.0" board. The receiver is a PIO state machine, so
any free GPIO (general-purpose input/output) can carry the wire. The 2.8"
board frees two GPIOs, GP28 and GP29; the ESC takes GP29 and KISS defaults to GP28, switched off until the user turns it on.

What remains is measurement. BUF PEAK and WORST FLUSH under sustained logging
should size the ring buffer; 8 kB (`SD_LOG_BUFFER_BYTES`, 8192 bytes) is
unmeasured, and the SD LOG screen shows both numbers so they can be read off
the device. On the KISS side, the display's own telemetry request and the line
voltage have no hardware record.
