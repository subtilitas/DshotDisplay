[English](The-Screens) | **Deutsch**

# Die Bildschirme

Fünf Bildschirme. Der Tester ist die Wurzel; alles andere erreichst du über
`CFG` und verlässt es mit `BACK`.

```
Tester ──CFG──> Einstellungen ──┬── AM32     ──BACK──> Einstellungen
                                ├── SD LOG   ──BACK──> Einstellungen
                                └── SETUP    ──BACK──> Einstellungen
                          └─────BACK──> Tester
```

**BACK sitzt immer an derselben Stelle**: oben rechts im Kopfbereich, auf jedem
Bildschirm, der einen hat. Der Tester hat keinen, weil er die Wurzel ist — es
gibt nichts, wohin man zurückkehren könnte.

---

## Der Tester

<img src="img/tester-armed.png" width="240" alt="Tester-Bildschirm">

**Obere Zeile.** Das Scharf-Feld (`SAFE` grün / `ARMED` rot), die DShot-Bitrate
— sie wird cyan, sobald eine EDT-Freigabe hinausgegangen ist — der
Aufzeichnungszustand und die von der Platine selbst gemessene Akkuspannung.
Wenn der Leerlauf-Timer kurz vor dem Entschärfen steht, ersetzt der Countdown
die Spannung.

**Drehzahl.** Mechanische Drehzahl, errechnet aus der vom ESC gemeldeten eRPM
und deiner Polzahl. `ERPM` und die Polzahl stehen darunter. Grüne Ziffern heißen
lebende Verbindung; dunkelrote heißen, dass zuletzt nichts angekommen ist.

**Sechs Kacheln.** Spannung, Strom, ESC-Temperatur, Stress, ESC-Status und
Verbindungsqualität. Siehe [Telemetrie](Telemetrie).

**Gas.** Die Anzeige, die Obergrenze und der befohlene Prozentwert. Der
Prozentwert bezieht sich auf den echten DShot-Bereich von 0–100 %, beschönigt
also nichts: bei 20 % Obergrenze zeigt ein Zug über die volle Breite `20%`.

**Schaltflächen.** `HOLD TO ARM` / `DISARM`, `HOLD` (haltendes Gas), `CFG`.

---

## Einstellungen

<img src="img/settings.png" width="240" alt="Einstellungsbildschirm">

Erreichbar über `CFG`, das dabei **zwangsweise entschärft**.

- **EDT ON / EDT OFF** — ein reines Anzeigefeld unter dem Titel. Grün, solange
  tatsächlich Extended-DShot-Telemetry-Frames eintreffen, rot, wenn nicht. Es
  gibt keine Einschalt-Schaltfläche: die Firmware fragt jeden ESC von sich aus,
  sobald er erscheint, und fragt weiter, solange ein ESC antwortet, aber kein
  EDT sendet. Das Feld folgt den *empfangenen Frames*, nicht der Frage, ob
  etwas gesendet wurde — die Anforderung wird abgeschickt und nie bestätigt.
- **MOTOR POLES** und **THROTTLE CEILING** — Schrittschalter; halten wiederholt.
- **BEEP** — lässt den Motor piepen, womit sich herausfinden lässt, an welchem
  ESC man eigentlich hängt. Der ESC muss dafür entschärft sein; eine abgelehnte
  Betätigung blinkt bernsteinfarben und die Bildunterschrift nennt den Grund.
- **AM32 / SD LOG / SETUP** — die drei Unterbildschirme.

`UNSAVED` erscheint unter dem Titel, wenn hier oder im SETUP etwas von dem
abweicht, was im Flash steht. Die Schaltfläche zum Speichern liegt im SETUP.

<img src="img/settings-unsaved.png" width="240" alt="UNSAVED — Änderungen noch nicht im Flash">

Eine Betätigung von `BEEP` wird auf dem Bildschirm quittiert, denn der Befehl
selbst ist nach etwa sechs Millisekunden vorbei — gegen einen Bildaufbau mit
40 Hz sähe die Schaltfläche sonst tot aus. Weiß heißt, er ging hinaus;
bernsteinfarben heißt, er wurde abgelehnt, und die Bildunterschrift wird zur
Begründung.

<img src="img/config-beep-flash.png" width="240" alt="BEEP angenommen"> <img src="img/config-beep-refused.png" width="240" alt="BEEP abgelehnt, weil der ESC scharf ist">

---

## SETUP

<img src="img/setup.png" width="240" alt="SETUP-Bildschirm">

Verkabelung und Anzeige, dazu die einzige Schaltfläche, die davon etwas ins
Flash schreibt.

| Zeile | Bedeutung |
|---|---|
| **BOARD** | Nur Anzeige. Was die Hardware beim Start geantwortet hat. Keine Auswahl — siehe unten |
| **ESC PIN** | An welchem GPIO das Signalkabel hängt. Angeboten werden nur Pins, die auf deiner Platine wirklich frei sind |
| **DSHOT KBAUD** | 150 / 300 / 600 / 1200. Herunterstellen, wenn die Telemetrie unzuverlässig ist |
| **KISS TELEM** | Ob das optionale dritte Telemetriekabel erwartet wird |
| **KISS PIN** | An welchem GPIO dieses Kabel hängt. Springt über den ESC-Pin hinweg, sodass sich beide nie überschneiden können |
| **CONTRAST** | `NORMAL` oder `HIGH`. Hoher Kontrast ist für Tageslicht |
| **BACKLIGHT** | 0–255 |

**LINK** darunter ist live: Pakete pro Sekunde und Fehlerrate, direkt vom ESC.
Das steht dort, damit sich das Ändern des ESC-Pins überprüfen lässt, ohne zum
Tester-Bildschirm zurückzugehen — grün heißt, dass Frames zurückkommen, und das
heißt, der Pin stimmt.

**Änderungen wirken sofort; nur das Behalten braucht das Halten.** Stelle die
Bitrate herunter, und die Frame-Ausgabe wird noch in diesem Bild neu aufgebaut.
`HOLD TO SAVE` schreibt nach einer Sekunde ins Flash. `RESET` stellt die
kompilierten Vorgaben nur in den laufenden Einstellungen wieder her — gehst du
weg, ohne zu speichern, ist nichts verloren.

> **Die Platine wird erkannt, nicht gewählt.** Kurzzeitig war sie eine
> Auswahl, und das war ein Fehler: falsch gewählt und gespeichert, und der
> *nächste* Start baute Anzeige und Pin-Belegung für Hardware auf, die nicht da
> war — und der Bildschirm, mit dem man es hätte zurücknehmen können, war genau
> der, der nicht mehr kam.

<img src="img/setup-contrast.png" width="240" alt="SETUP mit hohem Kontrast"> <img src="img/tester-contrast.png" width="240" alt="Der Tester-Bildschirm mit hohem Kontrast">

*Hoher Kontrast, für draußen. Er gilt für jeden Bildschirm, nicht nur für
diesen, und stellt die Hintergrundbeleuchtung auf voll, solange er an ist.*

---

## SD LOG

<img src="img/log-ready.png" width="240" alt="SD-LOG-Bildschirm, Karte bereit">

Aufzeichnungszustand und Zähler. Siehe [SD-Aufzeichnung](SD-Aufzeichnung).

---

## AM32

<img src="img/am32-list.png" width="240" alt="AM32-Einstellungsliste">

Liest, ändert und schreibt die Einstellungen eines AM32-ESC über dasselbe
Signalkabel. Siehe [AM32-Konfiguration](AM32-Konfiguration).

---

## Was jeder Bildschirm gleich macht

- **Ein Tippen löst beim Loslassen aus, nicht beim Berühren.** Rutscht du von
  einer versehentlich berührten Schaltfläche weg, passiert nichts. Die eine
  bewusste Ausnahme ist `DISARM`, das beim Berühren wirkt — das Bedienelement,
  dessen Aufgabe „Motor sofort anhalten" ist, kann nicht auf das Loslassen
  warten.
- **Eine Schaltfläche unter dem Finger sieht auch so aus.** Reagiert ein
  Bedienelement nicht, hat es die Berührung nicht angenommen.
- **`-` / `+` wiederholen beim Halten und werden schneller.** Drei Stufen:
  schrittweise, laufend, rasend.
- **Alles Unwiderrufliche ist ein Halten von einer Sekunde**, mit
  Fortschrittsbalken: Einstellungen ins Flash der Platine schreiben, und
  Einstellungen in einen ESC schreiben.
