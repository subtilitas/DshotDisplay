[English](The-Screens) | **Deutsch**

# Die Bildschirme

Die Firmware hat fünf Bildschirme. Der Tester ist die Wurzel. Die anderen
erreichst du über `CFG` und verlässt sie mit `BACK`.

```
Tester ──CFG──> Einstellungen ──┬── AM32     ──BACK──> Einstellungen
                                ├── SD LOG   ──BACK──> Einstellungen
                                └── SETUP    ──BACK──> Einstellungen
                          └─────BACK──> Tester
```

**BACK sitzt oben rechts** im Kopfbereich, auf jedem Bildschirm außer dem
Tester. Der Tester ist die Wurzel und hat keinen Kopfbereich.

---

## Der Tester

<img src="img/tester-armed.png" width="240" alt="Tester-Bildschirm">

**Obere Zeile.** Von links nach rechts:

- das Scharf-Feld: `SAFE` (grün) oder `ARMED` (rot);
- `DS` und die DShot-Bitrate in kbit/s, wie im SETUP eingestellt, cyan,
  sobald eine EDT-Freigabe (Extended DShot Telemetry) gesendet wurde. `DSERR`
  in Bernstein ersetzt sie, wenn die Anzeige ihren DShot-Ausgang nicht
  einrichten konnte; dann wird nichts an den ESC gesendet;
- der Aufzeichnungszustand: `REC`, `SD`, `SDERR` oder `NOSD`
  (siehe [SD-Aufzeichnung](SD-Aufzeichnung));
- die Spannung des platineneigenen Akkus, gemessen über den
  200k/100k-Teiler. Was sie bei reiner USB-Versorgung (Universal Serial Bus)
  anzeigt, ist nicht gemessen. In den letzten 5 s vor dem Entschärfen durch
  die Leerlaufsperre ersetzt `IDLE ns` in Bernstein die Spannung.

**Drehzahl.** Mechanische Drehzahl (RPM, Umdrehungen pro Minute), berechnet
aus der vom ESC (Electronic Speed Controller, Motorregler) gemeldeten eRPM
(elektrische Drehzahl) und der Polzahl. Der `ERPM`-Wert und die Polzahl stehen
darunter. Grüne Ziffern: eRPM ist in den letzten 500 ms eingetroffen.
Dunkelrote Null: nicht, und die Zeile zeigt `NO TELEMETRY`.

**Sechs Kacheln.** Spannung, Strom, ESC-Temperatur, Stress, ESC-Status und
Verbindungsqualität. Siehe [Telemetrie](Telemetrie).

**Gas.** Die Anzeige, die Obergrenze und der befohlene Prozentwert. Der
Prozentwert bezieht sich auf den vollen DShot-Bereich (0–2000 = 0–100 %): Bei
20 % Obergrenze zeigt ein Zug über die volle Breite `20%`. Im entschärften
Zustand zeigt der Balken `ARM TO ENABLE`.

**Schaltflächen.** `HOLD TO ARM` / `DISARM`, `HOLD` (haltendes Gas), `CFG`.

---

## Einstellungen

<img src="img/settings.png" width="240" alt="Einstellungsbildschirm">

Erreichbar über `CFG`, das dabei **entschärft**.

- **EDT ON / EDT OFF**: ein reines Anzeigefeld unter dem Titel. Grün, solange
  Extended-DShot-Telemetry-Frames eintreffen, sonst rot. Es gibt keine
  Einschalt-Schaltfläche. Die Firmware schickt die Freigabe an jeden ESC, der
  zu antworten beginnt, und wiederholt sie jede Sekunde, solange eRPM eintrifft
  und EDT nicht. Der ESC bestätigt die Freigabe nicht, daher zeigt das Feld
  *empfangene Frames*, nicht gesendete Befehle.
- **MOTOR POLES** und **THROTTLE CEILING**: Schrittschalter; halten
  wiederholt.
- **BEEP**: sendet den DShot-Beacon-Befehl, der Motor piept. Damit erkennst du,
  welcher ESC angeschlossen ist. Es funktioniert nur im entschärften Zustand.
  Die Bildunterschrift lautet `BEEP NEEDS THE ESC DISARMED`, nach einer
  abgelehnten Betätigung `REFUSED - DISARM THE ESC FIRST`.
- **AM32 / SD LOG / SETUP**: die drei Unterbildschirme.

`UNSAVED` erscheint unter dem Titel, wenn ein Wert hier oder im SETUP vom
Flash abweicht. Die Schaltfläche zum Speichern liegt im SETUP.

<img src="img/settings-unsaved.png" width="240" alt="UNSAVED — Änderungen noch nicht im Flash">

Der Befehl dauert 6 ms (6 Frames bei 1 kHz). Der Bildschirm wird alle 25 ms
neu gezeichnet, daher hebt die Firmware die Schaltfläche nach jeder Betätigung
350 ms lang hervor: weiß bei gesendet, bernsteinfarben bei abgelehnt.

<img src="img/config-beep-flash.png" width="240" alt="BEEP angenommen"> <img src="img/config-beep-refused.png" width="240" alt="BEEP abgelehnt, weil der ESC scharf ist">

---

## SETUP

<img src="img/setup.png" width="240" alt="SETUP-Bildschirm">

Verkabelung und Anzeige, dazu die Schaltfläche, die alle Einstellungen ins
Flash schreibt.

| Zeile | Bedeutung |
|---|---|
| **BOARD** | Nur Anzeige. Die Platine, die die Start-Erkennung gefunden hat |
| **ESC PIN** | GPIO (General Purpose Input/Output, frei verwendbarer Pin) für das Signalkabel. Angeboten werden nur die freien GPIOs dieser Platine: GP0–GP11 und GP21–GP23 auf der 2,0", GP28 und GP29 auf der 2,8" |
| **DSHOT KBAUD** | 150 / 300 / 600 / 1200. Herunterstellen, wenn die `LINK`-Fehlerrate steigt |
| **KISS TELEM** | Ob auf dem optionalen dritten Telemetriekabel empfangen wird |
| **KISS PIN** | GPIO für dieses Kabel. Überspringt den ESC-Pin, sodass beide nie zusammenfallen |
| **CONTRAST** | `NORMAL` oder `HIGH`. Hoher Kontrast ist für Tageslicht |
| **BACKLIGHT** | 16–255, in Schritten von 32 |

**LINK** unter den Zeilen ist live: Pakete pro Sekunde und Fehlerrate. Grün
heißt, es kommen Pakete an, der ESC-Pin stimmt also. Bernstein heißt, mehr als
5 % Fehler.

**Änderungen wirken sofort. Nur das Speichern braucht das Halten.** Eine neue
Bitrate baut den DShot-Treiber vor dem nächsten Frame neu auf. `HOLD TO SAVE`
1 s lang schreibt ins Flash. `RESET` lädt die kompilierten Voreinstellungen
nur in die laufenden Einstellungen. Wer ohne Speichern weggeht, lässt das
Flash unverändert.

Die Hinweiszeile unten nennt die verwendeten Werte: `SAVED SETTINGS IN USE`
oder `COMPILED DEFAULTS IN USE`. Nach einer Änderung zeigt sie
`UNSAVED - HOLD SAVE TO KEEP`.

> **Die Platine wird bei jedem Start erkannt, nicht gewählt.** Keine
> gespeicherte Einstellung kann die Erkennung überstimmen, daher kann ein
> falscher Wert den Start der Anzeige nicht verhindern.

<img src="img/setup-contrast.png" width="240" alt="SETUP mit hohem Kontrast"> <img src="img/tester-contrast.png" width="240" alt="Der Tester-Bildschirm mit hohem Kontrast">

*Hoher Kontrast gilt für jeden Bildschirm und setzt die
Hintergrundbeleuchtung auf 255, solange er an ist.*

---

## SD LOG

<img src="img/log-ready.png" width="240" alt="SD-LOG-Bildschirm, Karte bereit">

Aufzeichnungszustand und Zähler. Siehe [SD-Aufzeichnung](SD-Aufzeichnung).

---

## AM32

<img src="img/am32-list.png" width="240" alt="AM32-Einstellungsliste">

Liest, ändert und schreibt die Einstellungen eines AM32-ESC über das
Signalkabel. Siehe [AM32-Konfiguration](AM32-Konfiguration).

---

## Was jeder Bildschirm gleich macht

- **Ein Tippen wirkt beim Loslassen, nicht beim Berühren.** Rutschst du vor
  dem Loslassen von der Schaltfläche, wird nichts ausgelöst. `DISARM` ist die
  Ausnahme und wirkt beim Berühren.
- **Eine gedrückte Schaltfläche zeigt einen Druckrahmen.** Zeigt ein
  Bedienelement keinen Rahmen, hat es die Berührung nicht erkannt.
- **`-` / `+` wiederholen beim Halten und werden schneller.** Ein Schritt beim
  Drücken; nach 400 ms ein Schritt alle 120 ms; nach 1,2 s alle 60 ms; nach
  2,5 s alle 25 ms.
- **Schreiben ins Flash braucht 1 s Halten** mit Fortschrittsbalken:
  Einstellungen auf der Platine speichern und Einstellungen in einen ESC
  schreiben.
