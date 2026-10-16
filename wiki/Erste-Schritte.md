[English](First-Run) | **Deutsch**

# Erste Schritte

Diese Seite beschreibt, wie du einen ESC (Electronic Speed Controller,
Motorregler) anschließt, einschaltest und zum ersten Mal scharfschaltest. Du
brauchst zwei Kabel und einen Akku für den ESC.

---

## Sicherheit zuerst

Die Platine bringt einen Motor zum Drehen. Erledige vorher diese drei Dinge:

- **Propeller abnehmen.**
- **Motor sichern.** Ein ungesicherter Motor kann sich beim Hochlaufen bewegen
  oder springen.
- **ESC am eigenen Akku betreiben.** Versorge keinen Motor über den 5-V-Pin der
  Platine.

Die Firmware hat diese Sperren. Sie ersetzen die Schritte oben nicht:

- Die Gasobergrenze steht ab Werk auf **20 %** des DShot-Bereichs.
- Scharfschalten verlangt **1 s Halten**, bei mindestens 250 ms Gas auf null.
- `DISARM` wirkt **beim Berühren**. Alle anderen Bedienelemente wirken beim
  Loslassen.
- Im scharfen Zustand entschärft der Tester nach **30 s** ohne Berührung. Die
  letzten 5 s zählen auf dem Bildschirm herunter.
- Hält die Anzeige-Firmware an, setzt der zweite Kern das Gas nach **250 ms**
  ohne Herzschlag auf null.

---

## ESC anschließen

Zwei Kabel: Signal und Masse.

| | 2,0"-Platine | 2,8"-Platine |
|---|---|---|
| **Signal** | GP4, Stiftleiste P2, Pin 11 | GP29, J4 Pin 12 |
| **Masse** | GND, Stiftleiste P2, Pin 13 | GND, J4 Pin 1 oder 5 |

GP steht für GPIO (General Purpose Input/Output, frei verwendbarer Pin). Der
ESC-Pin lässt sich später unter **CFG → SETUP** ändern und wird im Flash
gespeichert.

> **Vorher das mittlere Kabel des ESC-Steckers abzwicken oder auspinnen.**
> Der mittlere Leiter eines dreipoligen Servosteckers ist der
> **+5-V-Ausgang des BEC** (Battery Eliminator Circuit) im ESC. Die Pins des
> RP2350 sind **nicht** 5-V-tolerant. Mehr als etwa 3,6 V überschreitet den
> absoluten Grenzwert und beschädigt den Chip. Der Tester braucht nur Signal
> und Masse.

Auf der 2,0"-Platine nimmt eine dreipolige Stiftleiste auf **P2 Pin 11–13**
einen Servostecker direkt auf: Signal auf GP4, das entfernte mittlere Kabel
über GP10, Masse auf Pin 13.

Halte das Signalkabel kurz. Die größte funktionierende Länge ist nicht
gemessen. Steigt die Fehlerrate unter `LINK`, stelle **DSHOT KBAUD** im SETUP
auf 300.

---

## Einschalten

Die Platine läuft über USB-C (USB: Universal Serial Bus), der ESC über seinen
eigenen Akku. Die Reihenfolge ist egal. Die Firmware schickt die
Telemetrie-Freigabe an jeden ESC, der zu antworten beginnt. Du kannst den ESC
also jederzeit anstecken oder neu starten.

Der Startbildschirm nennt 3 s lang die erkannte Platine, dann erscheint der
Tester-Bildschirm.

<img src="img/splash.png" width="240" alt="Startbildschirm">

---

## Solange kein ESC angeschlossen ist

<img src="img/tester-disarmed.png" width="240" alt="Tester-Bildschirm ohne ESC">

Antwortet kein ESC, zeigt der Bildschirm `NO TELEMETRY` unter der Drehzahl,
`--` in jeder Kachel und eine dunkelrote Null als Drehzahl.

Ändert sich das trotz versorgtem und verkabeltem ESC nicht, geht es bei
[Fehlersuche](Fehlersuche#keine-telemetrie) weiter.

---

## Das erste Scharfschalten

1. Prüfe, dass der Propeller ab ist.
2. `HOLD TO ARM` drücken und 1 s **halten**. Währenddessen füllt sich ein
   grüner Balken am unteren Rand des Felds.
3. Das Feld wird rot und zeigt `ARMED`. Die Hintergrundbeleuchtung sinkt für
   120 ms auf ein Viertel, damit du das Scharfschalten siehst, während du auf
   den Motor schaust.

<img src="img/tester-armed.png" width="240" alt="Scharf, mit eintreffender Telemetrie">

Zum Stoppen `DISARM` drücken. Es wirkt beim Berühren.

---

## Gas geben

Zwei Flächen stellen denselben Gaswert ein.

**Der Balken unten.** Mit `HOLD` aus ist die Fingerposition auf der Bahn das
Gas, und beim Loslassen geht das Gas auf null. Die volle Breite der Bahn ist
die *Obergrenze*: Bei den voreingestellten 20 % gibt das rechte Ende 20 %.

**Die Zahlenfläche** über dem Balken ist ein relatives Gas-Pad. Nach **oben**
wischen für mehr, nach **unten** für weniger. Ein Tippen bewirkt nichts, und
Wischer summieren sich. So lässt sich das Gas über mehrere kurze Wischer
erhöhen, ohne hinzusehen. Nach 6 px Bewegung wechselt der Hinweis unter der
Drehzahl auf `SWIPE ACTIVE`, und der Wischer bewegt das Gas.

`HOLD` macht den Balken relativ und haltend: Das Gas bleibt, wo du es
loslässt. Der Balken zeigt einen Griff. `HOLD` auszuschalten setzt das Gas auf
null.

---

## Die Obergrenze anheben

`CFG` öffnet die Einstellungen. Das Öffnen entschärft immer.

<img src="img/settings.png" width="240" alt="Einstellungsbildschirm">

- **MOTOR POLES**: rechnet die gemeldete eRPM (elektrische Drehzahl) mit
  `RPM = eRPM / (Pole / 2)` in die Drehzahl um. Bereich 2–28, nur gerade
  Werte. Voreinstellung 14. Ein falscher Wert verfälscht die Drehzahl um einen
  festen Faktor; andere Werte sind nicht betroffen.
- **THROTTLE CEILING**: 5–100 % in Schritten von 5 %. `-` oder `+` gedrückt
  halten wiederholt den Schritt, mit der Zeit schneller.

Beide Werte werden erst mit `HOLD TO SAVE` auf **SETUP** gespeichert. `UNSAVED`
unter dem Titel zeigt ungespeicherte Änderungen.

---

## Weiter

- [Die Bildschirme](Die-Bildschirme): der Rest der Oberfläche
- [Telemetrie](Telemetrie): was die Kacheln zeigen
- [SD-Aufzeichnung](SD-Aufzeichnung): einen Lauf aufzeichnen und auswerten
