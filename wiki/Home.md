# DshotDisplay

A bidirectional DShot tester for one ESC (electronic speed controller), on the
Waveshare **RP2350-Touch-LCD-2** and **RP2350-Touch-LCD-2.8**. With two wires
to the ESC, the board is the throttle source, the telemetry display and the
log recorder. It needs no flight controller, laptop or Betaflight.

<img src="img/tester-armed.png" width="240" alt="The tester screen, armed">

---

## English

**Start here:** [First Run](First-Run): wiring, power, and the first motor
run.

| Page | What it covers |
|---|---|
| [First Run](First-Run) | Wiring an ESC, powering up, arming, first throttle input |
| [The Screens](The-Screens) | Every screen and every control, in order |
| [Telemetry](Telemetry) | What each tile means, where the value comes from, and why a tile reads `--` |
| [SD Logging](SD-Logging) | Recording to microSD and opening the result in [logwiju](https://subtilitas.github.io/logwiju/) |
| [AM32 Configuration](AM32-Configuration) | Reading, editing and writing ESC settings |
| [Troubleshooting](Troubleshooting) | Symptom first, cause second |

> **Before you power anything on:** a motor on a bench can injure. Read
> [First Run](First-Run#safety-first) before the first arm.

---

## Deutsch

**Hier anfangen:** [Erste Schritte](Erste-Schritte): Verkabelung, Strom und
der erste Motorlauf.

| Seite | Inhalt |
|---|---|
| [Erste Schritte](Erste-Schritte) | ESC anschließen, einschalten, scharfschalten, erster Gasbefehl |
| [Die Bildschirme](Die-Bildschirme) | Jeder Bildschirm und jedes Bedienelement, der Reihe nach |
| [Telemetrie](Telemetrie) | Was jede Kachel bedeutet, woher der Wert stammt und warum eine Kachel `--` zeigt |
| [SD-Aufzeichnung](SD-Aufzeichnung) | Aufzeichnen auf microSD und Auswerten in [logwiju](https://subtilitas.github.io/logwiju/) |
| [AM32-Konfiguration](AM32-Konfiguration) | ESC-Einstellungen lesen, ändern und schreiben |
| [Fehlersuche](Fehlersuche) | Erst das Symptom, dann die Ursache |

> **Vor dem Einschalten:** Ein Motor auf der Werkbank kann verletzen. Lies
> [Erste Schritte](Erste-Schritte#sicherheit-zuerst) vor dem ersten
> Scharfschalten.

---

## Which board am I holding?

Both boards have a 240×320 portrait touch panel and an RP2350. The 2.0" has
2.54 mm headers. The 2.8" has JST-SH connectors only. The **unified** image
from [Releases](https://github.com/subtilitas/DshotDisplay/releases) runs on
both: it identifies the board at every boot and shows the result under
**CFG → SETUP**, read-only.

*Welche Platine habe ich?* Beide haben ein 240×320-Touch-Display im Hochformat
und einen RP2350. Die 2,0"-Platine hat 2,54-mm-Stiftleisten, die 2,8"-Platine
nur JST-SH-Stecker. Das **unified**-Abbild aus den
[Releases](https://github.com/subtilitas/DshotDisplay/releases) läuft auf
beiden: Es erkennt die Platine bei jedem Start und zeigt das Ergebnis unter
**CFG → SETUP** an, nur lesbar.

<img src="img/splash.png" width="240" alt="The splash screen naming the detected board">

---

*The project's test suite renders the screenshots on this wiki from the UI
code. CI (continuous integration) publishes them with the wiki, so they match
the released firmware.*

*Die Testsuite des Projekts erzeugt die Screenshots in diesem Wiki aus dem
UI-Code. Die CI (Continuous Integration) veröffentlicht sie mit dem Wiki,
daher entsprechen sie der veröffentlichten Firmware.*
