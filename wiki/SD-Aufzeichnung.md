[English](SD-Logging) | **Deutsch**

# SD-Aufzeichnung

Jedes Telemetrie-Frame, auf microSD im Betaflight-Blackbox-Format geschrieben,
damit sich ein Prüfstandslauf auswerten und nicht nur erinnern lässt.

<img src="img/log-screen.png" width="240" alt="SD-LOG-Bildschirm während der Aufzeichnung">

---

## Die Karte

Jede microSD-Karte, FAT32. Steck sie möglichst vor dem Einschalten ein — die
Karte wird einmal beim Start eingebunden, und keine der beiden Platinen hat
einen Karten-Erkennungspin. Eine später eingesteckte Karte braucht daher die
Schaltfläche `RETRY MOUNT`.

Die Dateien landen im Wurzelverzeichnis als `LOG00001.BFL`, `LOG00002.BFL` und
so weiter. Die Nummer zählt hoch; nichts wird je überschrieben.

---

## Aufzeichnen

**Automatisch:** Scharfschalten startet eine Aufzeichnung, Entschärfen beendet
sie. Das ist der Normalfall und braucht keinerlei Bedienung.

**Manuell:** `START` auf dem SD-LOG-Bildschirm beginnt jederzeit eine
Aufzeichnung, und eine von Hand gestartete Aufzeichnung überlebt das
Scharfschalten und Entschärfen, das sie sonst gesteuert hätte. `STOP` beendet
sie.

Der Tester-Bildschirm zeigt den Zustand in der obersten Zeile: `REC` in Rot
während der Aufzeichnung, `SD` im Leerlauf, `SDERR` bei defekter Karte, `NOSD`
wenn keine da ist.

---

## Den Bildschirm lesen

<img src="img/log-ready.png" width="240" alt="Karte eingebunden, bereit">

| Zeile | Bedeutung |
|---|---|
| **STATUS** | `NO CARD`, `READY`, `RECORDING` oder `CARD ERROR` |
| **FILE** | Die gerade geöffnete Datei |
| **FRAMES** | Bisher geschriebene Telemetrie-Frames |
| **WRITTEN** | Kilobyte auf der Karte |
| **DROPPED FRAMES** | Frames, die der Puffer nicht halten konnte. **Sollte null sein** |
| **BUF PEAK** | Höchststand des Schreibpuffers, gegen seine Größe |
| **WORST FLUSH** | Der längste einzelne Schreibvorgang auf die Karte, in Millisekunden |
| **CARD** | Was die Karte über sich selbst meldet: Typ und Größe |
| **MOUNT** | Das Ergebnis des Dateisystems. `0 OK` ist, was du willst |

Die unteren vier sind Diagnose, und interessant ist das Paar. Nähert sich
`BUF PEAK` der Puffergröße, oder dauert ein `WORST FLUSH` länger, als der Puffer
überbrücken kann, ist das die Warnung, dass die Karte für ihren Puffer zu
langsam ist. `DROPPED FRAMES` über null sagt, dass sie es bereits war — die
Aufzeichnung hat Lücken.

Meist ist eine langsame Karte die Ursache. Probiere zuerst eine andere.

### Keine Karte

<img src="img/log-nocard.png" width="240" alt="Keine Karte">

`NO CARD` mit `MOUNT 3 NOT READY` heißt, dass auf dem Bus überhaupt nichts
geantwortet hat — keine Karte, oder nicht richtig eingesteckt. `MOUNT 13 NO
FILESYSTEM` mit einer Größe unter `CARD` ist das Gegenteil und viel nützlicher:
die Karte ist da und spricht, und das Problem ist das Dateisystem. Formatiere
sie als FAT32.

### Eine nach dem Start eingesteckte Karte

<img src="img/log-mounted.png" width="240" alt="Karte über RETRY MOUNT gefunden">

`RETRY MOUNT`. Ohne diese Schaltfläche ist eine nach dem Einschalten eingelegte
Karte nicht von einer zu unterscheiden, die die Firmware nicht lesen kann.

---

## Die Aufzeichnungen öffnen

<a href="https://subtilitas.github.io/logwiju/"><img src="https://img.shields.io/badge/open%20your%20logs%20in-logwiju-07b0c8?style=for-the-badge" alt="Aufzeichnungen in logwiju öffnen"></a>

### [logwiju](https://subtilitas.github.io/logwiju/) — der vorgesehene Viewer

Läuft im Browser, nichts zu installieren, und lädt nichts hoch: die Datei wird
lokal gelesen. Zieh eine `.BFL`-Datei hinein, und du bekommst die Verläufe über
der Zeit aufgetragen — Drehzahl, Spannung, Strom, Temperatur, Stress, Gas.

Dafür wurden diese Aufzeichnungen gemacht, und danach greift man zuerst.

### Betaflight Blackbox Explorer

Die Dateien sind gewöhnliche Betaflight-Blackbox-Logs, also öffnet
[Blackbox Explorer](https://blackbox.betaflight.com/) sie ebenfalls, genau wie
das Kommandozeilenwerkzeug `blackbox_decode`. Einige Felder tragen Namen aus der
Welt der Flugsteuerung statt der eines Prüfstands — das ist der Preis dafür, ein
Format zu benutzen, für das es bereits Werkzeuge gibt.

---

## Was in einer Aufzeichnung steht

Pro Frame: Zeit, Drehzahl und eRPM, Gas, Spannung, Strom, ESC-Temperatur,
Stress, das ESC-Statusbyte, der Scharf-Zustand sowie die Paket- und Fehlerzähler.

Ist ein KISS-Telemetriekabel angeschlossen, werden beide Quellen zusätzlich zum
zusammengeführten Wert getrennt aufgezeichnet — eine Aufzeichnung lässt sich so
zur Gegenprobe verwenden, statt der Zusammenführung vertrauen zu müssen.

---

## Wenn die Karte nicht das Problem ist

Die Aufzeichnung ist eine Funktion, die beim Kompilieren ein- oder ausgeschaltet
wird, und ist standardmäßig an. Hast du die Firmware selbst mit
`SD_LOG_ENABLE=0` gebaut, gibt es den Bildschirm weiterhin, und er meldet immer
`NO CARD`.
