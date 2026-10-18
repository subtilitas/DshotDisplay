**English** | [Deutsch](Die-Bildschirme)

# The Screens

The firmware has five screens. The tester is the root. The others are reached
through `CFG` and left with `BACK`.

```
Tester ──CFG──> Settings ──┬── AM32     ──BACK──> Settings
                           ├── SD LOG   ──BACK──> Settings
                           └── SETUP    ──BACK──> Settings
                     └─────BACK──> Tester
```

**BACK is at the top right**, in the header band, on every screen except the
tester. The tester is the root and has no header.

---

## The tester

<img src="img/tester-armed.png" width="240" alt="The tester screen">

**Top row.** From left to right:

- the arm badge: `SAFE` (green) or `ARMED` (red);
- `DS` and the DShot bitrate in kbit/s, as set under SETUP, cyan once an EDT
  (Extended DShot Telemetry) enable has been sent. `DSERR` in amber replaces
  it when the display could not set up its DShot output; nothing is sent to
  the ESC (electronic speed controller) then;
- the recording state: `REC`, `SD`, `SDERR` or `NOSD`
  (see [SD Logging](SD-Logging));
- the board's own battery voltage, read through its 200k/100k divider. With
  only USB (Universal Serial Bus) power connected, what this reads is not
  measured. During the last 5 s before the idle interlock disarms, `IDLE ns`
  in amber replaces the voltage.

**RPM.** Mechanical RPM (revolutions per minute), computed from the eRPM
(electrical RPM) the ESC reports and the pole
count. The `ERPM` value and the pole count are shown below it. Green digits:
eRPM arrived within the last 500 ms. Dark red zero: it did not, and the line
reads `NO TELEMETRY`.

**Six tiles.** Voltage, current, ESC temperature, stress, ESC status and link
quality. See [Telemetry](Telemetry).

**Throttle.** The gauge, the ceiling and the commanded percentage. The
percentage is of the full DShot range (0–2000 = 0–100 %): at a 20 % ceiling, a
full-width drag reads `20%`. While disarmed the bar reads `ARM TO ENABLE`.

**Buttons.** `HOLD TO ARM` / `DISARM`, `HOLD` (latching throttle), `CFG`.

---

## Settings

<img src="img/settings.png" width="240" alt="The settings screen">

Reached with `CFG`, which **disarms**.

- **EDT ON / EDT OFF**: a read-only chip under the title. Green while Extended
  DShot Telemetry frames arrive, red otherwise. There is no enable button. The
  firmware sends the enable to every ESC that starts answering, and repeats it
  every 1 s while eRPM arrives and EDT does not. The ESC does not acknowledge
  the enable, so the chip shows *received frames*, not sent commands.
- **MOTOR POLES** and **THROTTLE CEILING**: steppers; hold to repeat.
- **BEEP**: sends the DShot beacon command, so the motor beeps. Use it to
  identify which ESC is connected. It works only while disarmed. The caption
  reads `BEEP NEEDS THE ESC DISARMED`, and `REFUSED - DISARM THE ESC FIRST`
  after a refused press.
- **AM32 / SD LOG / SETUP**: the three sub-screens.

`UNSAVED` appears under the title when a value here or on SETUP differs from
flash. The save button is on SETUP.

<img src="img/settings-unsaved.png" width="240" alt="UNSAVED, with changes not yet written to flash">

The command takes 6 ms (6 frames at 1 kHz). The screen repaints every 25 ms,
so the firmware highlights the button for 350 ms after each press: white when
sent, amber when refused.

<img src="img/config-beep-flash.png" width="240" alt="BEEP accepted"> <img src="img/config-beep-refused.png" width="240" alt="BEEP refused because the ESC is armed">

---

## SETUP

<img src="img/setup.png" width="240" alt="The SETUP screen">

Wiring and display settings, and the button that writes all settings to
flash.

| Row | What it does |
|---|---|
| **BOARD** | Read-only. The board the boot probe detected |
| **ESC PIN** | GPIO (general-purpose input/output pin) for the signal wire. Only this board's free GPIOs are offered: GP0–GP11 and GP21–GP23 on the 2.0", GP28 and GP29 on the 2.8" |
| **DSHOT KBAUD** | 150 / 300 / 600 / 1200. Lower it if the `LINK` error rate rises |
| **KISS TELEM** | Whether to listen on the optional third telemetry wire |
| **KISS PIN** | GPIO for that wire. Skips the ESC pin, so the two never coincide |
| **CONTRAST** | `NORMAL` or `HIGH`. High contrast is for daylight |
| **BACKLIGHT** | 16–255, in steps of 32, held at 16 and 255 |

**LINK** below the rows is live: packets per second and error rate. Green
means packets are arriving, so the ESC pin is correct. Amber means more than
5 % errors.

**Changes apply at once. Only storing them needs the hold.** A new bitrate
rebuilds the DShot driver before the next frame. `HOLD TO SAVE` for 1 s writes
to flash. `RESET` loads the compiled defaults into the live settings only.
Leaving without saving keeps flash unchanged.

The note line at the bottom says which set is in use: `SAVED SETTINGS IN USE`
or `COMPILED DEFAULTS IN USE`. After a change it reads
`UNSAVED - HOLD SAVE TO KEEP`.

> **The board is detected at every boot, not chosen.** No stored setting can
> override the detection, so a wrong value cannot stop the display from
> starting.

<img src="img/setup-contrast.png" width="240" alt="SETUP in high contrast"> <img src="img/tester-contrast.png" width="240" alt="The tester screen in high contrast">

*High contrast applies to every screen and sets the backlight to 255 while
it is on.*

---

## SD LOG

<img src="img/log-ready.png" width="240" alt="The SD LOG screen, card ready">

Recording state and counters. See [SD Logging](SD-Logging).

---

## AM32

<img src="img/am32-list.png" width="240" alt="The AM32 settings list">

Reads, edits and writes the settings of an AM32 ESC over the signal wire.
See [AM32 Configuration](AM32-Configuration).

---

## Things every screen does the same way

- **A tap acts on release, not on touch.** Sliding off a button before release
  cancels it. `DISARM` is the exception and acts on touch.
- **A pressed button shows a pressed outline.** A control that shows no outline
  did not register the touch.
- **`-` / `+` repeat while held, and speed up.** One step on press; after
  400 ms a step every 120 ms; after 1.2 s every 60 ms; after 2.5 s every
  25 ms.
- **Writing to flash needs a 1 s hold** with a progress bar: saving settings to
  the board, and writing settings to an ESC.
