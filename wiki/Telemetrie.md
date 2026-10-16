[English](Telemetry) | **Deutsch**

# Telemetrie

Alles auf dem Tester-Bildschirm kommt vom ESC (Electronic Speed Controller,
Motorregler), außer der Akkuspannung in der oberen Zeile, die die Platine
selbst misst. Diese Seite erklärt, woher jeder Wert stammt und wie genau er
ist.

<img src="img/tester-armed.png" width="240" alt="Telemetriekacheln mit eintreffendem EDT">

---

## Zweieinhalb Quellen

**eRPM** (elektrische Umdrehungen pro Minute) kommt über das
DShot-Signalkabel zurück. Jeder ESC mit bidirektionalem DShot sendet sie.
Funktioniert die Verbindung, funktioniert die Drehzahl.

**EDT** (Extended DShot Telemetry) überträgt Spannung, Strom, Temperatur,
Stress und ein Statusbyte über dasselbe Kabel, verschachtelt mit den
eRPM-Frames. Es braucht Unterstützung in der ESC-Firmware (BLHeli_32,
Bluejay, AM32), und der ESC muss einen Freigabebefehl erhalten. Den sendet die
Firmware; siehe unten.

Bidirektionales DShot und EDT funktionieren auf der Hardware. Ein Fehler ist
mit Stand v2.3 nicht bekannt.

**KISS-Telemetrie** ist ein optionales drittes Kabel vom Telemetrie-Pad des
ESC. Ihre Schritte sind 0,01 V und 0,01 A, gegenüber 0,25 V und 1 A bei EDT,
und sie liefert die verbrauchten mAh, die EDT nicht überträgt. Siehe
[das dritte Kabel](#das-optionale-dritte-kabel).

---

## Die Kacheln

| Kachel | Quelle | Anmerkung |
|---|---|---|
| **VOLTAGE** | KISS, wenn frisch, sonst EDT | Die Marke in der Beschriftungszeile nennt die Quelle |
| **CURRENT** | KISS, wenn frisch, sonst EDT | Dasselbe |
| **ESC TEMP** | KISS, wenn frisch, sonst EDT | Rot ab 90 °C |
| **STRESS** | Nur EDT | 0–255, die Lastangabe des ESC. Bernstein oberhalb von 200 |
| **ESC STATUS** | Nur EDT | `OK` / `WARN` / `ERROR` / `ALERT`. Die Bedeutung legt die ESC-Firmware fest |
| **LINK** | Zählung des Testers | Gute Pakete pro Sekunde und die Prüfsummenfehlerrate der letzten Sekunde. Bernstein oberhalb von 5 % |

Verbrauchte **mAh** erscheinen in der Ecke der Temperaturkachel, nur mit KISS.

<img src="img/tester-live.png" width="240" alt="Alle Kacheln gefüllt">

### Die kleine Marke `KISS` / `EDT`

Die Kacheln für Spannung und Strom tragen in ihrer Beschriftungszeile eine
Marke: cyan `KISS` oder gedämpft `EDT`. `12,25 V` aus EDT heißt: ein Wert
irgendwo in einem 0,25-V-Schritt. Derselbe Wert aus KISS liegt innerhalb von
0,01 V. Wechselt eine Marke von `KISS` auf `EDT`, ist das Telemetriekabel
verstummt und der Wert 25-mal gröber, auch wenn sich die Ziffern nicht
sichtbar ändern.

---

## Warum eine Kachel `--` zeigt

Jeder Messwert verfällt. Ein EDT-Wert, der 1 s lang nicht erneuert wurde,
zeigt `--`. Nach 500 ms ohne eRPM zeigt die Drehzahl eine dunkelrote Null. Ein
abgezogener oder getauschter ESC hinterlässt daher keine alten Werte.

<img src="img/tester-stale.png" width="240" alt="Telemetrie verfallen">

`--` heißt eines von dreien:

- der ESC beherrscht kein EDT (die Drehzahl funktioniert trotzdem);
- EDT wird beherrscht, ist aber noch nicht freigegeben. Beobachte das Feld auf
  dem Einstellungsbildschirm;
- der ESC antwortet nicht mehr.

Eine **verfallene Statuskachel zeigt `--`, nie `OK`**.

---

## EDT einschalten

Das macht die Firmware, ohne Schaltfläche.

Solange ein ESC eRPM liefert und 1 s lang kein EDT-Frame eingetroffen ist,
sendet die Firmware die Freigabe jede Sekunde erneut. Nach 500 ms ohne eRPM
wird der Zustand zurückgesetzt. Ein später angeschlossener, neu gestarteter
oder getauschter ESC bekommt so seine eigene Freigabe.

Jede Freigabe besteht aus 10 gleichen Frames. Sie wird nur gesendet, wenn alles
davon zutrifft:

- die Verbindung steht seit 300 ms, der ESC ist also fertig gestartet;
- der Tester ist entschärft und der Motor steht (eRPM ist null);
- kein anderer Befehl, etwa BEEP, wird gerade gesendet.

Ein ESC führt einen DShot-Befehl erst nach einer Folge gleicher Frames aus.
Eine Freigabe zu einem anderen Zeitpunkt ginge verloren.

<img src="img/config-edt-on.png" width="240" alt="EDT ON"> <img src="img/config-edt-off.png" width="240" alt="EDT OFF">

Bleibt das Feld rot, obwohl der ESC EDT beherrscht, siehe
[Fehlersuche](Fehlersuche#edt-bleibt-aus).

---

## Das optionale dritte Kabel

<img src="img/tester-kiss.png" width="240" alt="Kacheln aus KISS-Telemetrie gespeist">

Ein ESC mit KISS-Telemetrie sendet auf Anforderung ein 10-Byte-Frame mit
115200 Baud von seinem Telemetrie-Pad. Der Tester fordert bei jedem 20.
DShot-Frame eines an, also mit 50 Hz. Das Kabel bringt feinere Spannung und
Strom sowie die verbrauchten mAh.

**Stand:** Ein Nutzer meldet, dass es auf der 2,0"-Platine funktioniert; vom
Projekt nicht getestet.

| | 2,0"-Platine | 2,8"-Platine |
|---|---|---|
| **Telemetrie-RX** | GP5, Stiftleiste P1, Pin 10 | GP28, J4 Pin 11 |

Die Platine empfängt auf dieser Leitung nur. Der Empfänger ist ein UART
(Universal Asynchronous Receiver-Transmitter) aus einer PIO-Zustandsmaschine
(PIO: programmierbare Ein-/Ausgabe), daher **funktioniert jeder freie GPIO**
auf beiden Platinen. GPIO steht für General Purpose Input/Output, frei
verwendbarer Pin. Einschalten unter **CFG → SETUP → KISS TELEM**, den Pin in
der Zeile darunter setzen. Der Pin-Schrittschalter überspringt den Pin des
ESC-Signals.

**Voreinstellung: an auf der 2,0", aus auf der 2,8".** Die 2,8" hat zwei freie
Pins, GP28 und GP29, und der ESC belegt einen davon. Beim Einschalten von KISS
wird der KISS-Pin vom ESC-Pin weg verschoben, falls beide zusammenfallen.

> **Spannung.** Die KISS-Spezifikation legt diese Leitung auf 3,6 V, genau den
> absoluten Grenzwert der RP2350-GPIOs. Welchen Pegel ein bestimmter ESC
> treibt, hat das Projekt nicht gemessen. Miss deinen, bevor du ihn
> anschließt.

Wird das Kabel während des Betriebs abgezogen, wechseln die Marken 500 ms nach
dem letzten KISS-Frame auf `EDT`, und die Werte werden gröber. Sie halten den
letzten KISS-Wert nicht.

---

## LINK, und wie eine schlechte Verbindung aussieht

`998/S` bei `0% ERR` ist eine saubere 1-kHz-Verbindung. Die Fehlerrate wird
oberhalb von 5 % bernsteinfarben.

Eine schlechte Verbindung kommt meist vom Signalkabel: zu lang, ungeschirmt
oder nahe an den Motorphasen verlegt. Zuerst kürzen. Sonst **DSHOT KBAUD** im
SETUP auf 300 stellen. Die `LINK`-Zeile auf dem SETUP-Bildschirm ist live.
