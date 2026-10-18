**English** | [Deutsch](Erste-Schritte)

# First Run

This page covers wiring an ESC (electronic speed controller), powering up, and
the first arm. You need two wires and a battery for the ESC.

---

## Safety first

The board makes a motor spin. Do these three things before anything else:

- **Remove the propeller.**
- **Secure the motor.** An unsecured motor can move or jump when it spins up.
- **Power the ESC from its own battery.** Do not power a motor from the
  board's 5 V pin.

The firmware has these interlocks. They do not replace the steps above:

- The throttle ceiling defaults to **20 %** of the DShot range.
- Arming needs a **1 s hold**, with the throttle at zero for at least 250 ms.
- `DISARM` acts **on touch**. Every other control acts on release.
- While armed, **30 s** without a touch disarms the tester. The last 5 s count
  down on screen.
- If the display firmware stops, the second core sets the throttle to zero
  after **250 ms** without a heartbeat.

---

## Wiring the ESC

Two wires: signal and ground.

| | 2.0" board | 2.8" board |
|---|---|---|
| **Signal** | GP4, P2 header pin 11 | GP29, J4 pin 12 |
| **Ground** | GND, P2 header pin 13 | GND, J4 pin 1 or 5 |

GP means GPIO (general-purpose input/output pin). The ESC pin can be changed
later under **CFG → SETUP**, and the setting is stored in flash.

> **Cut or de-pin the middle wire of the ESC lead first.**
> The middle conductor of a 3-wire servo plug is the ESC's **+5 V BEC
> (battery eliminator circuit) output**. RP2350 pins are **not** 5 V tolerant.
> More than about 3.6 V exceeds the absolute maximum rating and damages the
> chip. The tester needs signal and ground only.

On the 2.0", a 3-pin male header soldered onto **P2 pins 11–13** takes a servo
plug directly: signal on GP4, the removed middle wire over GP10, ground on
pin 13.

Keep the signal wire short. The maximum working length is not measured. If the
`LINK` error rate rises, set **DSHOT KBAUD** to 300 in SETUP.

---

## Powering up

Power the board over USB-C (USB: Universal Serial Bus) and the ESC from its
own battery. The order does not matter. The firmware sends the telemetry
enable to every ESC that starts answering, so the ESC can be connected or
power-cycled at any time.

The splash screen names the detected board for 3 s, then the tester screen
appears.

<img src="img/splash.png" width="240" alt="Splash screen">

---

## Before an ESC is connected

<img src="img/tester-disarmed.png" width="240" alt="The tester screen with no ESC connected">

With no ESC answering, the screen shows `NO TELEMETRY` under the RPM
(revolutions per minute) readout, `--` in every tile, and a dark red zero for
RPM.

If the ESC is powered and wired and this does not change, see
[Troubleshooting](Troubleshooting#no-telemetry).

---

## Your first arm

1. Check that the propeller is off.
2. Press and **hold** `HOLD TO ARM` for 1 s. A green bar fills along the bottom of
   the badge while you hold.
3. The badge turns red and reads `ARMED`. The backlight dims to a quarter for
   120 ms, so the event is visible while you watch the motor.

<img src="img/tester-armed.png" width="240" alt="Armed, with telemetry arriving">

To stop, press `DISARM`. It acts on touch. The press that disarms, kept down,
does not arm again: arming needs a new press that starts on the button.

---

## Giving it throttle

Two surfaces set the same throttle value.

**The bar at the bottom.** With `HOLD` off, the finger position along the
track is the throttle, and the throttle returns to zero on release. The full
track width is the *ceiling*: at the default 20 % ceiling, the right end
commands 20 %.

**The number area** above the bar is a relative throttle pad. Swipe **up** for
more, **down** for less. A tap does nothing, and swipes add up, so the
throttle can be raised over several short swipes without looking. After 6 px
of travel the hint under the RPM readout changes to `SWIPE ACTIVE` and the
swipe moves the throttle.

`HOLD` makes the bar relative and latching: the throttle stays where you leave
it. The bar shows a grab handle. Turning `HOLD` off sets the throttle to zero.

---

## Raising the ceiling

`CFG` opens the settings screen. Opening it always disarms.

<img src="img/settings.png" width="240" alt="The settings screen">

- **MOTOR POLES**: converts the reported eRPM (electrical RPM) to RPM with
  `RPM = eRPM / (poles / 2)`. Range 2–28, even values only. Default 14. A
  wrong value scales the RPM reading by a fixed factor; other readings are
  unaffected.
- **THROTTLE CEILING**: 5–100 % in 5 % steps. Hold `-` or `+` to repeat; the
  repeat speeds up the longer you hold.

Both values are stored only when saved, with `HOLD TO SAVE` on **SETUP**.
`UNSAVED` under the title shows unsaved changes.

---

## Next

- [The Screens](The-Screens): the rest of the interface
- [Telemetry](Telemetry): what the tiles show
- [SD Logging](SD-Logging): recording a run and plotting it
