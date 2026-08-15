[English](AM32-Configuration) | **Deutsch**

# AM32-Konfiguration

Die Einstellungen eines AM32-ESC über dasselbe Signalkabel lesen, ändern und
schreiben — ohne Laptop und ohne Konfigurator.

<img src="img/am32-list.png" width="240" alt="AM32-Einstellungsliste">

> **Das schreibt in den Flash deines ESC.** Alles hier ist rücknehmbar, solange
> du auf diesem Bildschirm bist, und nichts erreicht den ESC, bevor du eine
> Schaltfläche eine volle Sekunde gehalten hast. Aber eine geschriebene
> Einstellung ist geschrieben.

---

## Verbinden

`CFG → AM32`.

Der Bildschirm übergibt den Signalpin von der DShot-Ausgabe an den
Bootloader-Transport und beginnt dann, nach einem ESC zu suchen.

**Dann den ESC neu starten.** Das ist nicht optional und keine Schikane des
Testers: der AM32-Bootloader lauscht nur in einem kurzen Fenster beim
Einschalten. Zieh den ESC-Akku ab und steck ihn wieder an, **während dieser
Bildschirm angezeigt wird**. Die Firmware wiederholt ihren Verbindungsaufbau so
schnell sie kann, damit sie gerade sendet, wenn dieses Fenster auftaucht.

Du siehst `SEARCHING...` und einen Ladekringel. Erwischt sie das Fenster,
erscheint die Liste, und der Streifen unter dem Titel nennt den ESC und seine
Firmware-Version.

Kommen Bytes zurück, die sich nicht deuten lassen, sagt der Bildschirm das
ausdrücklich — `REPLY NOT UNDERSTOOD` mit den ersten Bytes und der Baudrate. Das
ist ein anderes Problem als Schweigen und lohnt die Unterscheidung: es heißt,
das Kabel ist in Ordnung und die Rahmenbildung nicht.

---

## Blättern

Die Liste scrollt durch Ziehen. Sie folgt deinem Finger, sobald die Bewegung
erkannt ist, und ein Ziehen wählt nie etwas aus — du kannst also durchblättern,
ohne Zeilen aufleuchten zu lassen, die du gar nicht treffen wolltest.

<img src="img/am32-scrolled.png" width="240" alt="Die Liste mitten im Scrollen">

Ein **Tippen, das nirgendwohin geht**, wählt die Zeile darunter aus. Die
Editorleiste am unteren Rand zeigt dann Namen und Wert dieses Feldes.

Die Zeilen sind gruppiert, mit dem Gruppennamen in Grau darüber. Welche Felder
es gibt, hängt von der Layout-Version des ESC ab — die Firmware zeigt nur die,
die zur gelesenen Version passen.

---

## Ändern

<img src="img/am32-edit.png" width="240" alt="Ein Feld wird geändert">

| Geste | Wirkung |
|---|---|
| Zeile antippen | Auswählen |
| `-` / `+` in der Editorleiste | **Fein** — ein Schritt je Betätigung. Halten wiederholt, zunehmend schneller |
| Zeile **seitwärts** wischen | **Grob** — ein Wisch über die volle Breite deckt den gesamten Bereich des Feldes ab |
| **Hoch / runter** ziehen | Scrollen |

Eine Geste ist entweder Scrollen oder Ändern, nie beides: die Achse wird in den
ersten Pixeln der Bewegung entschieden und dann festgehalten.

Grobe Wischer beachten die Schrittweite des jeweiligen Feldes, sodass Werte, die
gültig bleiben müssen, es auch tun — die Polzahl bleibt zum Beispiel gerade. Sie
setzen an den Enden neu an: schießt du oben über und wischst ein Stück zurück,
reagiert es sofort, statt erst zurücklaufen zu müssen, wie weit du hinausgekommen
bist.

**Geänderte Zeilen sind am linken Rand bernsteinfarben markiert**, und ihre
Werte werden ebenfalls bernsteinfarben. Bis hierher hat noch nichts den ESC
erreicht.

`REVERT` verwirft jede ungespeicherte Änderung und stellt das Gelesene wieder
her.

---

## Schreiben

<img src="img/am32-written.png" width="240" alt="Schreiben überprüft">

`HOLD TO WRITE` eine volle Sekunde halten. Ein Fortschrittsbalken füllt sich
dabei, und die Schaltfläche zeigt `NO CHANGES`, wenn es nichts zu schreiben gibt.

Die Firmware **liest die Einstellungen anschließend zurück und vergleicht sie**,
statt der Bestätigung des ESC zu trauen — ein Schreibvorgang, der Erfolg meldet
und falsch landet, ist schlimmer als einer, der laut scheitert. Du bekommst
`WRITE VERIFIED`, oder genau das Byte, das abweicht, und was dort stehen sollte.

Die meisten AM32-Einstellungen werden beim nächsten Einschalten des ESC wirksam.

---

## Die Hex-Ansicht

<img src="img/am32-hex.png" width="240" alt="Die rohen Einstellungsbytes">

`HEX` zeigt die rohen Einstellungsbytes. Das ist die Notluke: wenn ein Feld für
deine Layout-Version nicht angeboten wird, oder wenn du prüfen willst, was eine
Änderung auf Byte-Ebene tatsächlich bewirkt hat, findest du es hier. Nur lesbar.

---

## Verlassen

`BACK` gibt den Signalpin an die DShot-Ausgabe zurück und kehrt zum
Einstellungsbildschirm zurück. Die Ausgabe startet entschärft.

---

## Wenn keine Verbindung zustande kommt

- **Endlos `SEARCHING...`.** Das Fenster beim Einschalten ist der springende
  Punkt. Zieh den ESC-Akku ab und steck ihn wieder an, *während der Bildschirm
  angezeigt wird*. Vorher zu starten funktioniert nicht.
- **`REPLY NOT UNDERSTOOD`.** Die Verbindung lebt, die Rahmenbildung stimmt
  nicht. Meist ein ESC, der kein AM32 ist.
- **`NO VALID SETTINGS`.** Es hat etwas geantwortet, und der Einstellungsblock
  sah nicht plausibel aus. Schreibe nichts.

Mehr unter [Fehlersuche](Fehlersuche#am32-verbindet-nicht).
