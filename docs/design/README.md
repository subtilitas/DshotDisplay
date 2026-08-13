# Design notes

Documents here describe work that is planned or in progress. They are written
before the code so the expensive decisions — protocol details, pin choices, what
runs on which core — can be argued about while they are still cheap to change.

A document that no longer matches the code is a bug. When a feature lands,
either fold the document into the README and delete it, or update it to describe
what was actually built and why it diverged.

| Document | Status |
|---|---|
| [kiss-telemetry.md](kiss-telemetry.md) | Implemented; untested against an ESC |
| [blackbox-logging.md](blackbox-logging.md) | Working on hardware, both boards |

## Shared context

Both features landed on `dev/telemetry-logging`, cut from `main`. They are related:
KISS supplies higher-resolution voltage and current, and the log is the main
reason that resolution is worth having — a 0.25 V/LSB voltage trace is not worth
plotting.

SD logging works on both boards: the 2.8" over SDIO at 25 MHz, the 2.0" over
SPI. KISS telemetry is implemented but has not met an ESC yet, and on the 2.8"
it cannot -- that board has no pin free for the telemetry wire once the DShot
signal has one.

What remains is measurement rather than construction. BUF PEAK and WORST FLUSH
under sustained logging are what should size the ring buffer; 8 kB is still a
guess, and the logging screen exists largely so those two numbers can be read
off the device.
