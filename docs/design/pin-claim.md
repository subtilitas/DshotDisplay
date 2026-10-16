# Pin claim and core1 jobs

Status: planned. Nothing here is built. The design uses the DShot driver in
`src/dshot/` (`out_dshot_bind()`, `out_dshot_release()`).

Terms: ESC is electronic speed controller. DShot is the digital ESC protocol
the tester drives; bidirectional DShot inverts the line and the ESC answers on
the same wire. EDT is Extended DShot Telemetry. KISS telemetry is the serial
protocol of the KISS ESC line, sent on its own wire. AM32 is an open ESC
firmware with a serial bootloader. KST and Chaservo are servo brands that
share one programming protocol. PWM is pulse-width modulation, here the
RP2350's hardware PWM slices that make servo pulses. PIO is the RP2350's
programmable input/output block, SM is one PIO state machine, DMA is direct
memory access, GPIO is general-purpose input/output, ADC is analogue-to-digital
converter, SD is Secure Digital, SDIO is Secure Digital input/output, SPI is
Serial Peripheral Interface, UI is user interface, USB is Universal Serial Bus.

The two features that use the claim have their own documents:
[servo-tester.md](servo-tester.md) and
[kst-programming.md](kst-programming.md). This document holds what they share:
the claim, the pins, the hardware budget and the wiring. The slice plan for all
three is at the end.

## Why

DshotDisplay brings out two signal pins. On the 2.8" board they are the only
free GPIOs. Today both belong to the ESC for the whole run: the DShot pump on
one, the KISS receiver on the other. The servo tester and the KST programmer
need the same two wires. A second set of pins does not exist on the 2.8"
board, so the pins change owner by screen.

## The two pins

The claim names pins by role, not by number, because the number is a setting.

| Role | Today | Claimed by |
|---|---|---|
| SIG | the ESC signal wire, `settings()->dshotPin` | AM32 config, servo A, KST programming |
| AUX | the KISS telemetry wire, `settings()->kissPin` | servo B (two-servo mode) |

AUX is the KISS pin whether or not KISS is switched on in SETUP. On the 2.8"
board KISS is off by default, and GP28 is still the AUX pin.

| Board | SIG default | AUX default | Header |
|---|---|---|---|
| RP2350-Touch-LCD-2 | GP4 | GP5 | GP4 on P2 pin 11, GP5 on P1 pin 10, GND on P1 pin 13 and P2 pins 2 and 13 |
| RP2350-Touch-LCD-2.8 | GP29 | GP28 | J4 pin 12 and J4 pin 11, GND on J4 pins 1 and 5 |

On the 2.0" board SETUP offers GP0 to GP11 and GP21 to GP23 for either role
(`BOARD_FREE_GPIO_MASK` 0x00E00FFF). On the 2.8" board it offers GP28 and GP29
only (0x30000000).

## The claim

A claim takes the whole ESC task down, not one pin of it. With the SIG pin
gone the ESC hears no frames and disarms, and with no DShot frames there is no
KISS request, so the KISS receiver has nothing to receive. A claim of SIG alone
would leave a receiver running on AUX for no reason, and a claim of AUX alone
has no user: every screen that wants AUX also wants SIG. So every claim is the
existing `escTaskSuspend()` and both pins are free while it holds. The roles a
screen asks for decide only which pins its job binds.

| Screen | Roles bound | Core1 job | Who drives the pin |
|---|---|---|---|
| AM32 CONFIG | SIG | none, core1 idle | core0, bit-banged bootloader transport (`am32_bl.cpp`) |
| SERVO, one servo | SIG | servo | core1, hardware PWM |
| SERVO, two servos | SIG and AUX | servo | core1, hardware PWM |
| KST SERVO | SIG | KST | core1, `kst_line` PIO driver and session |

A claim lasts while its screen is open, its sub-screens included (SERVO SET,
KST FILES). Leaving the screen with BACK ends it. No other screen is reachable
during a claim: SETUP, SD LOG and the main screen sit behind BACK.

### Sequence

Core0 runs the claim as a state machine in `src/pin_claim.cpp`, one step per UI
frame (`UI_PERIOD_MS`, 25 ms). Core1 does every hardware change, because the
core that claims a PIO state machine is the core that frees it, as
`esc_task.h` already requires.

1. **Check.** Core0 refuses the claim while armed, and, for SERVO and KST,
   while an ESC answers on SIG. See [interlocks](#interlocks). DShot keeps
   running during the check.
2. **Stopping.** Core0 calls `escTaskSuspend()`, which forces a disarm. Core1
   sends throttle-zero frames until `ARM_ZERO_THROTTLE_MS`, 250 ms, have passed
   since the last frame that carried a throttle, then `out_dshot_stop(sig)`
   (no edges), `out_dshot_release(sig)` (pad back to a plain input, pulls
   off), and `kissUartEnd()`, which ends the PIO receiver on AUX. It then sets
   `escTaskSuspended()`. Coming from CFG, the 250 ms have passed long before:
   CFG disarms on entry and the user spends more than 250 ms on the SETTINGS
   screen. The wait matters only for a caller that skips the UI, such as a
   test.
3. **Held.** Core0 reads the GPIO numbers once: SIG from `escTaskDshotPin()`,
   AUX from a new `escTaskKissPin()`. For SERVO and KST it starts the core1
   job with those numbers. AM32 calls `am32BlBegin()` as today.
4. **Running.** The job binds its pins and reports `Running`, or reports a bind
   failure and binds nothing.
5. **Leaving.** BACK asks the job to stop. The job leaves its pins as plain
   inputs and reports `Idle`. Before DShot restarts, SERVO and KST show the
   unplug page ([restore](#restoring-dshot)).
6. **Restoring.** Core0 calls `escTaskResume()`. Core1 rebuilds the driver from
   the wiring latched in `esc_task.cpp` and the claim is `Free`.

Today's `escTaskSuspend()` tears the driver down on the next poll and sends no
stop frames. Step 2 adds the 250 ms drain to that same path, so the AM32
screen gets it too.

### API

Core0, in `src/pin_claim.h`:

```c
enum class PinUser : uint8_t { None, Am32, Servo, Kst };
enum class ClaimState : uint8_t { Free, Stopping, Held, Running, Leaving, Restoring };
enum class ClaimRefusal : uint8_t {
    None,
    Armed,         // the tester is armed
    Busy,          // another screen holds the pins
    EscAnswering,  // an eRPM frame within ESC_LINK_STALE_MS (500 ms)
    KissLive,      // a KISS frame within KISS_STALE_MS (500 ms), AUX asked for
    PwmPair,       // SIG and AUX share a PWM compare register
};

ClaimRefusal pinClaimBegin(PinUser who, uint8_t roles);  // PIN_SIG | PIN_AUX
ClaimState   pinClaimPoll();               // once per UI frame
uint8_t      pinClaimGpio(uint8_t role);   // latched at Held; 0xFF before
uint8_t      pinClaimJobError();           // why a bind failed, 0 for none
void         pinClaimEnd(PinUser who);     // BACK; the poll finishes it
```

The refusal check is a pure function, `pinClaimCheck()`, beside it, so the
host suite tests every rule without the SDK.

Core1, in `src/core1_job.h`:

```c
enum class Core1Job : uint8_t { None, Servo, Kst };

bool core1JobStart(Core1Job job, uint8_t sigGpio, uint8_t auxGpio);  // core0
void core1JobStop();                                                   // core0
Core1Job core1JobRunning();   // core0: which job runs; None once idle
void core1JobPoll();          // core1, from loop1() after escTaskPoll()
bool core1UiAlive();          // core1: escHeartbeat() within 250 ms
```

`loop1()` becomes `escTaskPoll(); core1JobPoll();`. `escTaskPoll()` returns at
once while suspended, as it does today, and `core1JobPoll()` returns at once
while no job runs. `core1JobStart()` is refused unless `escTaskSuspended()` is
true, so the DShot pump and a job never drive a pin together.

Each job has its own command block and status block, written and read under
its own critical section, in the same pattern as `escSnapshot()`. They are in
the two feature documents. `core1UiAlive()` reads the heartbeat
`escTaskPoll()` already uses, so one rule decides "the UI is alive" for every
job.

### What core1 does while a pin is claimed

| Claim | Core1 | Why there |
|---|---|---|
| AM32 | nothing; `escTaskPoll()` and `core1JobPoll()` both return at once | the bootloader transport runs on core0 today and blocks it for about 40 ms per attempt; nothing in it needs core1 |
| SERVO | the servo job: holds the PWM bindings, slews the pulse width at SPEED, runs the sweep curve, checks the heartbeat, every pass of `loop1()` | core0 draws at 40 Hz and stalls for tens of milliseconds on SD writes; a 5 Hz sine sampled at 40 Hz is 8 steps per cycle. Core1 samples the curve every millisecond and PWM latches it at each frame |
| KST | the KST job: the entry loop wrapper, `kst_session_step()` and the `kst_line` driver | the session needs a step every 1 ms or faster, and its driver slack is 5 ms (`KST_DRIVER_SLACK_US`); core0 cannot promise either |

### Interlocks

**Armed: the claim is refused.** `pinClaimBegin()` returns `Armed` and
changes nothing. It does not disarm on the caller's behalf. Every entry point
is on the SETTINGS screen, and the CFG button that opens it already disarms
(`handleMainTouch()`), so through the UI the refusal is unreachable. A route
added later that reaches a claim while armed then shows up as a refused claim
in a test, instead of a motor stopped as a side effect of navigation.
`escTaskSuspend()` keeps its own forced disarm as the backstop under that
check.

An armed ESC therefore never has its pin taken. What it sees is: throttle zero
from the disarm, at least 250 ms of zero frames, then silence. On silence the
ESC disarms after its own signal-loss timeout, which is ESC firmware specific
and not recorded here.

**An ESC answers on SIG: SERVO and KST wait.** Servo pulses of 1000 to
2000 µs are a valid PWM throttle signal, and AM32 and BLHeli_32 accept PWM
input. A KST entry sequence holds the line low for 100 ms and sends frames
into whatever is there. So while an eRPM frame has arrived within
`ESC_LINK_STALE_MS`, 500 ms, the screen does not claim. It shows the reason
and keeps checking every frame with DShot still running; 500 ms after the ESC
goes quiet it claims by itself. The pulses still need HOLD TO START
([servo-tester.md](servo-tester.md)). The check misses an ESC that does not
speak bidirectional DShot, and an ESC plugged in after the claim. The screen
text says so.

AM32 is exempt: it needs the ESC on the pin.

**KISS frames on AUX: two-servo mode waits.** A live KISS frame within
`KISS_STALE_MS`, 500 ms, means an ESC telemetry output drives AUX. Servo PWM
on that pin would drive against it. The two-servo mode is refused with the
reason; one-servo mode is not affected.

**SIG and AUX share a compare register: two-servo mode is refused.** See
[PWM slices](#pwm-slices). Only reachable on the 2.0" board with pins chosen
in SETUP.

### Heartbeat and watchdog

Core0 calls `escHeartbeat()` at the top of every `uiTick()`, on every screen.
`UI_HEARTBEAT_TIMEOUT_MS` is 250 ms.

| Owner | Heartbeat lost for 250 ms | Heartbeat back |
|---|---|---|
| DShot | throttle forced to zero (`escFrameAction()`, unchanged) | throttle follows the UI again |
| AM32 | nothing; the transport runs on core0, which is what stopped | nothing |
| Servo job | both pins to level 0, no edges; the sweep ends; the job latches `Tripped` | nothing restarts. The screen shows `STOPPED: SCREEN STALLED` and needs a new HOLD TO START |
| KST job | the running operation finishes; no new operation and no new entry attempt starts; the line stays driven low | operations are taken again; the search needs RETRY |

Why KST does not abort: `kst_session_abort()` after the first write frame of a
plan locks the session and can leave the servo between two images. The
heartbeat exists to stop motion that the UI can no longer control; a write
plan commands no motion and ends by itself. Whether the motor moves in
programming mode is not measured (rcbench `docs/ServoProgrammer.md`,
measurement 2), so this is an open question below.

No flash write happens during a claim. `settingsSave()` parks core1 for tens
of milliseconds, which would stall a KST transaction past its driver slack.
SETUP, the only caller, is unreachable while a claim holds, and
`settingsSave()` gains an assertion that no claim holds.

### Settings

The claim reads the pins SETUP chose, as latched by `escTaskConfigure()`, not
the compiled defaults. They cannot change during a claim, because SETUP is
unreachable. A pin change in SETUP applies to the next claim.

Nothing about the servo tester or the programmer is stored in the settings
block. `SETTINGS_VERSION` stays 2, so no stored block is discarded by this
work. See the open questions in [servo-tester.md](servo-tester.md).

### When the pin is unavailable

The screen draws its header and BACK, a message in place of its body, and its
primary control disabled (drawn and gated per `ui_widgets.h`). BACK always
works.

| Cause | Screen text | Way out |
|---|---|---|
| An ESC answers on SIG | `AN ESC ANSWERS ON GP29.` `UNPLUG IT: SERVO PULSES READ AS THROTTLE.` | unplug the ESC; the claim follows 500 ms later |
| KISS frames on AUX, two servos asked | `KISS WIRE ON GP28 IS LIVE.` `TWO SERVOS NEED IT UNPLUGGED.` | unplug it, or choose one servo |
| SIG and AUX share a compare register | `GP5 AND GP21 SHARE ONE PWM OUTPUT.` `PICK ANOTHER PIN IN SETUP.` | one servo, or other pins |
| The job could not bind | `GP29 COULD NOT BE TAKEN (PWM)` or `(PIO)`, `(DMA)`, `(CLOCK)` | RETRY, or BACK; DShot is restored either way |
| Armed, or another screen holds the pins | `DISARM FIRST` or `PINS BUSY` | BACK; unreachable through the UI |

The GPIO numbers in the text are the live ones from SETUP.

### Restoring DShot

`escTaskResume()` rebuilds the driver from the wiring `esc_task.cpp` latched:
the same SIG pin, bitrate, bidirectional mode, KISS enable and KISS pin. Pole
count and throttle ceiling live in the settings block and are untouched. The
pump starts disarmed with HOLD off, as after a SETUP change. The EDT enable
runs again for the ESC it finds, because a rebuild clears its state
(`s_edtTried`). Telemetry counters in `EscTelemetry` carry on.

`out_dshot_bind()` sets the pull-up and the output inversion on SIG.
`kissUartBegin()` sets the pull-up on AUX. Both override what the job left:
servo PWM leaves a plain input with no pull, `kst_line_close()` leaves an
input with the pull-down on.

**Unplug page.** Bidirectional DShot idles the line high and pulls it low for
about 27 µs per frame at 600 kbit/s, once per `DSHOT_PERIOD_US`, 1000 µs. A
PWM servo left on SIG measures that as a pulse of about 973 µs at 1 kHz: it
drives towards one end of its travel, at a frame rate that overheats an
analogue servo. So before `escTaskResume()` the SERVO and KST screens show:

```
  UNPLUG THE SERVO.
  DSHOT RESTARTS ON GP29 AND READS AS
  A 970 US PULSE AT 1000 HZ.
  [ RESTART DSHOT ]
```

RESTART DSHOT is a tap. BACK on that page returns to the screen and keeps the
claim. The same hazard exists at boot for a servo left on SIG; the README
wiring section gains that warning.

### Reuse of the AM32 handover

`ui_am32.cpp` already performs a pin handover: `escTaskSuspend()` on entry,
`S_HANDOVER` until `escTaskSuspended()`, `am32BlBegin(escTaskDshotPin())`,
and `am32BlEnd()` with `escTaskResume()` on exit. The claim is that code
moved into `pin_claim.cpp` and given a state, a refusal and a user. The AM32
screen then calls `pinClaimBegin(PinUser::Am32, PIN_SIG)` and
`pinClaimEnd(PinUser::Am32)`, and keeps its own `S_HANDOVER` state reading
`pinClaimPoll()`. Its behaviour changes in one way: the 250 ms zero-frame
drain.

## Hardware

### PWM slices

The RP2350 has 12 PWM slices with two channels, A and B. A slice has one
counter and one wrap register, so both channels run at one frame rate. A
channel is one compare register, so two pins on the same channel carry the
same pulse width. For GP0 to GP31 the slice is `(gpio >> 1) & 7` and the
channel is `gpio & 1` (`PWM_GPIO_SLICE_NUM` in Pico SDK 2.1.1,
`out_pwm_map.c` in rcbench).

| GPIO | Slice | Channel | Use |
|---|---|---|---|
| GP4 | 2 | A | 2.0" SIG default |
| GP5 | 2 | B | 2.0" AUX default |
| GP28 | 6 | A | 2.8" AUX |
| GP29 | 6 | B | 2.8" SIG |
| GP15 | 7 | B | 2.0" backlight PWM (`st7789.cpp`) |
| GP16 | 0 | A | 2.8" backlight PWM |

Both boards' defaults put the two servos on one slice, channels A and B: two
pulse widths, one frame rate.

On the 2.0" board three pairs of free pins share a compare register: GP5 and
GP21 (2B), GP6 and GP22 (3A), GP7 and GP23 (3B). Two servos on such a pair
are refused. Pins on one slice share a rate: GP(2k) with GP(2k+1) for GP0 to
GP11, GP21 with GP4 and GP5, GP22 and GP23 with GP6 and GP7. The servo tester
uses one frame rate for both servos on every pin pair, so a shared slice never
needs a choice.

No free pin on either board is on the backlight's slice: slice 7 on the 2.0"
holds GP14 and GP15, neither free; slice 0 on the 2.8" holds GP0, GP1, GP16
and GP17, none free. Each board descriptor gains a `static_assert` for this,
because `pwm_init()` on the backlight's slice would retime the backlight.

The PWM counter runs at 1 MHz: `clk_sys` 150 MHz divided by 150, an integer
divider. One count is 1 µs. The wrap is 19,999 at 50 Hz and 1,784 at 560 Hz.

### PIO and DMA budget

Each PIO block has 32 instruction words and 4 state machines. The RP2350 has 3
blocks and 16 DMA channels. Program sizes are counted from the `.pio` sources
and, for SDIO, from the generated header in no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
v3.6.2.

| User | Program words | SMs | DMA channels | Board |
|---|---|---|---|---|
| DShot, bidirectional (rcbench `dshot_bidir_tx` + `dshot_rx`) | 11 + 5 = 16 | 2, adjacent, one block | 0 | both |
| KISS receiver (`pio_uart_rx`) | 9 | 1 | 0 | both |
| SDIO card (`sdio_cmd_clk` + `sdio_data_rx` + `sdio_data_tx`), fixed to pio1 | 18 + 5 + 9 = 32 | 2 | 2 | 2.8" |
| SD card over SPI1 | 0 | 0 | 2 | 2.0" |
| Panel (`st7789.cpp`) | 0 | 0 | 1 | both |
| KST line (`kst_line`) | 24 | 1 | 2 | both, during a KST claim |
| Servo PWM | 0 | 0 | 0 | both, PWM slices only |

Placement, with DShot built before KISS as `escTaskPoll()` does. The SDK's
free-block search starts at pio2, and `src/esc_task.cpp` asserts at build
time that DShot and KISS fit one block (3 state machines, 25 of 32 words):

| Block | 2.0", ESC running | 2.0", KST claim | 2.8", ESC running | 2.8", KST claim |
|---|---|---|---|---|
| pio0 | free | free | free | free |
| pio1 | free | free | SDIO, 32 of 32 words, 2 SMs | SDIO, full |
| pio2 | DShot 16 + KISS 9 = 25 words, SM0 to SM2 | KST 24 words, SM0 | DShot + KISS, 25 words | KST 24 words |
| DMA | 3 of 16 | 5 of 16 | 3 of 16 | 5 of 16 |

Nothing needs the claim for resources. If the KST line opened beside a
running DShot and KISS, it would not fit in pio2 (7 words left) and would land
in pio0 on either board. The claim exists for the pin, not for the PIO block.

### System clock

Nothing in DshotDisplay sets `clk_sys`. Pico SDK 2.1.1 defaults it to
150 MHz (`SYS_CLK_HZ` in `platform_defs.h`), and neither the FatFs driver nor
the panel driver changes it. The KST driver needs a 25.40 µs half-cell within
0.2 %: 3810 cycles at 150 MHz, exact. The clock may move by ±0.3 MHz before
`kst_line_open()` refuses it.

### Signal level

The pins drive 3.3 V, from IOVDD. The DshotDisplay README treats every GPIO as
3.6 V maximum. rcbench's documents rate RP2350 GPIO0 to GPIO39 at 5.5 V while
IOVDD is at 3.3 V and at 3.63 V while it is off. That statement is for
rcbench's RP2350B, whose ADC pins are GP40 to GP47. On the RP2350A of these
boards the ADC pins are GP26 to GP29, which includes GP28 and GP29; whether
the 5.5 V rating covers them is not checked here against the datasheet. This
design keeps the stricter rule for all signal pins on both boards: no level
above 3.6 V on SIG or AUX. It matters for KST, where the servo drives the line in
its reply ([kst-programming.md](kst-programming.md)).

Whether a given servo reads 3.3 V as high is not recorded per servo. Standard
servo inputs are commonly specified for 3.3 V logic.

### Wiring

The servo's power comes from its own supply. The board supplies signal and
ground only.

```
  servo supply, 4.8 V to 6.0 V (8.4 V for an HV servo)
     +  ----------------------------------------------  servo +   (red, middle)
     -  ----------------+-----------------------------  servo -   (brown or black)
                        |
  board GND  -----------+
  board SIG  ------------------------------------------  servo A signal (orange or white)
  board AUX  ------------------------------------------  servo B signal, two-servo mode only

  2.0": SIG GP4  = P2 pin 11, AUX GP5 = P1 pin 10, GND = P2 pin 13
  2.8": SIG GP29 = J4 pin 12, AUX GP28 = J4 pin 11, GND = J4 pin 1 or 5
```

- The servo's middle wire goes to its supply, never to a header pin. A servo
  plug pushed onto P2 pins 11 to 13 of the 2.0" board puts the servo's plus
  on GP10.
- The board's 5 V pin is its USB supply. A servo can draw more than 1 A at
  stall; do not feed it from the board.
- Power the board before the servo supply, and switch the servo supply off
  before the board. A servo fed while the board is off can drive its signal
  wire into an unpowered pin, rated 3.63 V.
- The two grounds are joined once, at the board.

## Testing

`test/test_claim.cpp`, against fakes of the esc_task and job interfaces:

- refusal per rule: armed, busy, ESC answering within 500 ms and quiet at
  500 ms, KISS live with AUX asked and ignored without, each compare-register
  pair of the 2.0" free set;
- the drain: no teardown before 250 ms of zero frames after a throttle frame;
  none needed after a long disarm;
- order: DShot released before any job bind; job idle before
  `escTaskResume()`; the unplug page before restore for SERVO and KST and not
  for AM32;
- BACK in every state, including `Stopping` and a failed bind;
- restore: the pins, bitrate and KISS settings handed to the rebuild equal
  those before the claim; the pump starts disarmed;
- the heartbeat rule per job, with the clock across a `millis()` rollover.

The AM32 tests in `test/test_ui.cpp` keep passing with the AM32 screen moved
onto the claim.

## Slices

Three slices. Each merges on its own and leaves the firmware working.

| Slice | Content | Depends on | Effort |
|---|---|---|---|
| 1. Claim | `pin_claim.*`, `core1_job.*` with no jobs yet, the drain and `escTaskKissPin()` in `esc_task.*`, `loop1()` in `main.cpp`, AM32 onto the claim, the CFG rows with SERVO and KST drawn disabled, stub `ui_servo.cpp` and `ui_kst.cpp`, per-feature source lists `src/servo.cmake`, `src/kst.cmake`, `test/servo.mk`, `test/kst.mk`, board `static_assert`s, `test_claim.cpp` | the DShot driver port | 2 to 3 days |
| 2. Servo tester | everything in [servo-tester.md](servo-tester.md) | slice 1 | 5 days |
| 3. KST programming | everything in [kst-programming.md](kst-programming.md) | slice 1 | 8 to 10 days, hardware bring-up not counted |

Slice 1 cannot run beside the DShot port: both edit `esc_task.cpp`,
`main.cpp` and `CMakeLists.txt`. Slices 2 and 3 run in parallel after slice 1.
Slice 1 creates the files they would otherwise both edit: the stub screens,
which each slice replaces whole, and one source list per feature, which
`CMakeLists.txt` and `test/Makefile` include. In code the two slices are then
disjoint: slice 2 edits its own files and `gfx.cpp`/`gfx.h` (line and arc
primitives), slice 3 its own files only. They share three documentation
files, each adding its own section: `README.md` (slice 3 also adds the legal
notice), `wiki/_Sidebar.md` and `wiki/The-Screens.md` with its German
counterpart. Each slice's user pages are new wiki files of its own. The second
slice to merge resolves those sections by hand.

## Open questions

1. **Heartbeat loss during a KST write.** Let the operation finish, or abort
   it? Recommended: finish it, as above. An abort after the first write frame
   locks the session and can leave the servo between images; a write commands
   no motion that the heartbeat could stop. Revisit if the bench shows the
   motor moving in programming mode.
2. **CFG layout.** The SETTINGS screen has one row of three destinations
   (AM32, SD LOG, SETUP) and 14 px spare below it. Recommended: split the BEEP
   row into BEEP and SD LOG, 104 px each, and make the destination row AM32,
   SERVO, KST, SETUP at 50 px each (4 × 50 px plus 3 × 4 px gaps is the 212 px
   between the margins). The three screens that take the pins sit together.
3. **ESC-answering wait.** Claim automatically 500 ms after the ESC goes
   quiet, or ask for a tap? Recommended: automatic. On SERVO the claim sends
   nothing: pulses need HOLD TO START. On KST the search starts at once, as the
   AM32 search does, and the ESC check has just passed.
4. **Unplug page.** A tap, or a hold? Recommended: a tap. The page guards
   against forgetting, not against a slip of the finger, and BACK beside it
   undoes nothing dangerous.
