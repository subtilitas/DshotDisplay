# DshotDisplay

[![CI](https://github.com/subtilitas/DshotDisplay/actions/workflows/ci.yml/badge.svg)](https://github.com/subtilitas/DshotDisplay/actions/workflows/ci.yml)

A self-contained bidirectional DShot ESC tester for the **Waveshare RP2350-Touch-LCD-2**
and **RP2350-Touch-LCD-2.8**.

Drag the on-screen throttle, and the board sends bidirectional DShot to a single ESC
while decoding the eRPM and Extended DShot Telemetry (EDT) that comes back on the same
wire — RPM, voltage, current, ESC temperature, stress and status — all rendered on the
touch panel. No flight controller, no laptop, no Betaflight.

![UI preview](docs/ui-preview.png)

*Screens are rendered by the host test suite from the real UI code, so they cannot drift
away from what the board shows.*

---

## Why it's built this way

Bidirectional DShot is half-duplex: the ESC only answers a frame that *you* sent, and it
disarms if frames stop arriving. That means a telemetry display can't be a passive
sniffer — it has to be the throttle source. So this is a bench tester, not a tap.

Frame timing is the whole game. The display does multi-millisecond SPI DMA bursts, which
would wreck DShot timing if they shared a core. So:

| | core0 | core1 |
|---|---|---|
| Job | touch, rendering, battery sense | DShot frame pump + telemetry decode |
| Rate | ~40 Hz | 1 kHz (`DSHOT_PERIOD_US`) |
| Allowed to block | yes, freely | never |

The two cores share one small `critical_section`-guarded struct. Core0 must call
`escHeartbeat()` every frame; if it stops for `UI_HEARTBEAT_TIMEOUT_MS`, core1 assumes the
UI has hung and forces throttle to zero on its own.

---

## Hardware

Two boards are supported. Both are an RP2350A with 16 MB of flash driving the same
240×320 IPS panel (ST7789T3 over SPI), so everything above the driver layer is shared:

| | [RP2350-Touch-LCD-2](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2) | [RP2350-Touch-LCD-2.8](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2.8) |
|---|---|---|
| Panel | 2.0", 240×320 | 2.8", 240×320 |
| Panel SPI | **SPI0** | **SPI1** |
| Touch | **CST816D**, 0x15, 8-bit regs | **CST328**, 0x1A, 16-bit regs |
| Touch bus | **I2C0** (`Wire`) | **I2C1** (`Wire1`) |
| Touch reset | shared net with `LCD_RST` | its own pin |
| Other peripherals | QMI8658 IMU, microSD, camera header | QMI8658 IMU, PCF85063 RTC, PCM5101A audio, microSD |
| Free GPIO | GP0–GP11, GP21–GP23 (camera bus) | **GP28 and GP29, and that's all** |
| ESC signal pin | **GP4** | **GP28** |

The 2.8" board spends on peripherals what the 2.0" board leaves on a camera header, which
is why it has exactly two spare pins. Everything else about the firmware is identical.

Which board a build targets is `BOARD` in `src/board.h`; see **Choosing a board**
under Building.

### Pin map — RP2350-Touch-LCD-2 (from the official schematic)

| Function | GPIO | Notes |
|---|---|---|
| LCD DC / CS / SCK / MOSI / RST | 16 / 17 / 18 / 19 / 20 | SPI0 |
| LCD backlight | 15 | NPN low-side, PWM at 20 kHz |
| Touch + IMU I2C SDA / SCL | 12 / 13 | I2C0, **shared bus** (touch 0x15, IMU 0x6B) |
| Touch INT | 29 | active low |
| Touch RESET | 20 | **same net as LCD_RST** — resetting one resets both |
| Battery sense | 28 | ADC2, 200k/100k divider → `VBAT = Vadc × 3` |
| microSD | 24–27 | SPI1, unused here |
| Camera bus | 0–11, 21–23 | **free if no camera is fitted** |

### Pin map — RP2350-Touch-LCD-2.8 (from the official schematic)

| Function | GPIO | Notes |
|---|---|---|
| LCD SCK / MOSI / MISO | 10 / 11 / 12 | **SPI1**; MISO is wired but never read |
| LCD CS / DC / RST | 13 / 14 / 15 | RST drives the panel only |
| LCD backlight | 16 | NPN low-side, PWM at 20 kHz |
| Touch + IMU + RTC I2C SDA / SCL | 6 / 7 | **I2C1**, shared (touch 0x1A, IMU 0x6B, RTC 0x51) |
| Touch RESET / INT | 17 / 18 | separate nets, both active low |
| Battery sense | 27 | ADC1, 200k/100k divider → `VBAT = Vadc × 3` |
| Battery latch / power button | 26 / 25 | GP26 **must be driven high** to survive on battery |
| microSD | 19–24 | unused here |
| Audio (PCM5101A → APA2068) | 2–4 | I2S, unused here |
| UART | 0 / 1 | on J5 and J4 |
| **Free** | **28, 29** | J4 pins 11 and 12 |

> **The battery latch is not optional.** The power button only holds the P-channel gate
> down while it is pressed; GP26 takes over the latch. `setup()` drives it high as its
> very first act, before serial or anything else, because a board that dies when you let
> go of the button looks like a hardware fault rather than a missing line of firmware.

### Wiring the ESC

Two wires. That's it.

| Board | Signal | Ground |
|---|---|---|
| RP2350-Touch-LCD-2 | **GP4** — P2 header, pin 11 | **GND** — P2 header, pin 13 |
| RP2350-Touch-LCD-2.8 | **GP28** — J4, pin 11 | **GND** — J4, pin 1 or 5 |

`DSHOT_PIN` in `config.h` defaults per board and can be overridden with `-DDSHOT_PIN=n`
or by editing it. On the 2.0" board any camera-bus pin (GP0–GP11, GP21–GP23) is free; on
the 2.8" board the only other choice is GP29.

#### On the 2.8" board there are no 2.54 mm headers

Everything comes out on JST-SH 1.0 mm connectors, so an ESC lead needs a pigtail whatever
pin you pick — there is no plug-and-go option to optimise for, which is why GP28 was
chosen simply for being unallocated:

```
J4 (12P): 1=GND  2=VBUS 3=USB_N 4=USB_P 5=GND   6=3V3
          7=GP7* 8=GP6* 9=GP0   10=GP1  11=GP28 12=GP29
J3  (4P): 1=GP7* 2=GP6* 3=3V3   4=GND
J5  (4P): 1=GP1  2=GP0  3=3V3   4=GND
                                (* = shared I2C bus, do not reuse)
```

#### On the 2.0" board, header pinout for reference:

```
P1: 1=GP7  2=GP9  3=GP8  4=GP22 5=GP21 6=GP18* 7=GP23
    8=GP11 9=GP6  10=GP5 11=GP20* 12=GP19* 13=GND 14=5V
P2: 1=3V3  2=GND  3=GP28* 4=GP29* 5=GP13* 6=GP12*
    7=GP2  8=GP1  9=GP3  10=GP0 11=GP4  12=GP10 13=GND 14=VBAT
                                        (* = used by an on-board peripheral)
```

### Plugging an ESC in directly (2.0" board)

Solder a 3-pin male header onto **P2 pins 11–13** and a standard servo plug drops straight
on: signal on GP4, middle position on GP10, ground on pin 13.

> **Cut or depin the middle wire first.** The middle conductor of an ESC lead is the BEC
> **+5 V** output, and RP2350 GPIO is **not** 5 V tolerant — anything above ~3.6 V on a pin
> is over absolute-maximum and damages it. The tester only needs signal and ground.

Three positions on these headers put a GPIO two pins from a ground. GP4 is the one to use:

| Candidate | Header run | Why |
|---|---|---|
| **GP4** ✅ | P2 11–13: GP4 / GP10 / GND | Both the signal pin and the skipped middle pin are unused camera-bus pins. Nothing on-board is involved. |
| GP20 ❌ | P1 11–13: GP20 / GP19 / GND | GP20 *is* LCD_RST — DShot on it would hold the panel in reset. The middle position is GP19, the LCD's live SPI MOSI line. |
| GP29 ❌ | P2 4–2: GP29 / GP28 / GND | GP29 is the CST816D's INT **output**. The touch chip would drive the line low on every touch and fight the ESC. The middle position is the battery-ADC divider node. |

There is no run of `[GPIO][5V][GND]` anywhere on either header, so a fully-populated servo
plug can't work regardless of pin choice — 5 V and GND sit adjacent at P1 pins 14 and 13.
Removing the BEC wire is the answer, not a different pin.

**Power the ESC from its own battery.** Do not try to run a motor off the board's 5 V pin.
Keep the ESC ground and the board ground tied together, and keep the signal wire short —
bidirectional DShot at 600 kBaud does not enjoy 30 cm of unshielded flapping servo lead.
If telemetry is flaky, drop `DSHOT_SPEED_KBAUD` to 300 before blaming the firmware.

---

## Building

### Choosing a board

The two boards need different SPI instances, a different I2C instance and a different
touch driver, so the choice is a compile-time constant, not something probed at boot.
It lives in `src/board.h`:

```c
#define BOARD BOARD_RP2350_TOUCH_LCD_2      // or BOARD_RP2350_TOUCH_LCD_2_8
```

Three ways to set it, depending on what is doing the building:

| Building with | How |
|---|---|
| Arduino IDE | Edit that line. The IDE cannot pass `-D` flags. |
| `arduino-cli` | `test/select_board.sh BOARD_RP2350_TOUCH_LCD_2_8`, then compile. Profiles cannot pass `-D` flags either. |
| `test/Makefile`, PlatformIO | `-DBOARD=...`, which the `#ifndef` in `board.h` gets out of the way for. |

`select_board.sh` rewrites the line and then **reads it back**, because a `sed` that
quietly matches nothing is exactly how a CI matrix builds the same board twice and
reports green.

Two compile-time guards back this up: an unrecognised `BOARD` is an `#error` rather than
a fall-through to whichever pin map is listed last, and a board header that selects zero
or two touch drivers is also an `#error`. Both are exercised in CI, so neither is
decorative.

The splash screen prints `BOARD_LABEL`, so a flashed image says which board it is for.
(Not `BOARD_NAME` — arduino-pico puts `-DBOARD_NAME="RPIPICO2"` on every compile line,
so that one is already taken by the core.)

### With the IDE

Arduino IDE with the [arduino-pico](https://github.com/earlephilhower/arduino-pico) core.

1. **Board manager URL** — File → Preferences → Additional boards manager URLs:

   ```
   https://github.com/earlephilhower/arduino-pico/releases/download/4.5.2/package_rp2040_index.json
   ```

   Then Tools → Board → Boards Manager → install *Raspberry Pi Pico/RP2040/RP2350*.

2. **Library** — Sketch → Include Library → Manage Libraries → install
   **`Pico_Bidir_DShot`** ([bastian2001/pico-bidir-dshot](https://github.com/bastian2001/pico-bidir-dshot)).

3. **Board settings**

   | Setting | Value |
   |---|---|
   | Board | Raspberry Pi Pico 2 (or Generic RP2350 to address the full 16 MB) |
   | CPU Speed | 150 MHz |
   | CPU Architecture | ARM |
   | Flash Size | 4 MB is plenty for this sketch |
   | USB Stack | Pico SDK |

4. **Upload** — hold BOOT, tap RESET, release BOOT. The board enumerates as mass storage;
   upload normally after that.

### Or just flash a release

Prebuilt `.uf2` images are attached to every
[release](https://github.com/subtilitas/DshotDisplay/releases). Hold BOOT, tap RESET,
release BOOT, and copy the file onto the drive that appears. Pick by board first
(`touch-lcd-2` or `touch-lcd-2.8` in the filename), then take the **arm** build unless
you specifically want the RP2350's RISC-V cores — both are functionally identical.

Every green CI run also uploads a `.uf2` artifact, so an untagged change can be tried on
hardware without cutting a release.

### Reproducible builds

`sketch.yaml` is an arduino-cli build profile — the closest thing Arduino has to a lock
file. It pins the core, its bundled toolchain, the library and the board options together,
so you don't depend on whatever happens to be installed:

```sh
test/select_board.sh BOARD_RP2350_TOUCH_LCD_2      # or ..._2_8
arduino-cli compile --profile rp2350               # or rp2350_28
arduino-cli upload  --profile rp2350 -p <port>
```

A profile is self-contained: arduino-cli installs the pinned platform and libraries into a
profile-local directory rather than the global sketchbook, so nothing system-wide affects
the result. Steps 1–3 above are only needed if you'd rather use the IDE.

Both boards share an FQBN — same MCU, same flash — so the two profiles differ only in
their notes. They exist so the intent is recorded in the build command instead of only in
an edited header; the profile does **not** select the board, `select_board.sh` does.
Compiling `rp2350_28` without having run it produces a perfectly good image for the wrong
board, which is why CI always runs the two together.

Currently pinned: arduino-pico **4.5.2**, Pico_Bidir_DShot **1.0.2**, FQBN
`rp2040:rp2040:rpipico2:flash=4194304_0,freq=150,arch=arm`. Switch `arch` to `riscv` to
build for the RP2350's Hazard3 cores instead — the DShot library supports both.

PlatformIO works too — add to `platformio.ini`:

```ini
lib_deps = https://github.com/bastian2001/pico-bidir-dshot.git
```

---

## Using it

**Main screen**

- **HOLD TO ARM** — press and hold for 1 s. Refuses to arm unless the throttle has been at
  zero for 250 ms. Once armed the button becomes **DISARM** and responds instantly to a tap.
- **Throttle bar** — behaves differently depending on HOLD, because a latched throttle and
  a momentary one want opposite gestures:

  | | HOLD off (default) | HOLD on |
  |---|---|---|
  | Bar label | `THROTTLE` | `THROTTLE REL` + grab handle |
  | Gesture | **absolute** — finger position *is* the throttle | **relative** — throttle moves by how far the finger travelled |
  | Touching the bar | jumps to that position | changes nothing |
  | On release | springs back to zero | stays where you left it |

  Relative dragging accumulates, so you can build throttle up over several short swipes
  instead of needing one precise placement, and it re-anchors at both rails — overshoot
  the bottom and a small push right responds immediately rather than having to unwind.
  Tune the feel with `HOLD_DRAG_SENSITIVITY_PCT` in `config.h`; below 100 gives finer
  control per swipe.

  The percentage shown is always of the real 0–2000 DShot range, so the number never
  flatters you, while the bar's full width is your configured ceiling.
- **Throttle pad** — the whole number-display area above the gauge (RPM readout and
  telemetry tiles) is a large relative throttle surface. Swipe **up** for more, **down**
  for less. It's a much bigger target than the 26 px gauge strip, so you can adjust
  throttle without looking at the screen, and swipes accumulate.

  It is *always* relative, in both modes, because a big blank region has no positional
  meaning — a tap on it must never move the motor. A 6 px deadzone
  (`PAD_DEADZONE_PX`) means taps, thumb roll and panel jitter are inert; only a deliberate
  drag engages. Sensitivity is `PAD_DRAG_SENSITIVITY_PCT`, default 60, so one full-height
  swipe covers 60 % of the ceiling.

  The pad and the gauge's grab area abut exactly, so there's no overlap and no dead strip
  between them. `SWIPE = THROTTLE` under the RPM readout lights cyan when the pad is live.
- **HOLD** — off is the safe mode. Turn it on only when you actually need a steady-state
  run; turning it off zeroes the throttle immediately.
- **CFG** — settings. Entering it force-disarms.

**Settings screen**

- **Motor poles** — eRPM → RPM conversion is `RPM = eRPM / (poles / 2)`. Almost every
  quad motor is 14.
- **Throttle ceiling** — defaults to a deliberately timid 20 %. Raise it when you know
  what's spinning.
- **EDT ON** — resends `DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE` (the firmware already does
  this automatically 1.5 s after boot). Only works while disarmed.
- **BEEP** — `DSHOT_CMD_BEACON1`, handy for finding which ESC you're actually plugged into.

**Reading the telemetry**

`--` means the ESC never sent that frame type. Plain eRPM always works on bidirectional
DShot; voltage, current, temperature, stress and status need EDT support in the ESC
firmware (BLHeli_32, Bluejay, AM32). The **LINK** tile shows good packets per second and
the checksum error rate — at 1 kHz you should see close to 1000/s and 0 % err. A rising
error rate points at signal integrity, not at the ESC.

---

## AM32 ESC configuration

**CFG → AM32 ESC CONFIG** reads and edits an AM32 ESC's settings over the same signal wire,
with no laptop involved. Settings only for now; the transport is deliberately general so
firmware flashing drops in later.

![AM32 config](docs/am32-preview.png)

Entering config mode disarms, tears down the DShot driver and hands the signal pin to the
bootloader transport. Leaving hands it back.

### Connecting

Power the ESC, then open config mode. Each attempt **holds the signal line low** for
`AM32_BOOT_LOW_MS` (default 200 ms), which makes AM32's running firmware give up on finding
a valid input and jump to its bootloader. That's what allows connecting to an ESC that is
already powered, instead of having to catch the brief listening window right after power-up.
The line is then released, and the greeting is sent.

If it doesn't connect, the screen tells you *which* half failed:

| Message | Meaning | What to change |
|---|---|---|
| `NO REPLY - ESC DID NOT JUMP` | Line stayed silent | Raise `AM32_BOOT_LOW_MS`; try `AM32_PUSH_PULL_TX 0` |
| `GOT n BYTES: xx xx xx` | ESC is talking, reply didn't parse | Baud or framing, not the jump — the bytes are shown so you can see what arrived |

That distinction matters: silence and garbage need completely different fixes, and without
a scope on the signal wire there's otherwise no way to tell them apart.

Set `AM32_DEBUG 1` in `config.h` to mirror every transport step to USB serial.

### Editing

Two gestures, coarse and fine:

| Gesture | Effect |
|---|---|
| Tap a row | Selects it; the editor bar at the bottom shows its name and value |
| **Swipe the row sideways** | **Coarse** — one full-width swipe covers the field's entire range |
| `-` / `+` buttons | **Fine** — one step per press; hold to repeat, accelerating |
| Drag up/down | Scrolls the list |

Axes are locked exclusively after 10 px of travel, so a swipe is either a scroll or an edit
and never both. Coarse swipes snap to each field's own step, so motor poles stay even, and
they re-anchor at the ends — overshoot the top and a small swipe back responds immediately
instead of unwinding.

Changed rows are marked amber down the left edge. **REVERT** restores everything to what
was read. **HEX** shows the raw 48 bytes, which is the escape hatch if a field is ever
decoded wrongly.

Writing needs a **one second hold** on `HOLD TO WRITE` — a stray tap must never reprogram a
motor controller. After writing, the block is read back and compared; a write that reports
success but lands wrong is worse than one that fails loudly, so the screen only says
`WRITE VERIFIED` if the readback matches byte for byte.

### Protocol

| | |
|---|---|
| Link | one-wire half-duplex serial, 19200 8N1, on `DSHOT_PIN` |
| Init string | 21 bytes: twelve `0x00`, `0x0D`, `"BLHeli"`, `0xF4 0x7D` |
| Commands | `SET_ADDRESS 0xFF 00 hi lo`, `SET_BUFFER 0xFE 00 00 len`, `PROG_FLASH 0x01 0x01`, `READ_FLASH 0x03 len`, `ERASE 0x02`, `RUN 0x00` |
| Checksum | CRC-16/ARC — polynomial `0xA001`, init 0, appended low byte first |
| ACK | `0x30`, always the last byte received |
| Settings | 48 bytes, at an address that depends on the MCU |

The settings page is **not** at a fixed address. The handshake reply identifies the MCU
family in its fifth-from-last byte, and that decides where to look:

| Type byte | Family | Settings at |
|---|---|---|
| `0x2B` | STM32G071, 2 KB pages | `0x7E00` |
| `0x1F` | STM32F051, 1 KB pages | `0x7C00` |
| `0x35` | STM32F3, 2 KB pages | `0xF800` |

Indexing the reply from the *end* rather than the start is deliberate: a host with a
hardware UART on the shared wire also captures its own transmission, so only the tail is
in a predictable place.

Bytes `0x05..0x0C` are overloaded — device name before layout revision 3, ramp and
current-PID settings from revision 3 on — so fields are version-gated and the wrong set is
never shown.

Because host and ESC share one conductor, everything transmitted is echoed back and
discarded before the reply is read. Interrupts are masked per byte rather than per frame:
masking for a whole frame would starve the system for milliseconds, while per byte keeps
the critical section near half a millisecond.

### Verification

Two independent cross-checks, both run from the host test harness:

- Every protocol constant is asserted against a known-working Python configurator —
  init string, all six command encodings, CRC polynomial and byte order, ACK position, and
  all three MCU-to-address mappings.
- The field table is decoded against that tool's own default settings blob and compared to
  the semantics documented in its byte comments: 14 poles, 24 kHz, 2200 KV, 15° advance,
  3.0 V cell cutoff, 1502 µs servo neutral, 204 A current limit, and so on.

The current-limit `×2` factor and the one-based protocol enum were both wrong until that
second check caught them.

---

## Safety

A motor on a bench is genuinely dangerous, and a propeller on a bench doubly so.

Built into the firmware:

- Boots disarmed and sends `MOTOR_STOP` continuously.
- Arming needs a deliberate 1 s hold with the throttle already at zero.
- Throttle springs back to zero on finger-lift unless HOLD is explicitly enabled.
- Configurable throttle ceiling, default 20 %.
- Auto-disarm after 30 s with no touch input (`IDLE_DISARM_MS`).
- Core1 zeroes the throttle if core0 stops sending heartbeats.
- Entering the settings screen force-disarms.

Not built into the firmware, and up to you:

- Take the prop off unless you specifically need it on.
- Clamp the motor down. Every time.
- Eye protection, and stay out of the plane of rotation.
- Have a way to cut battery power that isn't the touchscreen.

None of that makes the tester safe — it makes it *less unsafe*. The software interlocks are
a backstop for your habits, not a replacement for them.

---

## Layout

```
DshotDisplay.ino        core0 setup/loop, core1 setup1/loop1, @mainpage
sketch.yaml             pinned core + library versions (arduino-cli profiles)
src/
  config.h              everything you'd want to tune
  board.h               which board this build targets
  board_pins.h          dispatcher + the contract a board header must satisfy
  board_rp2350_touch_lcd_2.h     2.0" pin map, annotated from the schematic
  board_rp2350_touch_lcd_2_8.h   2.8" pin map, ditto
  esc_task.{h,cpp}      core1 DShot pump, EDT decode, cross-core state
  ui.{h,cpp}            screens, touch handling, arm/throttle state machine
  ui_am32.{h,cpp}       AM32 config screen: connect, edit, verified write
  am32_bl.{h,cpp}       one-wire bootloader transport (reused for FW flashing)
  am32_eeprom.{h,cpp}   AM32 settings layout, decoding and presentation
  gfx.{h,cpp}           RGB565 framebuffer, dirty bands, 5x7 font, 7-seg digits
  st7789.{h,cpp}        panel init + DMA blitter
  touch.h               touch interface + the shared rotation mapping
  cst816.cpp            CST816D driver  (2.0" board)
  cst328.cpp            CST328 driver   (2.8" board)
Doxyfile                API doc config -> docs/html/
docs/                   generated docs + UI preview image
test/                   host test suites + Arduino/Pico SDK stubs
test/select_board.sh    rewrites BOARD in src/board.h, then reads it back
.github/workflows/      CI
```

Both touch drivers are compiled on every build — the Arduino builder walks `src/`
recursively and does not know about boards — so each is wrapped in a whole-file `#ifdef`
and the unselected one becomes an empty translation unit. The rotation mapping lives in
`touch.h` rather than being copied into both, because a rotation that is right on one
board and stale on the other is a bug nobody finds without owning both.

The `.ino` has to stay at the sketch root — Arduino identifies the sketch by a `.ino` whose
name matches its folder. Everything else lives in `src/`, which the Arduino builder compiles
recursively. The sketch includes them as `"src/gfx.h"` because only the sketch root goes on
the include path; files inside `src/` include each other as plain siblings, since the
compiler resolves quoted includes relative to the including file.

### Tests

The firmware's own logic runs on a PC against stubs in `test/stubs/` that mirror the
Arduino and Pico SDK signatures:

```sh
cd test && make          # default board
cd test && make both     # every supported board
```

`make BOARD=BOARD_RP2350_TOUCH_LCD_2_8` builds against the other pin map; `make both`
does each in turn, which is what CI runs.

Needs nothing but a C++17 compiler. Time is virtual — `millis()` advances only when a test
says so — which makes hold-to-arm, hold-to-write and gesture repeat deterministic rather
than dependent on machine speed. The fake ESC serves a real settings blob recovered from
the reference configurator, so the UI tests operate on values a real ESC would report.
Failing tests dump the rendered frame as a PPM, so layout bugs can be looked at.

`test_ui.cpp` `#include`s `ui_am32.cpp` rather than linking it, so tests can read its
file-static state without production code carrying test-only accessors. That's why the
Makefile does not list `ui_am32.cpp` in `SRCS`.

### Continuous integration

`.github/workflows/ci.yml` runs four jobs:

| Job | What it catches |
|---|---|
| **Firmware** | Real `arduino-cli` build, both boards × ARM and RISC-V, pinned by `sketch.yaml` |
| **Host tests** | Protocol, codec and UI regressions a compile cannot see, run per board |
| **Config permutations** | All 8 combinations of `AM32_PUSH_PULL_TX`, `AM32_FORCE_LOW_JUMP` and `LCD_ROTATION`, on both boards, warnings fatal |
| **Doxygen** | Undocumented additions |

The permutation job exists because those options are exactly the ones nobody compiles by
hand — and because it is the **only** job that compiles the touch drivers at all. The host
tests link fakes in their place, so a syntax error in `cst328.cpp` would otherwise reach a
release. It also asserts that the landscape guard and the unknown-board guard actually
fire, since an `#error` nobody ever triggers is an `#error` that can quietly stop working. `check_docs.py` enforces documentation precisely — public API documented, `@param`
names matching signatures, no dangling `@ref` — where Doxygen's `WARN_IF_UNDOCUMENTED`
would also demand a comment on every file-static `s_scroll`.

### Releases

```sh
git tag -a v1.0.0 -m "First release"
git push origin v1.0.0
```

`release.yml` runs the host and documentation checks, builds `.uf2` images for both boards
on both RP2350 cores, checksums them, and publishes a GitHub Release with the images
attached.

Assets per release:

```
DshotDisplay-v1.0.0-touch-lcd-2-arm.uf2
DshotDisplay-v1.0.0-touch-lcd-2-riscv.uf2
DshotDisplay-v1.0.0-touch-lcd-2.8-arm.uf2
DshotDisplay-v1.0.0-touch-lcd-2.8-riscv.uf2
SHA256SUMS.txt
```

Four things it refuses to do, each because the failure is otherwise silent:

- **Build from failing code** — a broken image carrying a version number is worse than no
  release at all.
- **Publish an implausible image** — the `.uf2` is checked for the `UF2\n` magic and a
  512-byte-multiple size, so a truncated or mis-copied file fails the build rather than the
  user's board.
- **Invent a tag** — `--verify-tag` aborts if the tag was never pushed.
- **Leave a half-populated release** — creating a release and attaching assets are separate
  API calls, so the workflow reads the release back and confirms every expected asset is
  actually there. Each board/arch pair is named explicitly: checking only for "arm" and
  "riscv" would pass with one board's images missing entirely.

### API documentation

The source is annotated in Doxygen style, and a `Doxyfile` is checked in:

```sh
doxygen            # writes docs/html/index.html
```

`EXTRACT_ALL` is deliberately `NO` and `WARN_NO_PARAMDOC` is `YES`, so anything you add
without documentation shows up as a warning rather than silently producing an empty page:

```sh
doxygen 2>&1 | grep -i warning
```

`EXTRACT_STATIC` is on — most of this firmware is file-static, so leaving it off would
document almost nothing. The `@mainpage` block lives in `DshotDisplay.ino` and covers the
two-core architecture; this README is rendered as the front page.

### Rendering

The whole 240×320 frame lives in SRAM (150 KB of the RP2350's 520 KB).
Draw calls record the vertical span they touched; only those full-width bands get DMA'd,
each as one contiguous transfer with no per-row restart cost. Each UI zone also caches the
values it last displayed and skips drawing entirely when nothing changed, so a steady
screen costs almost no SPI traffic at all.

**Pixel format** — the SPI peripheral switches to 16-bit frames for pixel data, so RGB565
words go out MSB-first with no software byte swapping. Commands go out in 8-bit frames.

---

## Credits

- [pico-bidir-dshot](https://github.com/bastian2001/pico-bidir-dshot) by bastian2001 — the
  PIO DShot implementation doing the actual protocol work.
- [DShot — the missing handbook](https://brushlesswhoop.com/dshot-and-bidirectional-dshot/)
- [Extended DShot Telemetry](https://github.com/bird-sanctuary/extended-dshot-telemetry)
- [Waveshare RP2350-Touch-LCD-2 wiki](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2)
  and [schematic](https://files.waveshare.com/wiki/RP2350-Touch-LCD-2/RP2350-Touch-LCD-2.pdf)
- [Waveshare RP2350-Touch-LCD-2.8 wiki](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2.8)
  and [schematic](https://files.waveshare.com/wiki/RP2350-Touch-LCD-2.8/RP2350-Touch-LCD-2.8-Schematic.pdf)
- [CSE_CST328](https://github.com/CIRCUITSTATE/CSE_CST328) by CIRCUITSTATE — the reference
  the CST328 init and coordinate packing were checked against.

## License

This repository's own code is MIT (see `LICENSE`).

Note that `pico-bidir-dshot` is **GPL-3.0**. That's fine for personal use, but a compiled
binary of this sketch links GPL-3.0 code, so if you distribute firmware images or a
derived product you'll need to satisfy GPL-3.0 for the combined work. Worth settling
before you ship anything.
