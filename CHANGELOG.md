# Changelog

Notable changes to DshotDisplay, a tester for one ESC (electronic speed
controller) driven by bidirectional DShot, a digital ESC protocol in which the
ESC answers each frame on the same wire. The format follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/). Commit-level
history is in git; [STATUS.md](STATUS.md) records what is verified.

Each section covers `git log <previous tag>..<tag>`. A date is the tag's own:
the tagger date for an annotated tag, the commit date for a lightweight one.
The annotated tags v1.0.0 and v1.0.1 carry the tagger date 2026-08-01, five
days before the commits they point at (2026-08-06). The pre-release tags
pre-release-001 and pre-release-002 point at commits inside 1.0.0 and are
folded into it.

Issue and pull request numbers in the released sections are the ones the
commit messages give. The current GitHub tracker starts again at issue #1, so
those numbers, #1 to #25, do not resolve there. Issue #1 under Unreleased is
the current tracker's.

Acronyms used throughout: EDT is Extended DShot Telemetry, eRPM is electrical
revolutions per minute, RPM is revolutions per minute, SD is Secure Digital,
USB is Universal Serial Bus, UI is user interface, CI is continuous
integration, UF2 is the USB Flashing Format, PIO is the RP2350's programmable
input/output, SPI is Serial Peripheral Interface, SDIO is Secure Digital
input/output, I2C is Inter-Integrated Circuit, GPIO is general-purpose
input/output, CRC is cyclic redundancy check, UART is universal asynchronous
receiver-transmitter, SDK is software development kit, PR is pull request,
DMA is direct memory access, CSD is the card-specific data register. SDSC,
SDHC and SDXC are the standard, high and extended capacity SD card types.
KISS telemetry is the serial telemetry protocol of the KISS ESC line, sent on
its own wire. AM32 is an open-source ESC firmware.

## [Unreleased]

### Added

- [STATUS.md](STATUS.md): architecture, boards, verification state of each
  part, open items with issue #1, known limitations, tests and CI.
- This changelog.
- `make -C test coverage` and `tools/coverage.py`: host-suite line coverage
  in README.md and a per-file table in STATUS.md. The CI job Coverage fails
  on a stale figure, below 93 % in total or below 85 % in any file.
- `.github/workflows/docs.yml` publishes `wiki/` to the GitHub wiki after
  every successful Release run, from a tag push or started by hand, and on
  request for a named tag.
- `test/check_docs.py` checks that every relative link in STATUS.md,
  CHANGELOG.md and `docs/design/*.md` points at a file that exists, and
  that an anchor into a Markdown file points at a heading that exists.
- `DSERR` in amber in the status bar when the DShot driver cannot claim its
  two PIO state machines. Nothing is sent to the ESC then.
- Host tests for the DShot core: the 36 cases of rcbench for the frame, the
  reply and the EDT enable schedule, and new cases for the three-state poll
  and the counters it feeds. The suite compiles the core as C11.
- An SDIO card driver in `src/sdio/`, MIT, written from the SD Physical Layer
  Simplified Specification v9.10 and the RP2350 datasheet
  ([design note](docs/design/sdio-driver.md)). Two PIO programs fill pio1, 13
  and 19 instructions on 2 state machines, with 2 DMA channels and no
  interrupt. It initialises SDSC version 1, SDSC version 2
  and SDHC/SDXC cards, reads and writes single and multiple blocks, and ends
  every wait with a timeout and an error code. Not tested on hardware.
- Host tests for the SDIO driver (`runSdioTests`): CRC7 and CRC16 against the
  specification's examples, command framing, response parsing, CSD capacity,
  four-bit packing, the write CRC status token, and initialisation, reads and
  writes against a simulated card, with no card, voltage mismatch, timeouts
  and removal.
- CI checks that no compile unit of an image comes from the SD library's
  SDIO, ZuluSCSI-firmware or SdFat directories.

### Changed

- Bidirectional DShot runs on the MIT driver from rcbench, `protocols/dshot`
  at commit 453cf334, kept in `src/dshot/`, instead of `pico-bidir-dshot`
  1.0.2, which is GPL-3.0. No GPL code is fetched or linked for DShot.
- The DShot poll tells a good reply, no reply and a damaged reply (line code,
  group code or checksum) apart. The good, bad and missing packet counts and
  the error percentage keep their meaning.
- The DShot transmitter and receiver take two adjacent state machines and 16
  instructions on pio2, beside the KISS receiver; DShot was on pio0. pio1
  stays the SDIO driver's.
- eRPM is 60,000,000 divided by the reported period in µs.
  `pico-bidir-dshot` added 50 eRPM to every reading, about 7 RPM on a
  14-pole motor.
- EDT frames are decoded as EDT once an enable has gone out. Before that,
  every reply reads as eRPM.
- The design notes in `docs/design/` describe the code as it is: KISS
  reception through a PIO receiver on any free GPIO, KISS on the 2.8" board
  off by default rather than impossible, the SD interface chosen per board at
  run time, which log fields carry which source, and the USB card reader.
- README.md links STATUS.md and this changelog, and credits the
  collaboration.
- README.md and every wiki page, English and German, describe the firmware
  in the present tense with values from `src/`. Statements that disagreed
  with the code are corrected, among them the AM32 status strings, the
  backlight range of 16 to 255, the 34 px throttle gauge and the log fields.
- The wiki is published by `docs.yml` instead of a job inside `release.yml`.
- `actions/cache` v6 (Node.js 24) in place of v4 (Node.js 20).
- The workflow lint's duplicate-key check covers every workflow file.
- The 2.8" board's SD card runs on the driver in `src/sdio/` instead of the
  SDIO driver of `no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`, whose README derives it
  from ZuluSCSI-firmware (GPL-3.0). The build compiles FatFs, the glue and the SPI driver from a copy of the
  library's `src/` without its SDIO, ZuluSCSI-firmware and SdFat directories,
  not through the library's CMakeLists.txt. No GPL code is compiled or linked into any image. The 2.0"
  board's SPI path is unchanged.
- The SDIO pins need DAT0 to DAT3 consecutive and nothing more; CLK and CMD
  can be any other GPIO below 32.

### Fixed

- The status bar's DShot label showed the compiled default, `DS600`, at any
  bitrate set under SETUP. It shows the bitrate the pump runs at.
- `test/check_docs.py` run from any directory other than the repository root
  checked no files and reported success. It now resolves its paths from its
  own location and fails when it finds no files.
- `test/check_docs.py` checks links with single-quoted titles, angle-bracket
  targets and parentheses in the path, skips footnotes, and builds heading
  anchors from the rendered heading text.
- `test/check_docs.py` strips `~~~` code fences as well as backtick fences,
  builds heading anchors without `_` and `*` emphasis markers, and caps its
  exit status at 255, so a multiple of 256 problems does not exit 0.
- The release notes describe the board probe as it runs: on every boot,
  shown read-only under CFG → SETUP. They give GP28 as the KISS pin of the
  2.8" board, off by default.
- The manual and README.md list two USB DRIVE refusals. Without a card the
  button is drawn unavailable, so `NO CARD TO SHARE` cannot appear.
- The manual gives `vbatLatest` and `amperageLatest` as KISS only, 0 while
  KISS is not live, as `src/sd_log.cpp` writes them.
- Three comments in `src/config.h` disagreed with the code: the SD
  preallocation of 16 MiB lasts about 38 min, SETUP shows one note line for
  saved settings or compiled defaults, and the card interface differs per
  board.
- `tools/sdtest` links: it carries the mass-storage callbacks that the
  firmware's TinyUSB configuration requires, and declares
  `spi_get_by_num()`.

## [2.3] - 2026-08-26

Merges PR #25.

### Changed

- The throttle gauge is 34 px tall instead of 26 px. The grab margin under
  the bar is 1 px instead of 8 px, so the throttle's touch area stops short
  of the button row. The buttons stay at y = 283. Asked for in #24.

### Fixed

- Both ends of the throttle gauge are reachable on the 2.0" board. In spring
  (absolute) mode the outer 16 px at each end saturate and the 192 px between
  them span zero to the ceiling. Before, a 20 % ceiling stopped at 19 % and
  60 % at 58 %, because the end columns lie under the case rim (#22). HOLD
  mode is unchanged.
- A control drawn as unavailable no longer acts (#23): KISS PIN on SETUP with
  KISS off, START and USB DRIVE on SD LOG without a card, REVERT on the AM32
  screen with nothing changed. Unavailable controls are drawn as an outline
  without fill.

## [2.2] - 2026-08-17

Merges PR #20 and PR #21. The USB card reader comes from
`feat/usb-mass-storage`.

### Added

- USB DRIVE on CFG → SD LOG hands the card to the USB host as a read-only
  drive. The board is a composite device: serial port and card reader on one
  cable. The reader reports no medium until USB DRIVE is pressed. The logger
  unmounts first and touches nothing until the host ejects. Writes are
  refused with a write-protect error. The press is refused while armed or
  recording, with the reason shown for 4 s. Arming is refused while a host
  holds the card. The screen shows transfer progress.
- The RPM readout is smoothed by an integer moving average that settles in
  about 0.2 s (`RPM_FILTER_SHIFT` 3). It starts from the first reading, not
  from zero, and resets when the link goes stale. Display only: the log stays
  unfiltered. `RPM_FILTER_SHIFT` 0 turns it off.

### Fixed

- The AM32 editor reads timing advance in both encodings AM32 uses: 0 to 3 in
  7.5° steps (configurators before 1.90) and 10 to 42 in 0.9375° steps (1.90
  and later). AM32's default byte of 26 showed as 195°, and the first edit
  clamped it into the old range. Edits now write the 1.90 encoding.
- The manual's troubleshooting page no longer says KISS telemetry cannot be
  enabled on the 2.8" board. It is off there by default and one tap turns it
  on.

## [2.0] - 2026-08-15

Merges PR #3, #7, #9, #10, #11, #15, #16 and #17, and the branch
`test/multi-board-support`. The SD card writer comes from the native Pico SDK
port on `dev/pico-sdk-native`. The SD LOG screen, the KISS readout and the
2.8" SDIO path come from `dev/multiboard-sdio`, the PIO KISS receiver from
`feat/runtime-config`.

### Added

- Support for the Waveshare RP2350-Touch-LCD-2.8 beside the 2.0" board: CST328
  touch on I2C1, panel on SPI1, power latch on GP26 asserted at boot. ESC on
  GP29 and KISS on GP28 by default.
- A unified image that runs on either board. Each boot it identifies the
  board by I2C acknowledges from devices that are always powered. When
  neither or both buses answer, it holds a safe state: latch held, no
  display, no DShot. The board is neither stored nor chosen. Releases carry
  three images: `touch-lcd-2`, `touch-lcd-2.8` and `unified`.
- Blackbox logging to the microSD card in Betaflight's format, as
  `LOGnnnnn.BFL`. 500 Hz frames, an I frame every 32, 12 fields: both eRPM
  sources, KISS and EDT voltage and current in separate fields, temperature,
  consumption and stress. 8 kB ring buffer that drops and counts frames
  rather than blocking, 512-byte card writes, 16 MiB pre-allocated per file,
  directory entry synced every 2 s. Logging starts and stops with arming;
  START and STOP work by hand. The 2.0" board uses hardware SPI at 12 MHz,
  the 2.8" four-bit PIO SDIO at 10 MHz.
- CFG → SD LOG: card state, FatFs result and card type when a mount fails,
  RETRY MOUNT, file, frames, kB written, dropped frames, BUF PEAK against the
  buffer size (amber above half, red above three quarters) and WORST FLUSH.
  A REC indicator in the status bar.
- KISS values on the main screen. Voltage, current and temperature tiles show
  hundredths and a KISS or EDT tag. Consumption appears only with KISS.
- CFG → SETUP: ESC pin, DShot bitrate, KISS on or off and its pin, contrast
  and backlight, with the ESC link state shown live. Pins step only through
  the board's free GPIOs and skip the pin the other wire holds. Settings are
  stored in a flash sector, versioned and CRC-checked, saved with a 1 s hold,
  refused while armed. A block that fails a check is discarded whole.
- A high-contrast theme for outdoor use.
- Touch feedback: buttons fire on release and cancel when the finger slides
  off; DISARM fires on touch-down. Pressed state on every button. Steppers
  repeat and accelerate while held. The backlight dips for 120 ms on arm and
  disarm. The idle disarm counts down its last 5 s. The throttle pad shows
  SWIPE ACTIVE once a drag engages.
- An EDT ON / EDT OFF chip in the settings title bar, driven by arriving EDT
  frames.
- A user manual in `wiki/`, in English and German, published to the GitHub
  wiki on each release.
- `tools/sdtest`, a stand-alone SD bring-up firmware with a pin integrity
  check. Not released.
- CMake options for the per-board pin defaults (`DSHOT_PIN_LCD_2`,
  `KISS_PIN_LCD_2`, `DSHOT_PIN_LCD_2_8`, `KISS_PIN_LCD_2_8`). An override
  outside the board's free GPIOs, or both wires on one pin, fails the build.

### Changed

- The firmware builds with the native Pico SDK 2.1.1 and CMake instead of
  Arduino. Images are built for the ARM cores only, in Release mode, and are
  named `DshotDisplay-<tag>-<board>.uf2`.
- The KISS receiver is a UART built from one PIO state machine instead of
  UART1. Any free GPIO can receive. KISS is on by default on the 2.0" board
  (GP5) and off on the 2.8" (GP28).
- EDT is enabled automatically. The first enable goes out once the ESC has
  answered eRPM for 300 ms while disarmed and stopped, then every 1 s while
  eRPM arrives without EDT. The enable is 10 consecutive frames and a BEEP
  press no longer cuts it short.
- Telemetry expires instead of holding its last value. An EDT field shows
  `--` 1 s after its last frame. KISS gives way to EDT after 500 ms. A stale
  status block shows `--`, never OK. The log writes zero for stale EDT
  fields.
- BEEP flashes white when accepted and amber when refused, and takes the whole
  button row.
- One button, header and row style on every screen. BACK sits in the header
  at the same place on every screen. Settings rows grew from 38 px to 44 px,
  START and STOP from 34 px to 44 px.
- The AM32 list scrolls by the pixel and follows the finger. A row is
  selected only by a tap that did not move.

### Fixed

- EDT readings stayed on screen after the ESC was unplugged or swapped.
- EDT was enabled only once, 1.5 s after boot, so an ESC connected or
  power-cycled later reported RPM and nothing else.
- EDT and BEEP showed no reaction to a press.
- Text near the bottom edge at scale 2 could write past the framebuffer.
- Some glyph pixels, such as the top row of `0`, were drawn but not sent to
  the panel.
- Opening the AM32 screen before core1 had built the DShot driver waited
  forever.
- A single failed touch read counted as a lift-off and could complete a tap
  under a resting finger. Two consecutive failures are now needed.

### Removed

- The Arduino build (`sketch.yaml`, arduino-cli) and the RISC-V image.
- The ENABLE EDT button, made redundant by the automatic enable.
- The `-DDSHOT_PIN` CMake option, which configured without effect. CMake now
  refuses it and names the per-board options.

## [1.1.0] - 2026-08-11

Tagged on `dev/telemetry-logging`. Merges PR #1 and PR #2.

### Added

- KISS telemetry. The firmware sets the DShot telemetry bit on every 20th
  frame (50 Hz), receives the 10-byte reply on UART1 at 115200 baud, checks
  its CRC-8 (polynomial 0x07) and merges it per field with EDT. In this
  release the KISS values reach no screen and no log.
- A blackbox log encoder, host-tested and checked in CI against Betaflight's
  `blackbox_decode`. Nothing writes to the SD card yet.
- Design notes for KISS telemetry and blackbox logging in `docs/design/`.

### Fixed

- Text clipped at the left edge drew at its unclipped width.

## [1.0.1] - 2026-08-01

### Fixed

- The release workflow rejected every valid UF2 image: it checked for the
  magic bytes `55463200` instead of `5546320a`. The check now covers all
  three UF2 magic numbers and runs on every CI push.

## [1.0.0] - 2026-08-01

The first release, for the RP2350-Touch-LCD-2 only. pre-release-001 marks the
commit that added the host tests and CI; pre-release-002 is the same commit as
v1.0.0.

### Added

- A bench tester: drag the on-screen throttle and the board sends
  bidirectional DShot to one ESC on GP4, decoding eRPM and EDT from the same
  wire. Core0 draws the UI at about 40 Hz; core1 sends frames at 1 kHz and
  zeroes the throttle if core0 stops sending heartbeats.
- Safety interlocks: boots disarmed, 1 s hold to arm with the throttle at
  zero, spring return on lift-off, 20 % throttle ceiling by default, 30 s
  idle disarm, disarm on entering settings.
- An AM32 settings editor over the signal wire: 19200 baud one-wire
  bootloader, 39 fields, writes read back and compared byte by byte.
- A host test suite (60 checks) and CI: firmware images for the ARM and
  RISC-V cores, host tests, 8 build-option permutations with warnings fatal,
  Doxygen.
- A release workflow that refuses failing tests, implausible images, untagged
  builds and incomplete releases.
- Screenshots rendered by the host suite from the UI code; a landscape
  rotation is a compile error.

[Unreleased]: https://github.com/subtilitas/DshotDisplay/compare/v2.3...HEAD
[2.3]: https://github.com/subtilitas/DshotDisplay/compare/v2.2...v2.3
[2.2]: https://github.com/subtilitas/DshotDisplay/compare/v2.0...v2.2
[2.0]: https://github.com/subtilitas/DshotDisplay/compare/v1.1.0...v2.0
[1.1.0]: https://github.com/subtilitas/DshotDisplay/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/subtilitas/DshotDisplay/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/subtilitas/DshotDisplay/tree/v1.0.0
