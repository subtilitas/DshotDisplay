# SD logging in Betaflight blackbox format

Status: implemented including UI. Not yet proven against a card: the board it
was developed on has GPIO27 shorted to ground, so its SD slot cannot work at
all. See "Bring-up" below.
Branch: `dev/telemetry-logging`, ported to the native Pico SDK on
`dev/pico-sdk-native`.

## Why blackbox format rather than CSV

A CSV would be less work. The reason not to is tooling: Blackbox Explorer,
`blackbox_decode`, PIDtoolbox and Plasmatree all read this format already. A log
that opens in Blackbox Explorer can be scrubbed, zoomed and overlaid against
flight logs from the same ESC on a real craft, which is exactly the comparison a
bench tester exists to make.

The cost is that the format is a real binary format with predictors and
variable-length encodings, and a decoder is unforgiving of small mistakes. That
is a good argument for building the encoder as a pure, host-tested unit and
validating its output with the actual `blackbox_decode` binary in CI.

## Scope

**2.0" board only.** The 2.8" board's SD pins do not map onto any hardware SPI
instance — `SD_SCK` is GP19, which is SPI0 **TX** in the RP2350 pin mux, not
SCK — so it would need a PIO-SPI or SDIO driver. Out of scope. This branch is
cut from `main`, which is 2.0"-only, so there is nothing to compile out.
(`SD_LOG_ENABLE` exists anyway, and CI builds both settings.)

## Hardware

Pins are already in `board_pins.h` and map cleanly onto SPI1:

| Signal | GPIO | SPI1 function |
|---|---|---|
| `PIN_SD_MISO` | 24 | SPI1 RX |
| `PIN_SD_CS` | 25 | SPI1 CSn |
| `PIN_SD_SCK` | 26 | SPI1 SCK |
| `PIN_SD_MOSI` | 27 | SPI1 TX |

The display is on SPI0, so SD writes and the display's multi-millisecond DMA
bursts do not contend for a peripheral. They still contend for core0 time.

Library: FatFs, via carlk3's `no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`. `f_expand()`
reserves a contiguous cluster run up front so writes do not stall on FAT
allocation mid-log.

(The original plan named SdFat. That was written while this was still an Arduino
project; SdFat is Arduino-bound and did not survive the move to the native Pico
SDK.)

## File format

Confirmed against `betaflight/blackbox-tools` (`src/parser.c`,
`src/blackbox_fielddefs.h`).

### Header

ASCII `H <name>:<value>\n` lines. The first must be exactly the start marker —
the parser uses it to find log boundaries in a file that may hold several.

```
H Product:Blackbox flight data recorder by Nicholas Sherlock
H Data version:2
H Firmware type:Cleanflight
H Firmware revision:Betaflight 4.5.0 (DshotDisplay)
H I interval:32
H P interval:1/1
H Field I name:loopIteration,time,...
H Field I signed:0,0,...
H Field I predictor:0,0,...
H Field I encoding:1,1,...
H Field P name:...
H Field P signed:...
H Field P predictor:...
H Field P encoding:...
```

If the `P` name/signed lines are absent the parser copies them from `I`, but
predictor and encoding must be given for both.

Field 0 must be `loopIteration` and field 1 must be `time` (µs) —
`FLIGHT_LOG_FIELD_INDEX_ITERATION` and `_TIME` are hardcoded to 0 and 1.

### Frames

| Marker | Meaning |
|---|---|
| `I` | Intra — every field absolute. The keyframe. |
| `P` | Inter — every field predicted from history. |
| `E` | Event, including `LOG_END` (255). |
| `S` | Slow — infrequently changing fields. |
| `G`, `H` | GPS. Not applicable here. |

### Predictors and encodings

Predictors: `0` none, `1` previous, `2` straight line, `3` average of 2,
`4` minthrottle, `5` motor 0, `6` increment, `8` 1500, `9` vbatref,
`10` last main frame time, `11` min motor.

Encodings: `0` signed VB, `1` unsigned VB, `3` neg 14-bit, `6` TAG8_8SVB,
`7` TAG2_3S32, `8` TAG8_4S16, `9` null (nothing written, read as zero).

The standard treatment of the first two fields, which we should copy verbatim
rather than invent:

| Field | I predictor | I encoding | P predictor | P encoding |
|---|---|---|---|---|
| `loopIteration` | 0 (none) | 1 (unsigned VB) | 6 (increment) | 9 (null) |
| `time` | 0 (none) | 1 (unsigned VB) | 2 (straight line) | 0 (signed VB) |

`loopIteration` in P frames therefore costs zero bytes.

## Field set

Betaflight names where one fits, so existing tools label the axes correctly.
Invented names still decode — `blackbox_decode` emits them as CSV columns — but
Blackbox Explorer only draws graphs it recognises.

| Field | Source | Units | Notes |
|---|---|---|---|
| `loopIteration` | counter | — | required at index 0 |
| `time` | `micros()` | µs | required at index 1 |
| `motor[0]` | commanded throttle | 0..2000 | `rcCommand[3]` is the other candidate |
| `eRPM[0]` | bidir DShot | eRPM | 1 kHz; relative resolution ~0.2–0.4% |
| `eRPMkiss[0]` | KISS | eRPM | ~50 Hz; flat 100 eRPM steps |
| `vbatLatest` | KISS, else EDT | 0.01 V | `vbatscale`/`vbatref` in the header |
| `amperageLatest` | KISS, else EDT | 0.01 A | |
| `vbatEdt` | EDT | 0.01 V | non-standard name |
| `amperageEdt` | EDT | 0.01 A | non-standard name |
| `escTemperature[0]` | KISS, else EDT | °C | slow-moving; candidate for an S frame |
| `escConsumption` | KISS only | mAh | non-standard name |
| `escStress` | EDT | 0..255 | non-standard name |

`vbatLatest` and `amperageLatest` are certain — `parser.c` looks them up by name.
`eRPM[0]` and `escTemperature[0]` follow Betaflight's own naming but are *not* in
the parser's well-known list, so they decode as ordinary columns. Confirm against
a real Betaflight log before settling on them; matching the exact spelling is the
whole reason to prefer these names over invented ones.

Voltage and current units are chosen to match `escSensorData_t`, so KISS values
go in unscaled and the lossy step is only on the EDT fallback path.

Both eRPM sources are logged. Neither dominates the other: bidirectional DShot
wins on rate, KISS wins on resolution above roughly 5,000 RPM on a 12–16 pole
motor. See [kiss-telemetry.md](kiss-telemetry.md#erpm-which-source-is-actually-finer).
`eRPMkiss[0]` updates at ~50 Hz, so under a `PREVIOUS` predictor it costs one
byte per P frame for the ~19 frames in 20 where it has not changed.

Settled: both sources are logged separately. It costs about two bytes a frame
and it means the KISS decoder can be checked against EDT from the log itself
rather than trusted. The UI shows the merged value with a source tag; the log
keeps them apart.

## Rate and volume

Sizing before building, because SD write throughput is the constraint that
decides whether this works at all.

At 1 kHz with ~9 fields, P frames dominate and run roughly 10–20 bytes once the
predictors are doing their job. Call it 20 kB/s, ~72 MB/hour. Comfortable for
any card.

But 1 kHz is the DShot rate, not a rate anything here changes at. KISS arrives
at 50 Hz, the display refreshes far slower, and eRPM is the only genuinely
1 kHz signal. Proposal: default 500 Hz, configurable, with an I frame every 32
P frames (Betaflight's `I interval` is typically 32).

## Buffering and where it runs

The failure mode to design against: an SD card stalls for tens of milliseconds
during an internal erase, and any code that blocks on it for that long breaks
whatever else it was doing.

- Encode into a ring buffer; flush in 512-byte sector-aligned chunks.
- Never write from core1. Core1's job is DShot timing and a 30 ms SD stall
  would destroy it.
- Core0 owns the card. Core0 already tolerates multi-millisecond display DMA,
  and the UI is not real-time critical.
- Pre-allocate the file with `f_expand()` so FAT updates do not land
  mid-flight.
- Drop-and-count on buffer overrun rather than blocking. A gap in the log is
  recoverable; a stalled UI with a live motor is not. Surface the drop count —
  a silently lossy log is worse than an obviously lossy one.
- Ring buffer sized for the worst observed stall. Needs measuring; 8–16 kB is
  the starting guess.

## Lifecycle

- Card detected at boot; absent card is not an error, logging is just
  unavailable and the UI says so.
- Files `LOG00001.BFL` … , first free number. Betaflight uses `.BFL` for
  single logs and `.BBL` for concatenated; single-log files are simpler.
- Start/stop from the UI. Auto-start on arm is a candidate once manual works.
- On stop: `E` frame with `LOG_END` (255), flush, close. An unclosed file from a
  power cut should still be parseable up to the last complete frame — worth an
  explicit test, since yanking power mid-write is the normal way a bench session
  ends.

## Testing

The encoder is pure and belongs under host test. The strong test is not a
hand-written expectation of the bytes — it is whether the real decoder accepts
them.

1. Unit tests for the primitives: unsigned VB, signed VB zig-zag, TAG8_8SVB,
   boundary values (0, 1, 127, 128, 16383, 16384, INT32_MIN/MAX).
2. Predictors: increment, straight line, previous.
3. A generated log written to a temp file, then run through `blackbox_decode`
   in CI, asserting zero corrupt frames and that decoded values round-trip to
   the inputs. This is the test that catches the mistakes that matter.
4. Truncated file: cut the last frame in half, assert the decoder still reads
   everything before it.
5. SD layer behind a small interface so the host build writes to a file and the
   device writes to a card, keeping the encoder hardware-free.

`blackbox_decode` in CI means either building `blackbox-tools` from source in
the workflow or vendoring a binary. Building from source is slower but honest.

## Work breakdown

1. `src/blackbox_encode.h` / `.cpp` — VB/zig-zag/tag primitives, header
   emission, I and P frame assembly, predictor state. No hardware.
2. Host tests for the primitives.
3. CI step: build `blackbox-tools`, generate a log, decode it, assert clean.
4. `src/log_ring.h` — the buffer the encoder writes into; drained to the card
   by `sd_log.cpp`, or to a file in the host tests.
5. `src/sd_log.cpp` — card init, pre-allocation, ring buffer, chunked flush,
   overrun counting.
6. Field table wired to `EscTelemetry` + throttle.
7. UI: card status, start/stop, bytes written, drops. *Done: CFG -> SD LOG,
   plus a REC indicator in the status bar.*
8. `config.h`: log rate, I interval, buffer size, field selection.
9. Hardware bring-up: measure real throughput and worst-case stall; size the
   buffer from that rather than the guess above.
10. README section.

## Bring-up

The first hardware attempt never reached the code. Every card reported FatFs
`FR_NOT_READY` at every clock rate from 12 MHz down to 200 kHz, and
`tools/sdtest` found why: **GPIO27, the CMD line, is shorted to ground.** It
cannot be driven high push-pull with the slot empty, so the card can never
receive a command, and the driver's trace shows CMD0 retried and unanswered
while MISO idles correctly at 0xff.

Ruled out along the way, none of it worth repeating: the pin map (confirmed
against the schematic netlist), the SPI clock (all five rates fail identically),
exFAT support (enabled), SDXC support (HCS/CCS handled), the driver init
sequence (correct 74-clock preamble), and the integration itself (`tools/sdtest`
shares only sd_hw_config.c with the firmware and fails the same way). It is not
RP2350-E9 either -- that erratum latches pins high, this one is stuck low.

So the encoder, ring buffer, writer and UI remain unproven against real media.
The numbers that still need measuring are BUF PEAK and WORST FLUSH, which size
`SD_LOG_BUFFER_BYTES`; 8 kB is still a guess.

## Open questions

- Is a `S` (slow) frame worth it for temperature and status, or is the saving
  too small to justify the extra frame type? Still open, and minor.
- Does anything downstream care that `Firmware type` claims Cleanflight? That is
  what Betaflight itself writes, so probably not, but it is a lie worth knowing
  we are telling.

## Sources

- [`betaflight/blackbox-tools`](https://github.com/betaflight/blackbox-tools) — `src/parser.c` and `src/blackbox_fielddefs.h`, the definitive reader
- [Betaflight blackbox source](https://github.com/betaflight/betaflight/tree/master/src/main/blackbox) — the reference writer
- [`carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico) — FatFs over SPI, RP2350-capable
