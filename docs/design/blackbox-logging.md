# SD logging in Betaflight blackbox format

Terms: SD is Secure Digital, SPI is Serial Peripheral Interface, SDIO is
Secure Digital input/output, PIO is the RP2350's programmable input/output,
FAT is File Allocation Table, CSV is comma-separated values, ESC is electronic
speed controller, DShot is the digital ESC protocol the tester drives, EDT is
Extended DShot Telemetry, eRPM is electrical revolutions per minute, RPM is
revolutions per minute, KISS telemetry is the serial protocol of the KISS ESC
line on its own wire, UI is user interface, USB is Universal Serial Bus, CI is
continuous integration, VB is variable-byte, SDK is software development kit,
GPIO is general-purpose input/output. In the pin tables, MISO is master in,
slave out, MOSI master out, slave in, SCK and CLK the serial clock, CS and CSn
chip select (n: active low), CMD the SDIO command line, RX receive and TX
transmit.

Status: implemented on both boards. Hardware use is recorded in commits: the
2.8" board mounted a card over SDIO at 25 MHz in `tools/sdtest` and wrote a
file (036adfe); a replacement 2.0" board works over SPI (5b9c7bd). A run at the
shipped SDIO clock of 10 MHz is not recorded separately. BUF PEAK and WORST
FLUSH under sustained logging are not recorded, so the buffer size is
unmeasured. See "Bring-up" below.

## Why blackbox format rather than CSV

A CSV would be less work. The reason not to is tooling:
[logwiju](https://subtilitas.github.io/logwiju/), the intended viewer, a
browser-based plotter that takes a `.BFL` off the card with nothing to install,
along with Blackbox Explorer, `blackbox_decode`, PIDtoolbox and Plasmatree, all
read this format. A log that opens in Blackbox Explorer can be scrubbed, zoomed
and overlaid against flight logs from the same ESC on a real craft, which is
the comparison a bench tester exists to make.

The cost is that the format is a binary format with predictors and
variable-length encodings, and a decoder is unforgiving of small mistakes. So
the encoder is a pure, host-tested unit, and CI checks its output with the
real `blackbox_decode`.

## Scope

Both boards, by two different interfaces: hardware SPI on the 2.0", PIO SDIO
on the 2.8". The board descriptor selects the interface at run time.

## Hardware

The pins live in the per-board headers and reach the driver through the board
descriptor. The interface is chosen at run time: `sdHwConfigApply()` in
`src/sd_hw_config.c` fills both the SPI and the SDIO configuration from
`g_board` before the card is mounted, and the descriptor's `sdIface` field sets
which one the library uses. That is what lets one unified image drive either
board. `SD_IFACE_SPI` and `SD_IFACE_SDIO` in the board headers remain only for
`tools/sdtest`.

**RP2350-Touch-LCD-2 (2.0"): hardware SPI1, one bit wide, 12 MHz
(`SD_LOG_SPI_MHZ`):**

| Signal | GPIO | SPI1 function |
|---|---|---|
| `PIN_SD_MISO` | 24 | SPI1 RX |
| `PIN_SD_CS` | 25 | SPI1 CSn |
| `PIN_SD_SCK` | 26 | SPI1 SCK |
| `PIN_SD_MOSI` | 27 | SPI1 TX |

The display is on SPI0 on this board, so card writes and the display's
multi-millisecond DMA (direct memory access) bursts do not contend for a
peripheral. They still contend for core0 time.

**RP2350-Touch-LCD-2.8: PIO SDIO, four bits wide, 10 MHz
(`SD_LOG_SDIO_HZ`):**

| Signal | GPIO |
|---|---|
| `PIN_SD_SCK` (CLK) | 19 |
| `PIN_SD_CMD` | 20 |
| `PIN_SD_D0`..`D3` | 21, 22, 23, 24 |

Hardware SPI is impossible there: `SD_SCK` is GP19, which is SPI0 **TX** in
the pin mux, not SCK. The pins satisfy every constraint the PIO program
imposes (D1..D3 directly follow D0, and CLK is `(D0 + 30) % 32`), and
`board_desc_lcd2_8.cpp` asserts both at compile time. `sdtest` mounted a card
there at 25 MHz; the firmware ships at 10 MHz, because SDIO is more sensitive
to signal integrity than one-bit SPI, and a card that enumerates and then
corrupts is worse than one that refuses.

SDIO runs on **pio1**, and nothing else does: its programs take 2 state
machines and all 32 instruction slots. The DShot driver (2 state machines, 16
instructions) and the KISS receiver (1 state machine, 9 instructions) claim
from whichever block has room, and the SDK searches pio2 first, so both land
on pio2. Two drivers sharing one block would make whichever initialised
second fail depending on timing.

Library: FatFs, via carlk3's `no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`, pinned at
v3.6.2. `f_expand()` reserves a contiguous cluster run of
`SD_LOG_PREALLOC_BYTES`, 16 MiB, when a log opens, so writes do not stall on
FAT allocation mid-log. A failed reservation is not reported; the log then
allocates as it grows.

SdFat is not used: it depends on Arduino, and the firmware builds on the
native Pico SDK.

## File format

Checked against `betaflight/blackbox-tools` (`src/parser.c`,
`src/blackbox_fielddefs.h`).

### Header

ASCII (American Standard Code for Information Interchange) `H <name>:<value>\n`
lines. The first must be exactly the start marker;
the parser uses it to find log boundaries in a file that may hold several.
`bbBegin()` in `src/blackbox_encode.cpp` writes, among others:

```
H Product:Blackbox flight data recorder by Nicholas Sherlock
H Data version:2
H Firmware type:Cleanflight
H Firmware revision:Betaflight 4.5.0 (DshotDisplay)
H Craft name:DshotDisplay
H I interval:32
H P interval:1/1
H Field I name:loopIteration,time,...
H Field I signed:0,0,...
H Field I predictor:0,0,...
H Field I encoding:1,1,...
H Field P predictor:...
H Field P encoding:...
```

If the `P` name and signed lines are absent the parser copies them from `I`,
but predictor and encoding must be given for both.

Field 0 must be `loopIteration` and field 1 must be `time` (µs):
`FLIGHT_LOG_FIELD_INDEX_ITERATION` and `_TIME` are hardcoded to 0 and 1.

### Frames

| Marker | Meaning |
|---|---|
| `I` | Intra: every field absolute. The keyframe. |
| `P` | Inter: every field predicted from history. |
| `E` | Event, including `LOG_END` (255). |
| `S` | Slow: infrequently changing fields. Not written. |
| `G`, `H` | GPS (Global Positioning System). Not applicable here. |

### Predictors and encodings

Predictors in the format: `0` none, `1` previous, `2` straight line, `3`
average of 2, `4` minthrottle, `5` motor 0, `6` increment, `8` 1500, `9`
vbatref, `10` last main frame time, `11` min motor.

Encodings in the format: `0` signed VB, `1` unsigned VB, `3` neg 14-bit, `6`
TAG8_8SVB, `7` TAG2_3S32, `8` TAG8_4S16, `9` null (nothing written, read as
zero).

The encoder uses predictors 0, 1, 2 and 6 and encodings 0, 1 and 9. The first
two fields follow Betaflight:

| Field | I predictor | I encoding | P predictor | P encoding |
|---|---|---|---|---|
| `loopIteration` | 0 (none) | 1 (unsigned VB) | 6 (increment) | 9 (null) |
| `time` | 0 (none) | 1 (unsigned VB) | 2 (straight line) | 0 (signed VB) |

Every other field is predictor 0 and unsigned VB in I frames (signed VB for
temperature), predictor 1 (previous) and signed VB in P frames.
`loopIteration` in P frames costs zero bytes.

## Field set

Betaflight names where one fits, so existing tools label the axes. Invented
names still decode (`blackbox_decode` emits them as CSV columns), but Blackbox
Explorer draws only fields it recognises.

| Field | Source | Units | Notes |
|---|---|---|---|
| `loopIteration` | counter | — | required at index 0 |
| `time` | `micros()` | µs | required at index 1 |
| `motor[0]` | commanded throttle | 0..2000 | what the operator asked for, not what the interlocks sent |
| `eRPM[0]` | bidirectional DShot | eRPM | every DShot frame; relative resolution ~0.2–0.4 % |
| `eRPMkiss[0]` | KISS | eRPM | ~50 Hz; 100 eRPM steps; 0 while KISS is stale |
| `vbatLatest` | KISS only | 0.01 V | 0 while KISS is not the voltage source |
| `amperageLatest` | KISS only | 0.01 A | 0 while KISS is not the current source |
| `vbatEdt` | EDT | 0.01 V | non-standard name; 0 once EDT voltage is stale |
| `amperageEdt` | EDT | 0.01 A | non-standard name; 0 once EDT current is stale |
| `escTemperature[0]` | KISS, else EDT | °C | the one signed field |
| `escConsumption` | KISS only | mAh | non-standard name |
| `escStress` | EDT | 0..255 | non-standard name |

`vbatLatest` and `amperageLatest` are looked up by name in `parser.c`.
`eRPM[0]` and `escTemperature[0]` follow Betaflight's own naming but are not
in the parser's well-known list, so they decode as ordinary columns; a real
Betaflight log to compare spellings against has not been checked.

Every field but temperature is declared unsigned, and a negative value in an
unsigned field wraps to about 4.29e9 instead of failing, so values are clamped
non-negative before encoding.

Both eRPM sources are logged. Neither dominates: bidirectional DShot wins on
rate, KISS wins on resolution above about 6,500 mechanical RPM on a 12-pole
motor, 5,500 on 14 poles and 4,800 on 16.
See [kiss-telemetry.md](kiss-telemetry.md#erpm-which-source-is-finer).
Voltage and current are also logged per source rather than merged. It costs
about two bytes a frame, and it means the KISS decoder can be checked against
EDT from the log itself. The UI shows the merged value with a source tag; the
log keeps the sources apart.

## Rate and volume

DShot runs at 1 kHz, but eRPM is the only signal here that changes at that
rate. KISS arrives at 50 Hz and the display repaints at 40 Hz.

The log writes 500 Hz frames (`SD_LOG_RATE_HZ`) with an I frame every 32
(`SD_LOG_I_INTERVAL`, Betaflight's usual value). Both are compile-time
settings. With 12 fields `config.h` gives about 14.5 bytes a frame, 7.3 kB/s or
26 MB an hour.

## Buffering and where it runs

The failure to design against: an SD card stalls for tens of milliseconds
during an internal erase, and code that blocks on it for that long breaks
whatever else it was doing.

In `src/sd_log.cpp` and `src/log_ring.cpp`:

- `sdLogTick()` encodes into a RAM (random-access memory) ring buffer of
  `SD_LOG_BUFFER_BYTES`, 8192 bytes. `sdLogFlush()` writes it out in
  `SD_LOG_CHUNK_BYTES`, 512-byte, chunks and waits for a full chunk except
  while a log is being stopped.
- Nothing on core1 touches the card. Core1's job is DShot timing, and a 30 ms
  card stall would destroy it.
- Core0 owns the card and calls both functions on every pass of its loop,
  after the UI frame.
- A frame that does not fit in the ring is dropped whole and counted, never
  half-written and never waited for. A gap in the log is recoverable; a stalled
  UI with a live motor is not. The SD LOG screen shows the dropped-frame count.
- The ring tracks its high-water mark (BUF PEAK) and the log tracks its
  slowest single write or sync (WORST FLUSH). The SD LOG screen shows BUF PEAK
  against the buffer size, amber above half and red above three quarters.
- 8192 bytes holds about 1.1 s at 7.3 kB/s. It is not sized from a measured
  stall; see "Bring-up".

## Lifecycle

- The card is mounted at boot. An absent card is not an error: logging is
  unavailable and the screen says so. RETRY mounts a card inserted
  later. A failed mount shows the FatFs result and the card type.
- Files are `LOG00001.BFL` onwards, at the first free number. Betaflight uses
  `.BFL` for single logs and `.BBL` for concatenated ones.
- Logging starts on arming and stops on disarming (`SD_LOG_AUTO_ON_ARM`).
  START and STOP on CFG → SD LOG work by hand, and a log started by hand
  survives the disarm that entering the settings screens performs.
- On stop: an `E` frame with `LOG_END` (255), the ring drained, the unused
  pre-allocation released with `f_truncate()`, then sync and close.
- The directory entry is synced every `SD_LOG_SYNC_MS`, 2000 ms, right after a
  chunk goes out, so a log cut by a power loss is readable up to its last sync.
- USB DRIVE on CFG → SD LOG hands the card to a USB host as a read-only drive.
  The logger flushes, closes and unmounts first, and every logging entry point
  is inert until the host ejects. The handover is refused while armed or
  recording, and arming is refused while a host holds the card.

## Testing

The encoder is pure and runs under the host suite. The strong test is not a
hand-written expectation of the bytes but whether the real decoder accepts
them.

1. Primitives (`test/test_blackbox.cpp`): unsigned VB, signed VB zig-zag,
   boundary values. TAG8_8SVB and the other tag encodings are not used and
   not implemented.
2. Header and frames, and the straight-line time prediction across I frames.
3. `make -C test bbcheck`, run by CI's Blackbox round-trip job: 2000 generated
   frames go through Betaflight's `blackbox_decode`, built from source, and
   all 24,000 values must round-trip with no corrupt frames. A unit test
   checks the bytes against the same reading of the format the encoder was
   written from; the round trip checks them against the reference decoder.
4. A truncated file (the last frame cut in half) decoding up to the cut: not
   implemented.
5. The encoder writes to a `BlackboxSink` the caller supplies, so the host
   suite writes to memory or a file and the device to the ring.
6. The ring buffer (`test/test_log_ring.cpp`): all-or-nothing writes,
   wraparound, high-water mark, zero size.
7. The SD LOG screen, manual logging against auto-on-arm, and the USB DRIVE
   handover rules, through `test/test_ui.cpp` and `test/test_usb_msc.cpp`.

## Where the code is

- `src/blackbox_encode.h` and `.cpp`: primitives, header, I and P frames,
  predictor state.
- `src/log_ring.h` and `.cpp`: the buffer the encoder writes into.
- `src/sd_log.cpp`: mount, pre-allocation, ring, chunked flush, overrun
  counting, periodic sync, and the field table filled from `EscTelemetry` and
  the commanded throttle.
- `src/ui.cpp`: CFG → SD LOG, with card state, start and stop, bytes written
  and drops, and the REC (recording) indicator in the status bar.
- `src/config.h`: log rate, I interval, buffer size, chunk size,
  pre-allocation, sync interval, auto-on-arm. Field selection is not
  configurable.
- `.github/workflows/ci.yml`, job Blackbox round-trip: builds
  `blackbox-tools`, generates a log, decodes it.
- README.md, section "Blackbox logging to microSD".

## Bring-up

Recorded on hardware: the 2.8" board mounts a card over SDIO at 25 MHz in
`tools/sdtest` and writes a file (036adfe). A replacement 2.0" board mounts a
card and writes over SPI (5b9c7bd).

One 2.0" board has GPIO27, the card's CMD line (SPI MOSI), shorted to ground.
On that board every card reports FatFs `FR_NOT_READY` at every clock rate from
12 MHz down to 200 kHz, because the card never receives a command.
`tools/sdtest` shows the short: the pin does not go high when driven
push-pull with the slot empty. The replacement board works over the same SPI
path, so the fault is in that unit, not in the design. That record excludes
these causes of `FR_NOT_READY`:

- the pin map, checked against the schematic netlist;
- the SPI clock: all five rates fail identically;
- exFAT (Extended File Allocation Table) support, which is enabled;
- SDXC (Secure Digital eXtended Capacity) cards, whose HCS (Host Capacity
  Support) and CCS (Card Capacity Status) bits the driver handles;
- the driver's init sequence, which sends the 74-clock preamble;
- the firmware integration: `tools/sdtest` shares only `sd_hw_config.c` with
  the firmware and fails the same way.

Two properties of `tools/sdtest` keep its answers correct:

- The pin test drives each pad and reads it back. It does not use the
  internal pull-down: on the RP2350 that triggers erratum **RP2350-E9**, where
  a pad so configured latches at about 2.1 V and reads high from then on, and
  a good line reports as "held high".
- Each attempt at a lower clock forces a full re-initialisation. The driver
  caches both the SPI setup and the card state and revisits neither, so a
  plain retry runs at the first speed.

Unmeasured: BUF PEAK and WORST FLUSH under sustained logging, which are what
should size `SD_LOG_BUFFER_BYTES`. 8192 bytes is unmeasured.

## Open questions

- Is an `S` (slow) frame worth it for temperature and status, or is the saving
  too small to justify the extra frame type? Open, and minor.
- Does anything downstream care that `Firmware type` claims Cleanflight? That
  is what Betaflight itself writes, and the parser recognises only that and
  "Betaflight".
- What does a log cut by a power loss look like after its last sync? The
  pre-allocation is released only by `sdLogStop()`, so the file's recorded
  size after a power loss, and whether every viewer stops cleanly at the end of
  the written data, are not tested.

## Sources

- [`betaflight/blackbox-tools`](https://github.com/betaflight/blackbox-tools): `src/parser.c` and `src/blackbox_fielddefs.h`, the reference reader
- [Betaflight blackbox source](https://github.com/betaflight/betaflight/tree/master/src/main/blackbox): the reference writer
- [`carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico): FatFs over SPI and SDIO, RP2350-capable
