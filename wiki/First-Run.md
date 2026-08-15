**English** | [Deutsch](Erste-Schritte)

# First Run

Two wires, one battery, and about five minutes.

---

## Safety first

This board's whole job is to make a motor spin. Everything below assumes you
have already done these three things.

- **Take the propeller off.** Not "point it away". Off.
- **Secure the motor.** A motor that is not bolted to something becomes a
  thrown object at about 30 % throttle.
- **Keep the ESC on its own battery.** Do not try to power a motor from the
  board's 5 V pin.

The firmware helps, but it cannot help with any of the above:

- The throttle ceiling defaults to **20 %**, so the first thing you do cannot be
  full throttle. Raise it once you know what is spinning.
- Arming takes a deliberate **one-second hold**, and the throttle must have been
  at zero throughout.
- Disarming is **instant on press** — the one control that does not wait for you
  to lift your finger.
- If nothing touches the screen for **30 seconds** while armed, it disarms
  itself, counting down out loud for the last five.
- If the display firmware ever hangs, the second core stops believing the last
  throttle it was given within a quarter of a second and commands zero.

---

## Wiring the ESC

Two wires. Signal and ground.

| | 2.0" board | 2.8" board |
|---|---|---|
| **Signal** | GP4 — P2 header, pin 11 | GP29 — J4 pin 12 |
| **Ground** | GND — P2 header, pin 13 | GND on J4 |

Both are changeable later from **CFG → SETUP** on the board itself, and the
choice is remembered.

> **Cut or de-pin the middle wire of the ESC lead first.**
> The middle conductor of a standard three-wire servo plug is the ESC's **+5 V
> BEC output**. The RP2350's pins are **not** 5 V tolerant — anything above
> about 3.6 V is over absolute-maximum and damages the chip. The tester needs
> signal and ground and nothing else.

On the 2.0" you can solder a 3-pin male header onto **P2 pins 11–13** and a
servo plug drops straight on: signal on GP4, the (removed) middle wire over
GP10, ground on pin 13.

Keep the signal wire short. Bidirectional DShot at 600 kBaud does not enjoy
30 cm of unshielded flapping servo lead — if telemetry is unreliable, drop the
bitrate to 300 in SETUP before suspecting anything else.

---

## Powering up

Power the board over USB-C, and the ESC from its own battery. Order does not
matter: the firmware keeps asking a newly-arrived ESC for telemetry, so
connecting or power-cycling the ESC at any point is fine.

You will see the splash screen name the board it detected, then the tester
screen.

<img src="img/splash.png" width="240" alt="Splash screen">

---

## Before an ESC is connected

<img src="img/tester-disarmed.png" width="240" alt="The tester screen with no ESC connected">

`NO TELEMETRY` under the RPM readout and `--` in every tile is the correct
display for "nothing is answering". The RPM digits sit dark rather than showing
a confident zero, because a zero would be a claim.

If the ESC is powered and wired and this does not change within a second or
two, go to [Troubleshooting](Troubleshooting#no-telemetry).

---

## Your first arm

1. Check the propeller is off. Yes, again.
2. Press and **hold** `HOLD TO ARM` for one second. A green bar fills across the
   badge as you hold.
3. The badge turns red and reads `ARMED`, and the whole panel dips briefly —
   that flash is deliberate, so that arming registers in the corner of your eye
   while you are looking at the motor rather than the screen.

<img src="img/tester-armed.png" width="240" alt="Armed, with telemetry arriving">

To stop: press `DISARM`. It acts the moment you touch it.

---

## Giving it throttle

Two surfaces, and they feed the same value.

**The bar at the bottom** is a spring-loaded trigger. Where your finger is along
the track is the throttle, and it snaps back to zero the instant you let go.
The full width of the track is the *ceiling*, not full throttle — so at the
default 20 % ceiling, dragging to the far right commands 20 %.

**The big number area** is a large relative throttle pad. Swipe **up** for more,
**down** for less. It is relative, so a tap does nothing and swipes accumulate:
you can build throttle over several short swipes without looking at the screen.
`SWIPE ACTIVE` appears once a swipe has passed the small dead zone and is
actually moving the motor.

`HOLD` changes the bar from spring-loaded to latching, so the throttle stays
where you put it instead of returning to zero. The bar grows a grab handle to
say so. Turning `HOLD` back off zeroes the throttle.

---

## Raising the ceiling

`CFG` opens the settings screen — and force-disarms on the way in, every time.

<img src="img/settings.png" width="240" alt="The settings screen">

- **MOTOR POLES** — how the eRPM the ESC reports becomes the RPM shown. Almost
  every quadcopter motor is **14**. Get this wrong and the RPM number is wrong
  by a fixed ratio while everything else stays correct.
- **THROTTLE CEILING** — raise it when you know what is spinning. Hold `-` or `+`
  to repeat, and it accelerates.

Both are remembered only once you save them, and the button that saves is on
**SETUP**. `UNSAVED` under the title says so.

---

## Next

- [The Screens](The-Screens) — the rest of the interface
- [Telemetry](Telemetry) — what the tiles are telling you
- [SD Logging](SD-Logging) — recording a run and plotting it
