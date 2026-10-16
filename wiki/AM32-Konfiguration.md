[English](AM32-Configuration) | **Deutsch**

# AM32-Konfiguration

Dieser Bildschirm liest, ändert und schreibt die Einstellungen eines AM32-ESC
(Electronic Speed Controller, Motorregler) über das Signalkabel. Er braucht
keinen Laptop und keinen Konfigurator. Er aktualisiert keine ESC-Firmware.

<img src="img/am32-list.png" width="240" alt="AM32-Einstellungsliste">

> **Das schreibt in den Flash deines ESC.** Änderungen bleiben im Tester, bis
> du `HOLD TO WRITE` 1 s lang hältst. Danach stehen sie im ESC.

---

## Verbinden

`CFG → AM32` öffnen.

Der Bildschirm übergibt den Signalpin vom DShot-Treiber an die
Bootloader-Verbindung (19200 Baud, ein Draht, halbduplex) und beginnt dann zu
suchen.

**Dann den ESC neu starten.** Der AM32-Bootloader lauscht nur in einem kurzen
Fenster nach dem Einschalten. Zieh den ESC-Akku ab und steck ihn wieder an,
während dieser Bildschirm offen ist. Die Firmware wiederholt ihren
Verbindungsaufbau und wartet je 30 ms auf eine Antwort, damit sie sendet, wenn
das Fenster aufgeht. Die Länge des Fensters ist nicht gemessen.

Der Bildschirm zeigt `SEARCHING...`, dann `WAITING... (TRY n)` mit einem
Versuchszähler, und einen Ladekringel. Bei Erfolg erscheint die Liste, und der
Streifen unter dem Titel zeigt den ESC-Namen, die MCU-Familie (Microcontroller)
und die Firmware-Version.

Kommen Bytes an, die sich nicht deuten lassen, zeigt der Bildschirm
`REPLY NOT UNDERSTOOD:`, die Byteanzahl, die ersten drei Bytes und die
Baudrate sowie `LINK WORKS BUT FRAMING IS OFF.` Das heißt: Das Kabel
funktioniert, Rahmenbildung oder Baudrate passen nicht.

---

## Blättern

Hoch oder runter ziehen scrollt. Die Liste folgt dem Finger 1:1, sobald die
Bewegung erkannt ist. Ziehen wählt nie eine Zeile aus.

<img src="img/am32-scrolled.png" width="240" alt="Die Liste mitten im Scrollen">

Ein **Tippen ohne Bewegung** wählt die Zeile darunter aus. Die Editorleiste am
unteren Rand zeigt dann Namen und Wert des Feldes.

Die Zeilen sind gruppiert, mit dem Gruppennamen in Grau über jeder Gruppe.
Welche Felder es gibt, hängt von der Layout-Version der ESC-Einstellungen ab.
Angezeigt werden nur die Felder, die zur gelesenen Version passen.

---

## Ändern

<img src="img/am32-edit.png" width="240" alt="Ein Feld wird geändert">

| Geste | Wirkung |
|---|---|
| Zeile antippen | Auswählen |
| `-` / `+` in der Editorleiste | **Fein**: ein Schritt je Betätigung. Halten wiederholt, zunehmend schneller |
| Zeile **seitwärts** wischen | **Grob**: 180 px Weg decken den gesamten Bereich des Feldes ab |
| **Hoch / runter** ziehen | Scrollen |

Eine Geste ist entweder Scrollen oder Ändern. Die Achse steht nach 6 px
Bewegung fest.

Grobe Wischer rasten auf die Schrittweite des Feldes ein, sodass Werte gültig
bleiben; die Polzahl bleibt zum Beispiel gerade. Am Ende des Bereichs setzt
der Wischer neu an, sodass ein kleiner Wischer zurück sofort wirkt.

**Geänderte Zeilen sind am linken Rand bernsteinfarben markiert**, und ihre
Werte sind bernsteinfarben. An den ESC ist noch nichts gesendet.

`REVERT` verwirft alle ungespeicherten Änderungen und stellt die gelesenen
Werte wieder her.

---

## Schreiben

<img src="img/am32-written.png" width="240" alt="Schreiben überprüft">

`HOLD TO WRITE` 1 s lang halten. Währenddessen füllt sich ein
Fortschrittsbalken. Ohne Änderungen zeigt die Schaltfläche `NO CHANGES`.

Nach dem Schreiben wartet die Firmware 300 ms, **liest die Einstellungen
zurück und vergleicht sie** Byte für Byte. Sie verlässt sich nicht auf die
Bestätigung des ESC. Bei Übereinstimmung erscheint `WRITE VERIFIED`. Bei einer
Abweichung erscheint das erste abweichende Byte als
`DIFF @xx: GOT xx WANT xx`.

Die meisten AM32-Einstellungen wirken ab dem nächsten Einschalten des ESC. Das
ist Verhalten von AM32; der Tester prüft es nicht.

---

## Die Hex-Ansicht

<img src="img/am32-hex.png" width="240" alt="Die rohen Einstellungsbytes">

`HEX` zeigt die 48 rohen Einstellungsbytes, nur lesbar. Damit prüfst du ein
Feld, das deine Layout-Version nicht anbietet, oder siehst, was eine Änderung
auf Byte-Ebene bewirkt hat. `FIELDS` kehrt zur Liste zurück.

---

## Verlassen

`BACK` gibt den Signalpin an den DShot-Treiber zurück und kehrt zum
Einstellungsbildschirm zurück. Der Tester ist entschärft.

---

## Wenn keine Verbindung zustande kommt

- **Endlos `SEARCHING...` oder `WAITING...`.** Zieh den ESC-Akku ab und steck
  ihn wieder an, *während der Bildschirm offen ist*. Ein Neustart vor dem
  Öffnen des Bildschirms funktioniert nicht.
- **`REPLY NOT UNDERSTOOD`.** Die Verbindung funktioniert, die Rahmenbildung
  nicht. Mögliche Ursachen: Der ESC läuft nicht mit AM32, oder sein Bootloader
  nutzt ein Protokoll, das dieser Transport nicht umsetzt.
- **`NO VALID SETTINGS`.** Der ESC hat geantwortet, und der Einstellungsblock
  hat die Plausibilitätsprüfung nicht bestanden. Schreibe nichts.

Mehr unter [Fehlersuche](Fehlersuche#am32-verbindet-nicht).
