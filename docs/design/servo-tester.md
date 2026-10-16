# Servo tester

Status: planned. Nothing here is built. It depends on the pin claim in
[pin-claim.md](pin-claim.md), slice 1 there; this document is slice 2.

Terms: PWM is pulse-width modulation, here the RP2350's hardware PWM slices.
A servo pulse is a high level of 400 µs to 2500 µs repeated at a frame rate.
ESC is electronic speed controller, DShot the digital ESC protocol the tester
drives, UI user interface, SD Secure Digital. SIG and AUX are the two signal
pins as [pin-claim.md](pin-claim.md) defines them: the ESC pin and the KISS
telemetry pin.

Source: the interaction and the limits come from rcbench, commit 453cf334:
`shared/ui/servo_screen.c`, `shared/ui/ui_slider.c`,
`shared/servo/servo_sweep.c`, `firmware/iomcu/src/out_pwm.c`,
`shared/outputs/out_pwm_map.c` and `shared/outputs/outputs.h`. rcbench is MIT
licensed by the same holder.

## What it does

- Drives one servo on SIG, or two servos on SIG and AUX, with standard servo
  pulses from the hardware PWM.
- Manual position: drag the drawn horn; MIN, CENTRE and MAX buttons; a pulse
  range and a frame rate set on the SET screen.
- Sweep: square, sine or triangle about the centre, with rate, amplitude,
  dwell and an optional count of movements (`servo_sweep.c`).
- SPEED: a slew limit on every movement, from 10 % to 100 %.

It does not measure the servo. There is no position feedback, so the horn is
drawn where the pulse puts it, not where the servo is.

## Safety model: SAFE and RUNNING

A servo that jumps to a position is a hazard on a mounted linkage, the same
way a throttle step is on a motor. The screen has the motor screen's two
states under other names.

| | Motor (main screen) | Servo |
|---|---|---|
| Off state | DISARMED: throttle-zero frames | SAFE: the pins bound to PWM at level 0, so no edges and the line low |
| Starting | HOLD TO ARM, `ARM_HOLD_MS` 1000 ms, throttle at zero for `ARM_ZERO_THROTTLE_MS` | HOLD TO START, `ARM_HOLD_MS` 1000 ms, with the progress bar AM32's HOLD TO WRITE draws |
| First output | throttle 0 | the CENTRE pulse on every bound pin, whatever was commanded before |
| Stopping | DISARM, on press, instant | STOP, on press, instant: level 0 from the next frame |
| UI stalls 250 ms | throttle forced to zero | level 0, sweep ends, `Tripped` latched |
| Idle | auto-disarm after `IDLE_DISARM_MS`, 30 s | none: a sweep may run unattended as an endurance test |

rcbench does the same: until the bench is ARMED the coprocessor writes a
pulse of length zero to every PWM pin, ARM is a hold, and at the arm every
surface goes to its rest, the centre. rcbench holds ARM for 2 s; this screen
uses DshotDisplay's 1 s so that every hold on the device is one length (open
question 1).

STOP writes level 0 at once, and the compare register is double-buffered, so a
pulse already on the wire completes. The last pulse ends at most one frame
later: 20 ms at 50 Hz.

The text under HOLD TO START says `THE SERVO MOVES TO CENTRE AT START.`
Starting at the last commanded position instead would move the servo just as
far from wherever it rests, to a place nobody chose this session; centre is
the position a servo is mounted and trimmed at.

What a servo does with no pulses is the servo's own: hold, go limp, or move
to a failsafe position. It is not recorded per servo.

## Pulse limits

All of these are pure functions in `src/servo_model.cpp` and host-tested.

| Rule | Value | Source |
|---|---|---|
| Pulse floor and ceiling | 400 µs and 2500 µs, refused outside | `OUT_FLOOR_US`, `OUT_CEILING_US` |
| Frame rate | 40 Hz to 560 Hz | rcbench PWM driver table: below 40 Hz a servo steps audibly; 560 Hz is a 760 µs tail servo's rate |
| Pause after a pulse | 1000 µs, 500 µs for the heli profiles | `PWM_MIN_PAUSE_US`, `HELI_MIN_PAUSE_US` |
| Fastest rate for a range | `1,000,000 / (MAX + pause)`, under the profile's own ceiling | `max_rate_for()`: 333 Hz for 2000 µs |
| Pulse against the frame | at most period − 1 µs: 1784 µs at 560 Hz, 2499 µs at 400 Hz | `out_pwm_pulse_max()` |
| Order | MIN < CENTRE < MAX | |
| Warning before applying | a heli profile, or a rate above 60 Hz | `SAFE_RATE_HZ`: an analogue servo overheats |

The profiles are rcbench's:

| TYPE | MIN | CENTRE | MAX | Rates offered | Default | Ceiling |
|---|---|---|---|---|---|---|
| STANDARD PWM | 1000 µs | 1500 µs | 2000 µs | 50, 60, 100, 150, 200, 250, 300, 333 Hz | 50 Hz | pause rule |
| NARROW 760 | 660 µs | 760 µs | 860 µs | as STANDARD | 50 Hz | pause rule |
| WIDE | 800 µs | 1500 µs | 2200 µs | as STANDARD | 50 Hz | pause rule |
| HELI CYCLIC | 820 µs | 1520 µs | 2220 µs | 50, 120, 200, 333 Hz | 333 Hz | 333 Hz |
| HELI TAIL 760 | 410 µs | 760 µs | 1110 µs | 200, 333, 560 Hz | 560 Hz | 560 Hz |

Steppers only reach legal values, as SETUP's pin steppers only reach free pins:
MIN, CENTRE and MAX move in 10 µs steps and stop at the floor, the ceiling and
each other; RATE steps through the offered rates up to the ceiling the range
allows. There is no invalid value to reject.

Positions are commands from 0 to 1000 with the centre at 500, as in rcbench
(`OUT_SPAN`). A command maps onto the narrowest range centred on CENTRE that
holds MIN and MAX, clamped to MIN..MAX (`cmd_range()`), so an asymmetric
CENTRE rests the servo at CENTRE. On the dial, −90° is MIN and +90° is MAX,
each side scaled to its own half of the travel (`deg_to_us()`).

SPEED runs from 10 % to 100 %. Below 100 % the pulse slews at
`2 × 1000 × SPEED / 100` command units a second: at 10 % a full MIN to MAX
travel takes 5 s. 100 % is no slew, rcbench's default, so a user who never
touches SPEED gets the servo at its own rate. The SET screen says
`SPEED LIMITS THE SWEEP` when `sweep_slew_limited()` is true.

## The horn is the control

rcbench `servo_screen.c`: "The horn is the control: dragging the drawn arm
commands the servo." The screen keeps that interaction and its rules.

- A press on the dial commands the angle under the finger, and every move
  after it commands the new angle. It is absolute: the servo goes where the
  finger is.
- On release the servo holds the last position. There is no spring.
- A press on the dial while SAFE is refused and takes no drag. A servo started
  under a finger moves nothing until the next press (rcbench: "the press is
  the dial's and takes no drag").
- A drag under way at START or STOP ends. After the edge the first input is a
  new press (rcbench `servo_screen_set_armed()`: "A finger on the dial does not
  carry its drag across the edge").
- A drag, MIN, CENTRE, MAX, STOP, BACK, a heartbeat trip and a profile change
  each end a running sweep (rcbench `stop_sweep()`: "a drag, CENTRE, RELEASE,
  a curve restarted, a disarm, leaving, the link going").
- A press that begins off the dial never becomes a drag, however far it
  travels across it (`ui_slider.h`: "a press that starts anywhere else never
  becomes a drag"). `TouchState.downX` and `downY` give the press origin.
- A press inside the hit area but past ±90° commands the end of travel. The
  arc's ends saturate, which is the dial's version of the throttle track's
  `THR_EDGE_SAT`: both rails are reachable without reaching the rim.

### Against the throttle drag

| | Throttle gauge (`ui.cpp`) | Servo horn |
|---|---|---|
| Surface | track 224 × 34 px at y 248, 8 px slop above, 1 px below | annulus around the shaft, 24 px to 124 px radius, ±104° of straight up, clipped to the dial band |
| Spring mode | absolute: finger position is throttle, back to 0 on release | none |
| HOLD mode | relative, by distance dragged (`HOLD_DRAG_SENSITIVITY_PCT`), so a touch never jumps the motor | always absolute; the release holds |
| Large pad | relative swipe with a 6 px deadzone (`PAD_DEADZONE_PX`) | none |
| Edge | 16 px saturation at each end (`THR_EDGE_SAT`) | ±90° to ±104° saturates at MIN or MAX |
| Jump limit | relative modes, ceiling (`maxThrottle`) | SPEED slew, pulse range |
| Gate | armed | RUNNING |

The horn is absolute on purpose. A servo's position is the thing being
tested, and a position control that does not put the servo where the finger
is would not say what the servo does. The jump that absolute input allows is
bounded by the pulse range and slowed by SPEED.

### Two servos

Two horns share one dial: A in `C_LIME`, B in `C_CYAN`. A selector of three
buttons, `A`, `B` and `AB`, sits in the readout row. The selection decides
what the drag, MIN, CENTRE, MAX and SWEEP drive:

- `A` or `B`: that servo follows; the other holds its pulse.
- `AB`: both follow to the same angle. REVERSE B on the SET screen mirrors B,
  for a pair mounted face to face.

The selected horn is drawn with its grip and full width, the other at 1 px.
The touch drivers report one contact (`TouchState`, on both the CST816D and
the CST328), so one finger drives one selection; there is no second drag.
START starts both pins at CENTRE. STOP stops both.

Both servos share one profile and one frame rate. With the default pins they
are one PWM slice on both boards (GP4 and GP5 are slice 2, GP28 and GP29 slice
6), which forces one rate anyway.

## Screens

Entry: CFG → SETTINGS → SERVO, in the destination row
([pin-claim.md](pin-claim.md), open question 2). The main screen is the root
and gets no entry; CFG is its navigation.

### SERVO

```
y   0 +------------------------------------------+
      | SERVO                            [ BACK ]|  header, UI_HDR_H 30 px
   34 | GP29  50 HZ  1000-2000 US          (SAFE)|  strip: caption, chip
   56 |                                          |
      |               .   -   -   .              |
      |          .          |          .         |  dial band 56..199
      |       .             |             .      |  arc radius 96 px
      |     .               |               .    |  shaft at (120, 184)
      |    MIN              o              MAX   |  0 deg is straight up
  184 |                  (shaft)                 |
  200 |               1500 US                    |  readout, scale 2
  226 | SPEED [=======O-----------------]  100%  |  slider, track 22 px
  254 | [ MIN ] [CENTRE] [ MAX ] [ SWEEP ]       |  4 x 53 px, 30 px tall
  287 | [   HOLD TO START    ]       [  SET  ]   |  150 px and 72 px, 32 px tall
  320 +------------------------------------------+
```

Two-servo readout row, 200..223:

```
  200 | A 1500  B 1620              [A] [B] [AB] |  34 x 22 px each
```

- Header and BACK are `uiHeader()`. The strip carries the live pin, the rate
  and the range as a caption, and a chip: `SAFE` in `C_GREEN`, `RUNNING` in
  `C_RED`, `STALLED` in `C_MAGENTA` after a heartbeat trip.
- The dial: arc, MIN and MAX ticks, and the horn at the pulse core1 reports,
  so a slewed move is drawn as it happens.
- The readout: the pulse in µs; while sweeping, `SWEEP 3/10` or `SWEEP 3` on
  the right.
- SPEED: the rcbench slider with tap-to-set, because SPEED commands nothing by
  itself (`servo_screen.c` turns it on for the same reason).
- MIN, CENTRE, MAX fire on release, like every DshotDisplay button, and are
  drawn disabled while SAFE. SWEEP reads `SWEEP`, or `STOP SWEEP` while one
  runs; a stopped sweep leaves the servo where the curve was. A sweep starts
  its curve at the centre, so a servo held elsewhere moves to the centre
  first, at SPEED.
- HOLD TO START becomes `STOP` while RUNNING, in `C_RED`, and acts on press.
  The bottom-left position is the main screen's ARM position.
- SET opens the SET screen. The claim and the pulses continue under it.

Layout constants get `static_assert`s for every gap, as `ui.cpp` and
`ui_am32.cpp` have, and every button clears `UI_TAP_MIN`, 22 px.

While the pin is unavailable, the dial band holds the message from
[pin-claim.md](pin-claim.md) and HOLD TO START is drawn disabled.

### SET

The AM32 list idiom: a scrolling list of rows (26 px), an editor bar with
large − and + (hold to repeat, `repeatFires()`) and a horizontal swipe for
coarse steps, and a footer.

```
y   0 | SERVO SET                        [ BACK ]|
   34 | STOP TO CHANGE THE PROFILE               |  strip, only while RUNNING
   56 | PULSE   SERVOS                      ONE  |
      | PULSE   TYPE               STANDARD PWM  |
      | PULSE   RATE                      50 HZ  |
      | PULSE   MIN                     1000 US  |
      | PULSE   CENTRE                  1500 US  |
      | PULSE   MAX                     2000 US  |  list 56..215, scrolls
      | PULSE   REVERSE B                    OFF |
      | SWEEP   SHAPE                       SINE |
      | SWEEP   RATE                     0.50 HZ |
      | SWEEP   AMPLITUDE                  100 % |
      | SWEEP   DWELL                     500 MS |
      | SWEEP   MOVES                    ENDLESS |
  218 | [ - ]           RATE 50 HZ           [ + ]|  editor bar, 44 px
  268 | [  HOLD TO APPLY  ]      [ DEFAULTS ]    |  footer, 46 px
```

- PULSE rows change only while SAFE. A new rate needs the PWM slice rebuilt
  (`out_pwm_bind()` refuses a second rate on a bound pin), and a range changed
  under a moving servo moves it.
- SWEEP rows change at any time and apply to the next SWEEP. Ranges: SHAPE
  square, sine, triangle; RATE 0.05 Hz to 5.00 Hz (`SWEEP_MHZ_MIN`,
  `SWEEP_MHZ_MAX`); AMPLITUDE 0 % to 100 % of half the travel (0 to
  `SWEEP_AMPLITUDE_MAX`, 500 units); DWELL 0 ms to 5000 ms; MOVES endless or 1
  to 9999.
- A change that needs the warning (a heli TYPE, a RATE above 60 Hz) is held as
  pending. The strip names the risk, `ABOVE 60 HZ AN ANALOGUE SERVO OVERHEATS.`
  or `A HELI PROFILE CAN DESTROY A SERVO NOT MADE FOR IT.`, and HOLD TO APPLY
  (1000 ms) applies it. Every other change applies at once.
- SERVOS TWO is drawn disabled, with the reason on the strip, when the claim
  refuses AUX (KISS live, shared compare register).
- DEFAULTS returns to STANDARD PWM at 50 Hz, one servo, sine at 0.50 Hz.

## Where it runs

Core0 owns the screen and sends commands. Core1 owns the pins
([pin-claim.md](pin-claim.md), "What core1 does").

```c
enum class ServoCmdKind : uint8_t { Safe, Start, Position, Sweep, SweepStop };

struct ServoProfile {
    uint16_t minUs, centreUs, maxUs;
    uint16_t rateHz;
    uint8_t  servos;      // 1 or 2
    bool     reverseB;
};

struct ServoCmd {
    uint16_t     seq;        // core1 acknowledges by echoing it
    ServoCmdKind kind;
    uint8_t      mask;       // bit 0 servo A, bit 1 servo B
    uint16_t     command;    // 0..1000, centre 500
    uint16_t     slewPerS;   // command units a second, 0 for none
    sweep_cfg_t  sweep;      // servo_sweep.h
};

struct ServoStatus {
    uint16_t ackSeq;
    bool     running, tripped, sweeping;
    uint8_t  bindError;      // 0, or why out_pwm_bind() refused
    uint16_t outUs[2];       // what each pin carries now, 0 for no pulse
    uint32_t sweepMoves;
};

void servoJobProfile(const ServoProfile *p);   // core0, only while SAFE
void servoJobCommand(const ServoCmd *c);       // core0
void servoJobStatus(ServoStatus *out);         // core0, under the job's lock
```

Every pass of `loop1()`, the job:

1. checks `core1UiAlive()`; on a lapse it writes level 0, ends the sweep and
   latches `tripped`;
2. takes a new command if `seq` moved;
3. computes the target: the sweep curve at `millis()`, or the position;
4. moves the output towards it by the slew owed since the last pass;
5. writes the compare register when the pulse changed (`out_pwm_write()`); the
   slice latches it at the end of the frame.

A drag reaches core1 at the UI rate, 40 Hz, one `Position` command per frame.
Core1 renders each at the next PWM frame and slews between them, so the
servo does not step at 40 Hz below 100 % SPEED.

Profile and bind: `servoJobProfile()` in SAFE releases and rebinds both pins
at the new rate. A refused bind leaves both pins unbound and reports
`bindError`.

## Porting

| rcbench file | Here | How |
|---|---|---|
| `shared/servo/servo_sweep.c`, `include/servo_sweep.h` | `lib/rcbench/shared/servo/` | copied as they are |
| `shared/outputs/out_pwm_map.c`, `include/out_pwm_map.h` | `lib/rcbench/shared/outputs/` | copied as they are |
| `firmware/iomcu/src/out_pwm.c`, `.h` | `src/servo_pwm.cpp`, `.h` | adapted: 2 bindings instead of the bank's slot count, `link_pages.h` and `outputs.h` dropped, the bind, release, write and sharing rules kept |
| `shared/ui/ui_slider.c`, `include/ui_slider.h` | `src/ui_slider.cpp`, `.h` | adapted: the event logic kept (delta drag, tap-to-set, presets, the two press contracts); `touch_event_t` replaced by `TouchState` (`pressed` is DOWN, `down` is MOVE, `released` is UP, one contact); drawing rewritten on `gfx.h` and the theme colours |
| `shared/ui/servo_screen.c` (6957 lines) | `src/servo_model.cpp` (pure) and `src/ui_servo.cpp` | rewritten. Taken: the profile table, `max_rate_for()`, `dangerous()`, `cmd_range()`, `deg_to_us()`, `us_to_deg()`, `slew_of()`, `on_the_dial()` rotated to point up, the drag and stop rules. Left out: the encoder feedback, the limit run, the automatic test, the supply and PD mini rows, the power plot, the keypad, the tabs, the pause and resume with its link lineage (core0 and core1 share memory, so no command is ever in flight long enough to need it) |
| `test/host/test_servo_sweep.c` | `test/rcbench/` | copied as it is, with rcbench's harness `greatest.h` |
| out_pwm_map cases of `test/host/test_outputs.c` | `test/rcbench/test_out_pwm_map.c` | adapted: the map cases only |

`gfx.cpp` gains a thick line and an arc of dots for the horn and the dial.
DshotDisplay draws only rectangles, rounded rectangles, lines along an axis,
text and seven-segment digits today.

Copied files keep their text and their `SPDX-License-Identifier: MIT` line.
`lib/rcbench/README.md` lists each copied file with its rcbench path and the
commit. Adapted files start with `Adapted from rcbench <path>, commit
453cf334, MIT.` Both projects are MIT under the same holder, so no licence
changes.

## Testing

Ported, built as C11 in `test/rcbench/` and run by `make`:
`test_servo_sweep.c`, the out_pwm_map cases.

New, `test/test_servo.cpp`:

- limits: each profile within 400 µs to 2500 µs; steppers stop at the floor,
  the ceiling and each other; the rate ceiling (333 Hz for 2000 µs plus
  1000 µs); the pulse against the frame (1784 µs at 560 Hz); the warning set;
- mapping: command to µs with a symmetric and an asymmetric CENTRE; ±90° to
  MIN and MAX; saturation from 90° to 104°; REVERSE B;
- hit test: inside the annulus, inside 24 px, outside 124 px, past ±104°,
  below the dial band, and a press that began off the dial;
- the job against a fake PWM: level 0 while SAFE; CENTRE on every bound pin at
  START; slew per SPEED; sweep start and stop; heartbeat trip latched and not
  cleared by the heartbeat coming back;
- the stop rules: a drag, MIN, CENTRE, MAX, STOP, BACK, a profile change and a
  trip each end a sweep; a drag across START and STOP ends; a press while SAFE
  takes no drag;
- two servos: the selector, `AB` with REVERSE B, refusal on each 2.0" pin pair
  that shares a compare register.

`test/test_ui.cpp`: the SERVO and SET screens render, the disabled states draw
disabled and do not act, HOLD TO START fires after 1000 ms and not on a slide
off, STOP fires on press. `make previews` adds `docs/servo-preview.png`.

## Work breakdown

1. `servo_model.*` and its tests. Pure; no hardware.
2. `servo_pwm.*`, `servo_job` in `core1_job.cpp`'s dispatch, the vendored
   sweep and map, the ported tests.
3. `gfx` line and arc, `ui_slider.*`.
4. `ui_servo.cpp`, SET, the CFG entry enabled, UI tests, preview.
5. Bring-up: a scope on SIG and AUX at 50 Hz and 560 Hz on both boards; one
   servo, two servos; STOP latency; heartbeat trip by halting core0 in a
   debugger.
6. README section "Servo tester" with the wiring diagram and the unplug
   warning; wiki pages `Servo-Tester.md` and its German counterpart, and
   their entries in `_Sidebar.md` and the screens pages.

About 5 days for 1 to 4.

## Open questions

1. **START hold length.** DshotDisplay's 1000 ms (`ARM_HOLD_MS`) or rcbench's
   2 s? Recommended: 1000 ms, one hold length on the device.
2. **Persist the profile.** Store TYPE, RATE and the range in flash?
   Recommended: no. Every boot starts at STANDARD PWM 50 Hz, as rcbench
   restarts do, so a heli profile or a high rate is never applied to the next
   servo by accident, and `SETTINGS_VERSION` stays 2.
3. **One profile or two.** Two servos of different types on one test?
   Recommended: one shared profile with REVERSE B. The default pins share a
   slice and therefore a rate on both boards; a second range is a second set
   of rows for a case nobody has asked for.
4. **Sweep pause.** rcbench pauses and resumes a sweep at its phase.
   Recommended: not in this slice. STOP SWEEP holds the servo where the curve
   was, and SWEEP starts a new curve from the centre.
5. **SPEED default.** rcbench starts at 100 %, no slew. Recommended: keep
   100 %; HOLD TO START already states the move to centre, and a slower
   default would make the servo look slow.
6. **SAFE output.** No pulses (rcbench), or the CENTRE pulse? Recommended: no
   pulses. A servo held at a pulse nobody commanded this session is worse than
   one let go; that is rcbench's rule for `out_pwm_write(pin, 0)`.
