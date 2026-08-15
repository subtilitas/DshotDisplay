[English](First-Run) | **Deutsch**

# Erste Schritte

Zwei Kabel, ein Akku und etwa fünf Minuten.

---

## Sicherheit zuerst

Die einzige Aufgabe dieser Platine ist es, einen Motor drehen zu lassen. Alles
Folgende setzt voraus, dass diese drei Dinge bereits erledigt sind.

- **Propeller ab.** Nicht „zur Seite gedreht". Ab.
- **Motor festschrauben.** Ein Motor, der nicht verschraubt ist, wird bei etwa
  30 % Gas zum Geschoss.
- **ESC am eigenen Akku betreiben.** Versuche nicht, einen Motor über den 5-V-Pin
  der Platine zu versorgen.

Die Firmware hilft, aber bei nichts davon:

- Die Gasobergrenze steht ab Werk auf **20 %**, damit die erste Handlung nicht
  Vollgas sein kann. Erhöhe sie, wenn du weißt, was sich da dreht.
- Scharfschalten verlangt ein bewusstes **Halten von einer Sekunde**, und das Gas
  muss die ganze Zeit auf null gestanden haben.
- Entschärfen wirkt **sofort beim Berühren** — das einzige Bedienelement, das
  nicht darauf wartet, dass du den Finger hebst.
- Wird der Bildschirm im scharfen Zustand **30 Sekunden** nicht berührt,
  entschärft er sich selbst und zählt die letzten fünf Sekunden sichtbar herunter.
- Sollte die Anzeige-Firmware jemals hängen, glaubt der zweite Kern dem zuletzt
  übergebenen Gaswert nach einer Viertelsekunde nicht mehr und stellt auf null.

---

## ESC anschließen

Zwei Kabel: Signal und Masse.

| | 2,0"-Platine | 2,8"-Platine |
|---|---|---|
| **Signal** | GP4 — Stiftleiste P2, Pin 11 | GP29 — J4 Pin 12 |
| **Masse** | GND — Stiftleiste P2, Pin 13 | GND an J4 |

Beides lässt sich später unter **CFG → SETUP** direkt am Gerät ändern und wird
gespeichert.

> **Vorher das mittlere Kabel des ESC-Steckers abzwicken oder auspinnen.**
> Der mittlere Leiter eines normalen dreipoligen Servosteckers ist der
> **+5-V-BEC-Ausgang** des ESC. Die Pins des RP2350 sind **nicht** 5-V-tolerant —
> alles oberhalb von etwa 3,6 V liegt über dem absoluten Grenzwert und
> beschädigt den Chip. Der Tester braucht Signal und Masse, sonst nichts.

Auf der 2,0"-Platine kannst du eine dreipolige Stiftleiste auf **P2 Pin 11–13**
löten; ein Servostecker passt dann direkt: Signal auf GP4, das (entfernte)
mittlere Kabel über GP10, Masse auf Pin 13.

Halte das Signalkabel kurz. Bidirektionales DShot mit 600 kBaud mag keine 30 cm
ungeschirmte, herumhängende Servoleitung — wenn die Telemetrie unzuverlässig
ist, stelle im SETUP zuerst auf 300 kBaud, bevor du etwas anderes verdächtigst.

---

## Einschalten

Die Platine läuft über USB-C, der ESC über seinen eigenen Akku. Die Reihenfolge
ist egal: die Firmware fragt einen neu erschienenen ESC von sich aus nach
Telemetrie, du kannst den ESC also jederzeit anstecken oder neu starten.

Zuerst nennt der Startbildschirm die erkannte Platine, dann erscheint der
Tester-Bildschirm.

<img src="img/splash.png" width="240" alt="Startbildschirm">

---

## Solange kein ESC angeschlossen ist

<img src="img/tester-disarmed.png" width="240" alt="Tester-Bildschirm ohne ESC">

`NO TELEMETRY` unter der Drehzahl und `--` in jeder Kachel ist die richtige
Anzeige für „es antwortet nichts". Die Drehzahl-Ziffern bleiben dunkel, statt
eine selbstbewusste Null zu zeigen — eine Null wäre eine Behauptung.

Ändert sich das trotz versorgtem und verkabeltem ESC nicht innerhalb ein bis
zwei Sekunden, geht es bei [Fehlersuche](Fehlersuche#keine-telemetrie) weiter.

---

## Das erste Scharfschalten

1. Prüfe, ob der Propeller ab ist. Ja, noch einmal.
2. `HOLD TO ARM` drücken und eine Sekunde **halten**. Ein grüner Balken füllt
   sich währenddessen über dem Feld.
3. Das Feld wird rot und zeigt `ARMED`, und das ganze Display dunkelt kurz ab —
   dieses Blinken ist Absicht, damit das Scharfschalten im Augenwinkel ankommt,
   während du auf den Motor schaust und nicht auf den Bildschirm.

<img src="img/tester-armed.png" width="240" alt="Scharf, mit eintreffender Telemetrie">

Zum Stoppen: `DISARM`. Wirkt in dem Moment, in dem du es berührst.

---

## Gas geben

Zwei Flächen, die denselben Wert speisen.

**Der Balken unten** ist ein federnder Abzug. Wo dein Finger auf der Bahn steht,
ist das Gas, und es springt sofort auf null zurück, sobald du loslässt. Die
volle Breite der Bahn ist die *Obergrenze*, nicht Vollgas — bei den
voreingestellten 20 % bedeutet ganz rechts also 20 %.

**Die große Zahlenfläche** ist ein relatives Gas-Pad. Nach **oben** wischen für
mehr, nach **unten** für weniger. Es ist relativ, ein Tippen bewirkt also
nichts, und Wischbewegungen summieren sich: du kannst das Gas über mehrere kurze
Wischer aufbauen, ohne auf den Bildschirm zu sehen. `SWIPE ACTIVE` erscheint,
sobald ein Wischer die kleine Totzone verlassen hat und den Motor wirklich
bewegt.

`HOLD` macht aus dem federnden Balken einen haltenden: das Gas bleibt stehen,
wo du es hingelegt hast, statt auf null zurückzugehen. Der Balken bekommt dafür
einen Griff. `HOLD` wieder auszuschalten stellt das Gas auf null.

---

## Die Obergrenze anheben

`CFG` öffnet die Einstellungen — und entschärft dabei jedes Mal.

<img src="img/settings.png" width="240" alt="Einstellungsbildschirm">

- **MOTOR POLES** — wie aus der vom ESC gemeldeten eRPM die angezeigte Drehzahl
  wird. Fast jeder Quadrocopter-Motor hat **14**. Ist der Wert falsch, ist die
  Drehzahl um einen festen Faktor falsch, während alles andere stimmt.
- **THROTTLE CEILING** — anheben, wenn du weißt, was sich dreht. `-` oder `+`
  gedrückt halten wiederholt den Schritt, zunehmend schneller.

Beides wird erst gespeichert, wenn du es speicherst, und die Schaltfläche dafür
liegt auf **SETUP**. `UNSAVED` unter dem Titel weist darauf hin.

---

## Weiter

- [Die Bildschirme](Die-Bildschirme) — der Rest der Oberfläche
- [Telemetrie](Telemetrie) — was die Kacheln sagen
- [SD-Aufzeichnung](SD-Aufzeichnung) — einen Lauf aufzeichnen und auswerten
