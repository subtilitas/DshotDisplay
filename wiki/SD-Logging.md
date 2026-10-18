**English** | [Deutsch](SD-Aufzeichnung)

# SD Logging

The tester records telemetry to microSD in Betaflight blackbox format, at
500 frames per second. A frame averages 14.5 bytes (measured), which is
7.3 kB/s or 26 MB per hour.

<img src="img/log-screen.png" width="240" alt="The SD LOG screen while recording">

---

## The card

Use a microSD card formatted FAT32 (File Allocation Table). The card is
mounted once at boot, and neither board has a card-detect pin. Insert the card
before power-up, or press `RETRY` on the SD LOG screen after inserting it.

Files are written to the card root as `LOG00001.BFL`, `LOG00002.BFL`, and so
on. Each new file takes the lowest unused number from 00001 to 09999. Existing
files are not overwritten.

The 2.0" board reads the card over SPI (Serial Peripheral Interface) at
12 MHz, the 2.8" over 4-bit SDIO (Secure Digital I/O) at 10 MHz.

---

## Recording

**Automatic:** arming starts a log, disarming stops it.

**Manual:** `START` on the SD LOG screen starts a log at any time. A log
started by hand is not stopped by a disarm. `STOP` ends it.

The tester screen shows the state in its top row: `REC` (red) while recording,
`SD` while idle, `SDERR` after a card error, `NOSD` with no card.

---

## Reading the screen

<img src="img/log-ready.png" width="240" alt="Card mounted, ready to record">

| Row | Meaning |
|---|---|
| **STATUS** | `NO CARD`, `READY`, `RECORDING` or `CARD ERROR` |
| **FILE** | The open file |
| **FRAMES** | Frames written so far |
| **WRITTEN** | Kilobytes written |
| **DROPPED FRAMES** | Frames the buffer could not hold. **Should be zero.** Red otherwise |
| **BUF PEAK** | Highest fill of the 8192-byte write buffer. Amber above 1/2, red above 3/4 |
| **WORST FLUSH** | The longest single write to the card, in ms. Amber above 50 ms |
| **CARD** | Card type and size, as the card reports them |
| **MOUNT** | The filesystem result code. `0 OK` means mounted |

`BUF PEAK` near the buffer size, or a `WORST FLUSH` longer than the buffer
covers (8192 bytes is about 1.1 s at 7.3 kB/s), means the card is too slow for
the buffer. `DROPPED FRAMES` above zero means the log already has gaps.

The usual cause is a slow card. Try another card first.

### No card

<img src="img/log-nocard.png" width="240" alt="No card">

`NO CARD` with `MOUNT 3 NOT READY` means nothing answered on the bus: no card,
or the card is not seated. `MOUNT 13 NO FILESYSTEM` with a size under `CARD`
means the card answers and the filesystem is the problem. Format it as FAT32.

### A card inserted after boot

<img src="img/log-mounted.png" width="240" alt="A card found by RETRY">

Press `RETRY`. Without it, a card inserted after power-up is not mounted.

---

## Getting the logs off without a card reader

<img src="img/log-usb-serving.png" width="240" alt="The card served as a USB drive">

`USB DRIVE` on the SD LOG screen presents the card to the computer on the USB
(Universal Serial Bus) cable as a removable drive. Copy the `.BFL` files off,
eject the drive on the computer, and the tester takes the card back. The
screen shows the amount read so far.

The board is a USB device already, for power and the serial port. The drive is
a second USB interface on the same cable.

**The drive is read-only.** The computer can copy files and cannot write. A
host filesystem driver therefore cannot change the card or the log numbering.
A "write protected" message from the computer is expected.

**The card is handed over, not shared.** FAT has no arbitration between two
writers. Before the drive appears the logger flushes, closes and unmounts, and
it does not touch the card until eject. Meanwhile the SD LOG screen shows the
handover in place of its counters.

Without a card, `USB DRIVE` is drawn unavailable and does nothing.

**With a card, the button refuses in two cases, and names the reason:**

<img src="img/log-usb-refused.png" width="240" alt="USB DRIVE refused while recording">

| Caption | What to do |
|---|---|
| `DISARM FIRST` | Disarm. The handover pauses the logger |
| `STOP RECORDING FIRST` | Stop the recording |

**Arming is blocked while the computer holds the card.** `BACK` leaves the
screen without cancelling the handover, so the tester screen is reachable, but
it does not arm until the card is ejected. A press that starts while the
computer holds the card does not arm, even after the eject. Press again.

Eject on the computer, or press `EJECT` on the screen. Both end the handover.

---

## Opening the logs

<a href="https://subtilitas.github.io/logwiju/"><img src="https://img.shields.io/badge/open%20your%20logs%20in-logwiju-07b0c8?style=for-the-badge" alt="Open your logs in logwiju"></a>

### [logwiju](https://subtilitas.github.io/logwiju/) — the intended viewer

logwiju runs in the browser with no install. The file is read locally and not
uploaded. Drop a `.BFL` file on the page to plot the fields against time.

### Betaflight Blackbox Explorer

The files are Betaflight blackbox logs, so
[Blackbox Explorer](https://blackbox.betaflight.com/) and the
`blackbox_decode` command-line tool open them too. Some field names come from
flight controllers, for example `motor[0]` for the throttle.

---

## What is in a log

Fields per frame:

| Field | Content |
|---|---|
| `loopIteration`, `time` | Frame counter and time |
| `motor[0]` | Commanded throttle |
| `eRPM[0]` | eRPM (electrical RPM) from bidirectional DShot |
| `eRPMkiss[0]` | eRPM from KISS |
| `vbatLatest`, `amperageLatest` | Voltage and current from KISS; 0 while KISS is not live |
| `vbatEdt`, `amperageEdt` | Voltage and current from EDT (Extended DShot Telemetry) |
| `escTemperature[0]` | ESC (electronic speed controller) temperature |
| `escConsumption` | Consumed mAh (KISS only) |
| `escStress` | EDT stress value |

The ESC status byte, the arm state and the link counters are not logged.

With a KISS wire fitted, both sources are recorded separately, so a log can
check one source against the other.

---

## If the card is not the problem

Logging is compiled in by default. A firmware built with `SD_LOG_ENABLE=0`
still shows the SD LOG screen, and it always reports `NO CARD`.
