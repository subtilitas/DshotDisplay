# Where DshotDisplay stands

The running record: what exists, how far each part is verified, and what is
open. Two parts of this file are held to the tree by a checker. Every relative
link here and in [CHANGELOG.md](CHANGELOG.md) is checked by
`test/check_docs.py`. The coverage table under "Tests and CI" (continuous
integration) is written and checked by `tools/coverage.py`. Nothing else is
checked: the architecture figures, pin numbers, suite and job lists and
verification states are written by hand, against the code and the issue tracker
as of 2026-10-16. Read them as a dated record, not as an inventory.

[README.md](README.md) is the technical document, the
[wiki](https://github.com/subtilitas/DshotDisplay/wiki) is the manual,
[CHANGELOG.md](CHANGELOG.md) lists changes by release, and
[docs/design/](docs/design/README.md) holds the design notes.

## Architecture

DshotDisplay is a bench tester for one ESC (electronic speed controller). The
board is the throttle source. It sends bidirectional DShot (a digital ESC
protocol in which the ESC answers each frame on the same wire) and decodes the
eRPM (electrical revolutions per minute) and EDT (Extended DShot Telemetry)
frames that come back; RPM (revolutions per minute) is eRPM divided by half
the motor's pole count. An optional third wire carries KISS telemetry, a serial
protocol from the KISS ESC line with finer voltage and current.

The two cores of the RP2350 split the work by deadline:

| | core0 | core1 |
| --- | --- | --- |
| Work | USB (Universal Serial Bus) device task, touch, UI (user interface) rendering, SD (Secure Digital) card logging, settings, serial telemetry dump | DShot frame pump, eRPM and EDT decode, KISS request and receive |
| Rate | one UI frame every 25 ms (40 Hz); USB task, log encode and card flush on every loop pass | one DShot frame every 1000 µs (1 kHz); KISS receive FIFO (first-in, first-out buffer) drained on every loop pass |
| May block | yes: display DMA (direct memory access) bursts of several ms, card writes of tens of ms | never |

The cores share one struct, `EscTelemetry` in `src/esc_task.h`, guarded by a
critical section. Core0 sends a heartbeat every UI frame. When none arrives for
250 ms (`UI_HEARTBEAT_TIMEOUT_MS`), core1 sends zero throttle on its own.
Writing flash parks core1, because core1 runs from XIP (execute-in-place)
flash, so a settings save is refused while armed.

Rates and defaults, from `src/config.h` and `src/main.cpp`:

- DShot bitrate: 150, 300, 600 or 1200 kbit/s. Default 600 kbit/s
  (`DSHOT_SPEED_KBAUD`), changeable on CFG → SETUP.
- EDT: voltage in 0.25 V steps, current in 1 A steps, temperature in 1 °C
  steps, plus stress and status. A field blanks to `--` 1000 ms after its last
  frame (`EDT_STALE_MS`). The enable command goes out as 10 consecutive frames,
  first once the ESC has answered eRPM for 300 ms while disarmed and stopped,
  then every 1000 ms while eRPM arrives and EDT does not.
- KISS: a request every 20th DShot frame, 50 Hz. The reply is 10 bytes at
  115200 baud with a CRC-8 (cyclic redundancy check, polynomial 0x07). A reply
  incomplete after 10 ms counts as a timeout. A KISS value gives way to EDT
  500 ms after the last good frame (`KISS_STALE_MS`).
- Blackbox log: 500 Hz frames, an I frame every 32 frames, 12 fields. About
  14.5 bytes a frame, 7.3 kB/s, per `config.h`. 8192-byte ring buffer, 512-byte
  card writes, 16 MiB pre-allocated per file, directory entry synced every
  2000 ms.
- Display: 240 × 320 pixel RGB565 framebuffer (16 bits per pixel: 5 red,
  6 green, 5 blue; 153,600 bytes), panel SPI
  (Serial Peripheral Interface) clock 62.5 MHz.
- Serial dump over USB CDC (communications device class) at 10 Hz
  (`SERIAL_TELEMETRY` = 1).
- Safety: arming needs a 1000 ms hold with the throttle at zero. The throttle
  ceiling starts at 400 of 2000 (20 %). Idle disarm after 30 s. Entering the
  settings screens disarms.

PIO (programmable input/output) blocks: three, each with 4 state machines and
32 instruction slots.

| Block | 2.0" board | 2.8" board |
| --- | --- | --- |
| pio0 | free | free |
| pio1 | free | SDIO (Secure Digital input/output) card driver in `src/sdio/`: 2 state machines, 32 of 32 instructions, plus 2 DMA channels and no interrupt (`src/sd_hw_config.c`) |
| pio2 | DShot transmitter and receiver on adjacent state machines, 16 instructions; KISS receiver when KISS is on, 1 state machine, 9 instructions | the same |

The unified image uses the column of the board it detects. DShot and KISS
claim from whichever block has room, and the Pico SDK (software development
kit) searches pio2 first. DShot binds first after every release, so it always
finds a block with no other user. A `static_assert` in `src/esc_task.cpp`
checks at build time that DShot and KISS together fit one block: 3 state
machines and 25 of 32 instructions. A bind that
cannot get two adjacent state machines is refused, and the status bar then
shows `DSERR`.

Board detection. One image can carry both boards (`BOARD_UNIFIED`). Each boot
it asks for I2C (Inter-Integrated Circuit) acknowledges from devices that are
always powered: 0x6B and 0x15 on I2C0 (GP12, GP13) for the 2.0" board; 0x6B,
0x51 and 0x1A on I2C1 (GP6, GP7) for the 2.8" board. Three attempts, 5 ms
apart. Exactly one answering bus selects the board. Otherwise the firmware
holds a safe state: power latch held, no display, no DShot. The board is never
stored and never chosen by the user.

Build: native Pico SDK (software development kit) 2.1.1 with CMake, ARM cores
only. The one dependency besides the SDK is pinned by commit:
`no-OS-FatFS-SD-SDIO-SPI-RPi-Pico` v3.6.2. Its FatFs, glue and SPI driver are
built; its SDIO driver is not. The DShot driver in `src/dshot/` is part of the
tree, from rcbench `protocols/dshot` at commit 453cf334 (MIT, same copyright
holder). The SDIO driver in `src/sdio/` is part of the tree, MIT, written from
the SD Association's Physical Layer Simplified Specification v9.10 and the
RP2350 datasheet ([design note](docs/design/sdio-driver.md)). A release carries three UF2
(USB Flashing Format) images: `touch-lcd-2`, `touch-lcd-2.8` and `unified`.

## Boards

Both are Waveshare boards with an RP2350A, a 240 × 320 ST7789T3 panel and a
QMI8658 IMU (inertial measurement unit). GPIO means general-purpose
input/output.

| | RP2350-Touch-LCD-2 (2.0") | RP2350-Touch-LCD-2.8 |
| --- | --- | --- |
| Panel bus | SPI0: GP18 clock, GP19 data out | SPI1: GP10 clock, GP11 data out |
| Touch | CST816D on I2C0, GP12 and GP13 | CST328 on I2C1, GP6 and GP7 |
| SD card | hardware SPI1, 1 bit, 12 MHz, GP24 to GP27 | PIO SDIO on pio1, 4 bits, 10 MHz, GP19 to GP24 |
| GPIOs free for the two wires | 15: GP0 to GP11, GP21 to GP23 | 2: GP28, GP29 |
| ESC signal default | GP4, P2 pin 11 | GP29, J4 pin 12 |
| KISS default | GP5, P1 pin 10, on | GP28, J4 pin 11, off |
| Battery sense | GP28, ADC (analog-to-digital converter) input 2, 3:1 divider | GP27, ADC input 1, 3:1 divider |
| Power latch | none | GP26, asserted first thing in `setup()` |
| Other devices | none used | PCF85063 RTC (real-time clock), I2S (Inter-IC Sound) audio codec, neither used |

The ESC and KISS pins are changeable on CFG → SETUP within the free set, and
the two wires never share a pin.

## What exists and how far it is verified

States used below. "Hardware-tested (owner report)": the owner reports it
working on hardware. "Hardware use recorded": a commit records use on a board;
the commit is named. "Reported by a user": a third party reports it. "Host
only": covered by the host suite, no hardware record. "Not recorded": no
record either way.

| Part | Host tests | Hardware |
| --- | --- | --- |
| DShot driver in `src/dshot/` (from rcbench 453cf334): frame, reply decode, three-state poll, RP2350 pin driver | frame and checksum, throttle map, line and group code, sampler against clock error, extended telemetry schedule against an AM32 model, three-state poll and the counters it feeds; the pin driver is compiled only | not tested on hardware in DshotDisplay. rcbench ran it at bidirectional DShot600 on one AM32 2.21 ESC (Skystars 35A); other rates and ESC firmwares are untested |
| Bidirectional DShot throttle, eRPM and RPM readout | core1 frame decision, throttle and arming interlocks, RPM smoothing | hardware-tested (owner report) with `pico-bidir-dshot`, the driver before `src/dshot/`; the owner has found no bug since v2.3, and issue #1 is a separate, unreproduced report |
| EDT telemetry, automatic enable, expiry | merge, expiry, enable rule, decode of each frame type | hardware-tested (owner report) with `pico-bidir-dshot` |
| KISS telemetry: request, PIO receiver, decode, merge, KISS/EDT tags | CRC, decode, resynchronisation, request payload, merge; the PIO receiver is typechecked only | hardware-tested (owner report) with `pico-bidir-dshot`; see below |
| DShot bitrate label in the status bar, `DSERR` | label at each bitrate, colour, `DSERR` | not recorded |
| SDIO card driver in `src/sdio/` (2.8" board): protocol, PIO and DMA bus, library callbacks | CRC7 and CRC16 against the specification's examples, command framing, response parsing, CSD (card-specific data register), four-bit packing, CRC status token; initialisation, reads and writes against a simulated card (standard capacity SDSC v1 and v2, high capacity SDHC, no card, voltage mismatch, timeouts, removal); the PIO programs and the bus are compiled only | not tested on hardware; open item 8 lists the bench tests |
| Blackbox logging to SD, CFG → SD LOG screen | encoder, ring buffer, round trip through `blackbox_decode`, screen states | hardware use recorded with the previous SDIO driver on the 2.8" board; see below |
| USB card reader (read-only mass storage) | handover state machine, screen | not recorded |
| AM32 settings editor over the signal wire (AM32 is an open-source ESC firmware) | init string, CRC-16/ARC, 39-field codec, timing advance encodings, write interlock | hardware use recorded: ab79c67, bd90e17 |
| Settings in flash (CFG → SETUP) | block validation, CRC, board re-judging, pin defaults | not recorded; 46a64a7 states no hardware coverage |
| Board detection, unified image | board selection and defaults | not recorded |
| Touch handling, high-contrast theme, throttle gauge | tap on release, repeats, screen residue, gauge reach | hardware use recorded: the reports #22 to #24 named in the v2.3 commits; the fixes on hardware: not recorded |
| Serial telemetry dump | none | not recorded |
| `tools/sdtest`, SD bring-up firmware | none, not built in CI | hardware use recorded on both boards: f16a70f, e9c9d9a, with the previous SDIO driver on the 2.8" board |

KISS in detail. The owner reports KISS telemetry working on hardware, the
display's own telemetry request included. Issue #1 separately reports correct
KISS values with a receiver driving the ESC and the display only listening.
Which board each report covers is not recorded.

SD logging in detail. Commit 036adfe records the 2.8" board mounting a 64 GB
card over SDIO at 25 MHz and writing a file, with the library's SDIO driver
that `src/sdio/` replaces. Nothing is recorded for the current driver. Commit
5b9c7bd and the design note record a replacement 2.0" board working over SPI,
after the first 2.0" board had GPIO27 shorted to ground; the SPI path is
unchanged. Commit 5cec1ec fixes a logging fault reported from hardware. A run
at the shipped SDIO clock of 10 MHz is not recorded separately. BUF PEAK and
WORST FLUSH under sustained logging are not recorded.

Not on `main`: the branches `feat/usb-bridge` (a USB to one-wire bridge,
6693084) and `copilot/create-lcars-display` (a themed UI, 5d17541) are not
merged.

## Open items

1. **Issue #1, "Initialisation fails on some BL_Heli32 ESC".** Opened
   2026-09-11 by DerGeheimdienst. Open, no comments, not reproduced by the
   owner. This is an external report, not a confirmed defect. Conditions as
   reported:
   - ESCs: RAZOR32 and EXUAV 70A, both running BLHeli_32 32.6.
   - Signal wire on the display: the ESC does not initialise, and its start-up
     sounds lack the zero-throttle beep.
   - The reporter tried every DShot rate and shorter signal cables, with the
     same result.
   - Racerstar, T-Motor and Flycolor ESCs work. Their firmware versions are not
     given.
   - Signal wire on a receiver, KISS wire and GND (ground) on the display:
     the ESC arms from the transmitter and the display shows correct
     telemetry.

   Facts from the code that bear on it: the firmware drives only bidirectional
   DShot and has no plain DShot mode. While disarmed it sends DShot value 0
   every 1000 µs, with the telemetry bit set on every 20th frame when KISS is
   on. The EDT enable goes out only after the ESC answers eRPM.
2. **Ring buffer size.** `SD_LOG_BUFFER_BYTES` is 8192, unmeasured. At
   7.3 kB/s it holds about 1.1 s. A card stall longer than that drops frames,
   which DROPPED FRAMES counts. BUF PEAK and WORST FLUSH on the SD LOG screen
   are the numbers to size it from.
3. **KISS line voltage.** The KISS specification puts the line at 3.6 V, the
   RP2350's absolute maximum GPIO voltage. No measurement is recorded. The
   firmware enables the internal pull-up on the KISS pin; a 1 kΩ series
   resistor is suggested in `config.h`.
4. **GP26 at boot on the 2.0" board.** The unified image drives GP26 high
   before it knows the board, because GP26 is the power latch on the 2.8".
   On the 2.0" GP26 is the SD clock. `main.cpp` calls this reasoned, not
   measured.
5. **Settings flash and the USB card reader on hardware.** Not recorded.
6. **Design questions left open** in [kiss-telemetry.md](docs/design/kiss-telemetry.md)
   and [blackbox-logging.md](docs/design/blackbox-logging.md): which eRPM source
   the main readout uses above the crossover (about 5,500 RPM on a 14-pole
   motor), whether consumption is shown from ESC power-on or from arming,
   whether a slow frame type is worth adding.
7. **`tools/sdtest` is built by nothing in CI**, so a change that breaks it
   shows only in a local build.
8. **SDIO driver on hardware.** The driver in `src/sdio/` is not tested on
   hardware. Bench tests needed on the 2.8" board:
   - initialisation on an SDSC card (2 GB or less) and on SDHC and SDXC
     (extended capacity) cards;
   - sustained logging at 7.3 kB/s, with BUF PEAK and WORST FLUSH read off the
     SD LOG screen;
   - USB DRIVE: a host reads files, which takes multi-block reads;
   - the card pulled out while logging, while idle and while a host reads,
     then put back;
   - 10 MHz in the firmware, and 10 MHz and 25 MHz in `tools/sdtest`.
9. **Extended frames before the first enable.** The DShot decoder reads EDT
   frames as such only once an enable has gone out. An ESC that kept EDT on
   across a board restart or a SETUP pin change sends EDT frames that read as
   eRPM until the next enable, which goes out 300 ms after eRPM arrives with
   the motor stopped. Not observed on hardware.

## Known limitations

- **2.8" pins.** That board frees two GPIOs, GP28 and GP29. The ESC takes one
  and KISS the other, so there is no third choice and the SETUP steppers stay
  put. KISS is off there by default and one tap on CFG → SETUP turns it on. A
  build-time pin override outside a board's free set, or both wires on one pin,
  fails the build.
- **Bidirectional DShot only.** An ESC that does not accept the inverted
  bidirectional signal is not driven.
- **Saved settings outrank `config.h`.** On a board with saved settings, a
  changed default in `config.h` has no effect after reflashing. SETUP marks the
  whole block, not each row: while no change is pending, its note line reads
  `SAVED SETTINGS IN USE` or `COMPILED DEFAULTS IN USE`.
- **Power loss during a log.** A log is readable up to its last directory
  sync, so pulling power loses up to 2000 ms (`SD_LOG_SYNC_MS`).
- **Logs past the pre-allocation.** 16 MiB lasts about 38 minutes at
  7.3 kB/s. Past that, or when `f_expand()` finds no contiguous space, FatFs
  allocates clusters as the file grows. The stall that causes is not measured,
  and a failed pre-allocation is not reported.
- **USB DRIVE.** Read-only. Refused while armed or recording, with the reason
  shown for 4000 ms. Arming is refused while a host holds the card.
- **Consumption.** KISS mAh (milliampere-hours) counts from ESC power-on, not
  from arming. EDT carries no consumption.
- **Log field names.** `eRPM[0]` and `escTemperature[0]` follow Betaflight's
  naming but are not in `blackbox_decode`'s list of known fields; `eRPMkiss[0]`,
  `vbatEdt`, `amperageEdt`, `escConsumption` and `escStress` are new names.
  Blackbox Explorer draws only the fields it knows. The header claims
  `Firmware type:Cleanflight`, as Betaflight's own logs do.
- **Board probe failure.** When neither or both buses answer, the screen stays
  dark and nothing is driven. Recovery is a reflash over USB.
- **Landscape.** `LCD_ROTATION` 1 fails the build.

## Tests and CI

**Host suite.** `make -C test run` compiles the firmware sources with a C++17
compiler against the stubs in `test/stubs/`, and the DShot core in
`src/dshot/` and the SDIO protocol layers in `src/sdio/` as C11, and runs one
binary, `dshot_tests`. `test/test_main.cpp` runs twelve suites:

| Suite | File | Covers |
| --- | --- | --- |
| `runAm32Tests` | `test_am32.cpp` | AM32 init string, CRC-16/ARC, settings address per MCU (microcontroller) family, field codec, timing advance |
| `runUiTests` | `test_ui.cpp` (includes `ui_am32.cpp`) | screens, throttle modes, safety interlocks, core1 frame decision, EDT enable, touch, SETUP, SD LOG, USB DRIVE, screenshots |
| `runKissTests` | `test_kiss.cpp` | KISS CRC-8, decode, resynchronisation, request payload, KISS/EDT merge, expiry, EDT enable rule |
| `runGfxTests` | `test_gfx.cpp` | dirty bands, glyph rendering, clip rectangle |
| `runBlackboxTests` | `test_blackbox.cpp` | variable-byte and zig-zag encodings, header, frames, time prediction |
| `runLogRingTests` | `test_log_ring.cpp` | ring buffer, all-or-nothing writes, wraparound, high-water mark |
| `runSettingsTests` | `test_settings.cpp` | settings block, validation, board selection, pin defaults |
| `runUsbMscTests` | `test_usb_msc.cpp` | card handover state machine, RPM smoothing |
| `runDshotFrameTests` | `test_dshot_frame.cpp` | DShot frame layout, checksum, inverted checksum, EDT enable word, throttle map (from rcbench) |
| `runDshotTelemTests` | `test_dshot_telem.cpp` | line and group code, eRPM and EDT decode, checksum against single-bit errors, sampler (from rcbench); three-state poll, counters |
| `runDshotEdtTests` | `test_dshot_edt.cpp` | extended telemetry schedule against an AM32 model (from rcbench) |
| `runSdioTests` | `test_sdio.cpp` | SDIO driver: CRC7 and CRC16 vectors, framing, responses, CSD, four-bit packing, CRC status token; initialisation, reads and writes against a simulated card |

Other targets in `test/Makefile`: `both` runs the suite for the 2.0", 2.8" and
unified configurations; `syntax` typechecks the device-only sources on all
three with `-Werror`; `bbcheck` encodes 2000 frames and decodes them with
Betaflight's `blackbox_decode`; `previews` and `wiki` rebuild screenshots;
`coverage` builds an instrumented suite. Scripts: `test/check_docs.py`,
`test/check_uf2.sh`, `test/check_uf2_selftest.sh`, `test/check_blackbox.py`,
`tools/check_wiki.py`, `tools/coverage.py`.

**CI** (`.github/workflows/ci.yml`; on push to `main` or `master`, on pull
requests, by hand):

| Job | What it does |
| --- | --- |
| Firmware, three legs (`touch-lcd-2`, `touch-lcd-2.8`, `unified`) | builds with arm-none-eabi-gcc 13.3 and `STRICT=ON`; flash under 1 MiB and RAM (random-access memory) under 400,000 bytes; checks from the DWARF line table that the image holds its own touch driver; checks from the DWARF compile units that no file of the SD library's SDIO, ZuluSCSI-firmware or SdFat directories is in the image; checks pin overrides reach CMake and `-DDSHOT_PIN` is refused; validates the UF2 |
| Host tests | `make both`; the unified suite again with all four pin macros overridden; wiki screenshots and `check_wiki.py` |
| Blackbox round-trip | builds `blackbox_decode` from source, runs `make bbcheck` |
| Config permutations | typechecks all of `src/` with `-Werror` for 64 combinations: 2 boards × AM32 transmit mode × AM32 low-jump × rotation 0 or 2 × KISS on or off × SD logging on or off; checks the landscape guard fires |
| Workflow lint | actionlint 1.7.7 and a duplicate-key check on every workflow in `.github/workflows/` |
| Docs | `test/check_docs.py`, then Doxygen with zero warnings allowed |
| Coverage | `make -C test coverage`, then `python3 tools/coverage.py --check`; fails on drift from the table below, below 93 % in total, or below 85 % in any file |

**Release** (`.github/workflows/release.yml`; on `v*` tags, by hand): resolves
the tag, builds the three images after the host tests and `check_docs.py`
pass, publishes them with checksums and confirms every asset landed. Each run
is titled `Release <tag>`, which Docs reads to find the tag.

**Docs** (`.github/workflows/docs.yml`): mirrors `wiki/` to the GitHub wiki
after every successful Release run, from a tag push or started by hand, and
can be started by hand itself. The wiki is
published per release on purpose, so it describes firmware that can be
downloaded.

Not covered by any job: hardware behaviour, `tools/sdtest`, and the PIO
programs beyond compilation.

<!-- coverage-table:start -->
| File | Lines | Covered | Coverage |
| --- | ---: | ---: | ---: |
| `src/am32_eeprom.cpp` | 69 | 67 | 97.1% |
| `src/am32_eeprom.h` | 11 | 11 | 100.0% |
| `src/blackbox_encode.cpp` | 113 | 113 | 100.0% |
| `src/board_desc.cpp` | 13 | 12 | 92.3% |
| `src/dshot/dshot_edt.c` | 33 | 33 | 100.0% |
| `src/dshot/dshot_frame.c` | 25 | 25 | 100.0% |
| `src/dshot/dshot_telem.c` | 143 | 143 | 100.0% |
| `src/esc_merge.cpp` | 44 | 44 | 100.0% |
| `src/esc_task.h` | 56 | 56 | 100.0% |
| `src/gfx.cpp` | 201 | 186 | 92.5% |
| `src/kiss_telem.cpp` | 37 | 35 | 94.6% |
| `src/log_ring.cpp` | 43 | 39 | 90.7% |
| `src/plat.h` | 5 | 5 | 100.0% |
| `src/rpm_filter.h` | 17 | 17 | 100.0% |
| `src/sd_log.h` | 4 | 4 | 100.0% |
| `src/sdio/sd_host.c` | 180 | 180 | 100.0% |
| `src/sdio/sd_proto.c` | 135 | 135 | 100.0% |
| `src/settings.cpp` | 128 | 126 | 98.4% |
| `src/theme.cpp` | 11 | 11 | 100.0% |
| `src/ui.cpp` | 608 | 602 | 99.0% |
| `src/ui_am32.cpp` | 347 | 316 | 91.1% |
| `src/ui_input.cpp` | 25 | 25 | 100.0% |
| `src/ui_setup.cpp` | 188 | 181 | 96.3% |
| `src/ui_widgets.cpp` | 43 | 43 | 100.0% |
| `src/usb_msc.h` | 37 | 35 | 94.6% |
| **Total** | **2516** | **2444** | **97.1%** |

Generated by `tools/coverage.py`. CI runs it with `--check` and fails on drift.
<!-- coverage-table:end -->
