**English** | [Deutsch](AM32-Konfiguration)

# AM32 Configuration

This screen reads, changes and writes the settings of an AM32 ESC (electronic
speed controller) over the signal wire. It needs no laptop or configurator. It
does not update ESC firmware.

<img src="img/am32-list.png" width="240" alt="The AM32 settings list">

> **This writes to your ESC's flash.** Changes stay on the tester until you
> hold `HOLD TO WRITE` for 1 s. After that they are in the ESC.

---

## Connecting

Open `CFG → AM32`.

The screen hands the signal pin from the DShot driver to the bootloader link
(19200 baud, one wire, half-duplex), then starts searching.

**Then power-cycle the ESC.** The AM32 bootloader listens only in a short
window after power-up. Unplug the ESC battery and plug it back in while this
screen is open. The firmware repeats its handshake, waiting 30 ms for each
reply, so that it is sending when the window opens. The window's length is not
measured.

The screen shows `SEARCHING...`, then `WAITING... (TRY n)` with an attempt
counter, and a spinner. On success the list appears, and the strip under the
title shows the ESC name, MCU (microcontroller) family and firmware revision.

If bytes arrive that do not parse, the screen shows `REPLY NOT UNDERSTOOD:`,
the byte count, the first three bytes and the baud rate, and
`LINK WORKS BUT FRAMING IS OFF.` That means the wire works and the framing or
baud rate does not match.

---

## Browsing

Drag up or down to scroll. The list follows the finger 1:1 once the drag is
recognised. A drag never selects a row.

<img src="img/am32-scrolled.png" width="240" alt="The list mid-scroll">

A **tap without movement** selects the row under it. The editor bar at the
bottom then shows the field's name and value.

Rows are grouped, with the group name in grey above each group. The available
fields depend on the ESC's settings layout revision. Only fields that apply to
the revision read are shown.

---

## Editing

<img src="img/am32-edit.png" width="240" alt="Editing a field">

| Gesture | Effect |
|---|---|
| Tap a row | Select it |
| `-` / `+` in the editor bar | **Fine**: one step per press. Hold to repeat, speeding up |
| Swipe a row **sideways** | **Coarse**: 180 px of travel covers the field's whole range |
| Drag **up / down** | Scroll |

A gesture is either a scroll or an edit. The axis is fixed after 6 px of
travel.

Coarse swipes snap to each field's step, so values stay legal; motor poles
stay even, for example. At the end of the range the swipe re-anchors, so a
small swipe back responds at once.

**Changed rows are marked amber on the left edge**, and their values are
amber. Nothing has been sent to the ESC yet.

`REVERT` discards all unsaved changes and restores the values that were read.

---

## Writing

<img src="img/am32-written.png" width="240" alt="Write verified">

Hold `HOLD TO WRITE` for 1 s. A progress bar fills while you hold. With no
changes the button reads `NO CHANGES`.

After the write the firmware waits 300 ms, **reads the settings back and
compares them** byte by byte. It does not rely on the ESC's acknowledgement.
A match shows `WRITE VERIFIED`. A mismatch shows the first differing byte as
`DIFF @xx: GOT xx WANT xx`.

Most AM32 settings take effect at the next ESC power-up. This is AM32
behaviour; the tester does not check it.

---

## The hex view

<img src="img/am32-hex.png" width="240" alt="The raw settings bytes">

`HEX` shows the raw 48 settings bytes, read-only. Use it to check a field that
your layout revision does not offer, or to see what a change did at byte
level. `FIELDS` returns to the list.

---

## Leaving

`BACK` hands the signal pin back to the DShot driver and returns to the
settings screen. The tester is disarmed.

---

## When it will not connect

- **`SEARCHING...` or `WAITING...` with no end.** Unplug and replug the ESC
  battery *while the screen is open*. A power-cycle before opening the screen
  does not work.
- **`REPLY NOT UNDERSTOOD`.** The link works; the framing does not. Possible
  causes: the ESC does not run AM32, or its bootloader uses a protocol this
  transport does not implement.
- **`NO VALID SETTINGS`.** The ESC answered, and the settings block failed the
  plausibility check. Do not write anything.

More in [Troubleshooting](Troubleshooting#am32-will-not-connect).
