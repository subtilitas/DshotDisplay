# DshotDisplay

A self-contained bidirectional DShot ESC tester for the Waveshare
**RP2350-Touch-LCD-2** and **RP2350-Touch-LCD-2.8**. Two wires to the ESC, and
the board becomes the throttle source, the telemetry display and the log
recorder. No flight controller, no laptop, no Betaflight.

<img src="img/tester-armed.png" width="240" alt="The tester screen, armed">

---

## English

**Start here:** [First Run](First-Run) — wiring, power, and the first time you
spin a motor.

| Page | What it covers |
|---|---|
| [First Run](First-Run) | Wiring an ESC, powering up, arming, your first throttle input |
| [The Screens](The-Screens) | Every screen and every control, in order |
| [Telemetry](Telemetry) | What each tile means, where the number came from, and why one reads `--` |
| [SD Logging](SD-Logging) | Recording to microSD and opening the result in [logwiju](https://subtilitas.github.io/logwiju/) |
| [AM32 Configuration](AM32-Configuration) | Reading, editing and writing ESC settings |
| [Troubleshooting](Troubleshooting) | Symptom first, cause second |

> **Before you power anything on:** a motor on a bench is a hazard. Read
> [First Run](First-Run#safety-first) before the first arm, not after it.

---

## Deutsch

**Hier anfangen:** [Erste Schritte](Erste-Schritte) — Verkabelung, Strom, und
das erste Mal, dass sich ein Motor dreht.

| Seite | Inhalt |
|---|---|
| [Erste Schritte](Erste-Schritte) | ESC anschließen, einschalten, scharfschalten, erster Gasbefehl |
| [Die Bildschirme](Die-Bildschirme) | Jeder Bildschirm und jedes Bedienelement, der Reihe nach |
| [Telemetrie](Telemetrie) | Was jede Kachel bedeutet, woher der Wert stammt, und warum dort `--` steht |
| [SD-Aufzeichnung](SD-Aufzeichnung) | Aufzeichnen auf microSD und Auswerten in [logwiju](https://subtilitas.github.io/logwiju/) |
| [AM32-Konfiguration](AM32-Konfiguration) | ESC-Einstellungen lesen, ändern und schreiben |
| [Fehlersuche](Fehlersuche) | Erst das Symptom, dann die Ursache |

> **Vor dem Einschalten:** ein Motor auf der Werkbank ist eine Gefahrenquelle.
> Lies [Erste Schritte](Erste-Schritte#sicherheit-zuerst) vor dem ersten
> Scharfschalten, nicht danach.

---

## Which board am I holding?

Both are 240×320 portrait touch panels on an RP2350. The 2.0" has 2.54 mm
headers; the 2.8" has JST-SH connectors only. You do not have to know which one
you have to flash it: take the **unified** image from
[Releases](https://github.com/subtilitas/DshotDisplay/releases) and it works
out which board it is on at every boot. It shows the answer under
**CFG → SETUP**, read-only.

<img src="img/splash.png" width="240" alt="The splash screen naming the detected board">

---

*Screenshots on this wiki are rendered by the project's own test suite from the
real UI code and published by CI on each release, so they show the firmware you
can actually download rather than an older build somebody remembered to
photograph.*

*Die Screenshots in diesem Wiki werden von der Testsuite des Projekts aus dem
echten UI-Code erzeugt und bei jedem Release automatisch veröffentlicht — sie
zeigen also die Firmware, die es tatsächlich zum Herunterladen gibt.*
