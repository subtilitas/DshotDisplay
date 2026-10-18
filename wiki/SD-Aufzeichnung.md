[English](SD-Logging) | **Deutsch**

# SD-Aufzeichnung

Der Tester zeichnet die Telemetrie im Betaflight-Blackbox-Format auf microSD
auf, mit 500 Frames pro Sekunde. Ein Frame hat im Mittel 14,5 Byte (gemessen),
also 7,3 kB/s oder 26 MB pro Stunde.

<img src="img/log-screen.png" width="240" alt="SD-LOG-Bildschirm während der Aufzeichnung">

---

## Die Karte

Verwende eine microSD-Karte mit FAT32 (File Allocation Table). Die Karte wird
einmal beim Start eingebunden, und keine der Platinen hat einen
Karten-Erkennungspin. Steck die Karte vor dem Einschalten ein, oder drück nach
dem Einstecken `RETRY` auf dem SD-LOG-Bildschirm.

Die Dateien landen im Wurzelverzeichnis als `LOG00001.BFL`, `LOG00002.BFL` und
so weiter. Jede neue Datei erhält die niedrigste freie Nummer von 00001 bis
09999. Vorhandene Dateien werden nicht überschrieben.

Die 2,0"-Platine liest die Karte über SPI (Serial Peripheral Interface) mit
12 MHz, die 2,8" über 4-Bit-SDIO (Secure Digital I/O) mit 10 MHz.

---

## Aufzeichnen

**Automatisch:** Scharfschalten startet eine Aufzeichnung, Entschärfen beendet
sie.

**Manuell:** `START` auf dem SD-LOG-Bildschirm startet jederzeit eine
Aufzeichnung. Eine von Hand gestartete Aufzeichnung wird durch Entschärfen
nicht beendet. `STOP` beendet sie.

Der Tester-Bildschirm zeigt den Zustand in der oberen Zeile: `REC` (rot)
während der Aufzeichnung, `SD` im Leerlauf, `SDERR` nach einem Kartenfehler,
`NOSD` ohne Karte.

---

## Den Bildschirm lesen

<img src="img/log-ready.png" width="240" alt="Karte eingebunden, bereit">

| Zeile | Bedeutung |
|---|---|
| **STATUS** | `NO CARD`, `READY`, `RECORDING` oder `CARD ERROR` |
| **FILE** | Die geöffnete Datei |
| **FRAMES** | Bisher geschriebene Frames |
| **WRITTEN** | Geschriebene Kilobyte |
| **DROPPED FRAMES** | Frames, die der Puffer nicht halten konnte. **Sollte null sein.** Sonst rot |
| **BUF PEAK** | Höchster Füllstand des 8192-Byte-Schreibpuffers. Bernstein über 1/2, rot über 3/4 |
| **WORST FLUSH** | Der längste einzelne Schreibvorgang auf die Karte, in ms. Bernstein über 50 ms |
| **CARD** | Kartentyp und Größe, wie die Karte sie meldet |
| **MOUNT** | Der Ergebniscode des Dateisystems. `0 OK` heißt eingebunden |

Nähert sich `BUF PEAK` der Puffergröße, oder dauert ein `WORST FLUSH` länger,
als der Puffer überbrückt (8192 Byte sind etwa 1,1 s bei 7,3 kB/s), ist die
Karte für den Puffer zu langsam. `DROPPED FRAMES` über null heißt, die
Aufzeichnung hat bereits Lücken.

Meist ist eine langsame Karte die Ursache. Probiere zuerst eine andere Karte.

### Keine Karte

<img src="img/log-nocard.png" width="240" alt="Keine Karte">

`NO CARD` mit `MOUNT 3 NOT READY` heißt, auf dem Bus hat nichts geantwortet:
keine Karte, oder sie sitzt nicht richtig. `MOUNT 13 NO FILESYSTEM` mit einer
Größe unter `CARD` heißt, die Karte antwortet und das Dateisystem ist das
Problem. Formatiere sie als FAT32.

### Eine nach dem Start eingesteckte Karte

<img src="img/log-mounted.png" width="240" alt="Karte über RETRY gefunden">

Drück `RETRY`. Ohne das wird eine nach dem Einschalten eingesteckte Karte
nicht eingebunden.

---

## Aufzeichnungen ohne Kartenleser holen

<img src="img/log-usb-serving.png" width="240" alt="Die Karte als USB-Laufwerk">

`USB DRIVE` auf dem SD-LOG-Bildschirm stellt die Karte dem Computer am
USB-Kabel (Universal Serial Bus) als Wechsellaufwerk bereit. `.BFL`-Dateien
herüberkopieren, das Laufwerk am Computer auswerfen, und der Tester nimmt die
Karte zurück. Der Bildschirm zeigt die bisher gelesene Datenmenge.

Die Platine ist ohnehin ein USB-Gerät, für Strom und den seriellen Anschluss.
Das Laufwerk ist eine zweite USB-Schnittstelle am selben Kabel.

**Das Laufwerk ist schreibgeschützt.** Der Computer kann Dateien kopieren und
nicht schreiben. Ein Dateisystemtreiber des Computers kann daher weder die
Karte noch die Nummerierung verändern. Eine Meldung „schreibgeschützt" vom
Computer ist zu erwarten.

**Die Karte wird übergeben, nicht geteilt.** FAT kann zwischen zwei Schreibern
nicht vermitteln. Bevor das Laufwerk erscheint, leert, schließt und hängt der
Logger die Karte aus, und er rührt sie bis zum Auswerfen nicht an. Währenddessen
zeigt der SD-LOG-Bildschirm die Übergabe statt seiner Zähler.

Ohne Karte ist `USB DRIVE` als nicht verfügbar gezeichnet und tut nichts.

**Mit Karte lehnt die Schaltfläche in zwei Fällen ab und nennt den Grund:**

<img src="img/log-usb-refused.png" width="240" alt="USB DRIVE während der Aufzeichnung abgelehnt">

| Meldung | Was zu tun ist |
|---|---|
| `DISARM FIRST` | Entschärfen. Die Übergabe hält den Logger an |
| `STOP RECORDING FIRST` | Die Aufzeichnung beenden |

**Scharfschalten ist gesperrt, solange der Computer die Karte hat.** `BACK`
verlässt den Bildschirm, ohne die Übergabe abzubrechen. Der Tester-Bildschirm
ist also erreichbar, schaltet aber erst nach dem Auswerfen scharf. Ein Druck,
der begann, während der Computer die Karte hatte, schaltet nicht scharf: nach
dem Auswerfen neu drücken.

Am Computer auswerfen oder `EJECT` auf dem Bildschirm drücken. Beides beendet
die Übergabe.

---

## Die Aufzeichnungen öffnen

<a href="https://subtilitas.github.io/logwiju/"><img src="https://img.shields.io/badge/open%20your%20logs%20in-logwiju-07b0c8?style=for-the-badge" alt="Aufzeichnungen in logwiju öffnen"></a>

### [logwiju](https://subtilitas.github.io/logwiju/) — der vorgesehene Viewer

logwiju läuft im Browser ohne Installation. Die Datei wird lokal gelesen und
nicht hochgeladen. Zieh eine `.BFL`-Datei auf die Seite, um die Felder über der
Zeit darzustellen.

### Betaflight Blackbox Explorer

Die Dateien sind Betaflight-Blackbox-Logs, daher öffnen
[Blackbox Explorer](https://blackbox.betaflight.com/) und das
Kommandozeilenwerkzeug `blackbox_decode` sie ebenfalls. Einige Feldnamen
stammen aus Flugsteuerungen, etwa `motor[0]` für das Gas.

---

## Was in einer Aufzeichnung steht

Felder pro Frame:

| Feld | Inhalt |
|---|---|
| `loopIteration`, `time` | Frame-Zähler und Zeit |
| `motor[0]` | Befohlenes Gas |
| `eRPM[0]` | eRPM (elektrische Drehzahl) aus bidirektionalem DShot |
| `eRPMkiss[0]` | eRPM aus KISS |
| `vbatLatest`, `amperageLatest` | Spannung und Strom aus KISS; 0, solange KISS nicht aktiv ist |
| `vbatEdt`, `amperageEdt` | Spannung und Strom aus EDT (Extended DShot Telemetry) |
| `escTemperature[0]` | Temperatur des ESC (Electronic Speed Controller, Motorregler) |
| `escConsumption` | Verbrauchte mAh (nur KISS) |
| `escStress` | EDT-Stresswert |

Das ESC-Statusbyte, der Scharf-Zustand und die Verbindungszähler werden nicht
aufgezeichnet.

Mit KISS-Kabel werden beide Quellen getrennt aufgezeichnet, sodass sich eine
Quelle an der anderen prüfen lässt.

---

## Wenn die Karte nicht das Problem ist

Die Aufzeichnung ist standardmäßig einkompiliert. Eine mit `SD_LOG_ENABLE=0`
gebaute Firmware zeigt den SD-LOG-Bildschirm weiterhin, und er meldet immer
`NO CARD`.
