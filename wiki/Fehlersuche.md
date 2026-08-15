[English](Troubleshooting) | **Deutsch**

# Fehlersuche

Erst das Symptom.

---

## Der Bildschirm bleibt dunkel

Die Platine hat die falsche Pin-Belegung erwischt, oder die Start-Erkennung hat
sich geweigert zu raten.

Hast du ein **einzelplatinen**-Abbild auf die jeweils andere Platine geflasht,
ist das die Ursache: geflasht sind die beiden nicht mehr zu unterscheiden, und
ihre Display-Pins unterscheiden sich. Flashe stattdessen das **unified**-Abbild
aus den [Releases](https://github.com/subtilitas/DshotDisplay/releases) — es
erkennt die Platine bei jedem Start.

Hast du das unified-Abbild bereits, heißt ein dunkler Bildschirm, dass die
Erkennung dreimal hintereinander nicht feststellen konnte, auf welcher Platine
sie läuft, und bewusst angehalten hat, statt Pins für womöglich gar nicht
vorhandene Hardware zu treiben. Über USB ist die Platine weiterhin erreichbar.

---

## Keine Telemetrie

<img src="img/tester-disarmed.png" width="240" alt="Keine Telemetrie">

Auf dem Signalkabel antwortet nichts. Grob nach Wahrscheinlichkeit:

1. **Falscher Pin.** Vergleiche **CFG → SETUP → ESC PIN** damit, wo das Kabel
   tatsächlich steckt. Die `LINK`-Zeile auf demselben Bildschirm ist live, du
   kannst sie also beim Umstellen beobachten — jede Paketrate über null heißt,
   du hast ihn gefunden.
2. **Keine Masse.** Die Masse des ESC und die der Platine müssen verbunden sein.
   Das ist der Fall, der wie ein toter ESC aussieht.
3. **Der ESC hat keinen Strom.** Er läuft an seinem eigenen Akku, nicht an der
   Platine.
4. **Der ESC kann kein bidirektionales DShot**, oder es ist in seinen eigenen
   Einstellungen abgeschaltet. Einfaches DShot sendet nichts zurück.
5. **Das Kabel ist zu lang oder zu störanfällig.** Stelle **DSHOT KBAUD** auf
   300 und sieh nach, ob `LINK` erwacht.

---

## Drehzahl läuft, jede Kachel zeigt `--`

Das ist ein ESC, der eRPM liefert und keine Extended DShot Telemetry sendet.
Entweder beherrscht er kein EDT, oder EDT ist noch nicht eingeschaltet.

Sieh auf das Feld unter dem Titel des Einstellungsbildschirms. Steht dort
`EDT OFF`, lies den nächsten Abschnitt. Führt das Datenblatt oder die Firmware
des ESC keine EDT-Unterstützung, ist die Drehzahl alles, was zwei Kabel
hergeben — ein
[KISS-Telemetriekabel](Telemetrie#das-optionale-dritte-kabel) ist dann der Weg
zum Rest.

---

## EDT bleibt aus

Die Firmware fragt von sich aus und wiederholt das, ein Feld, das länger als ein
paar Sekunden rot bleibt, heißt also, dass die Versuche nicht angenommen werden.

- **Steht der Motor still?** DShot-Befehle werden nur bei stehendem Motor
  ausgeführt, und nach dem Entschärfen läuft ein Propeller mehrere Sekunden aus.
  Warte, bis er wirklich steht.
- **Ist der Tester entschärft?** Im scharfen Zustand gehen keine Befehle hinaus.
- **Kann der ESC überhaupt EDT?** BLHeli_S ohne EDT-fähigen Build und einige
  ältere AM32-Versionen ignorieren die Freigabe dauerhaft und sehen genau so aus.
- **Starte den ESC neu.** Der Abbruch der Verbindung setzt den Zustand der
  Firmware zurück, der Ersatz — auch wenn es derselbe ESC ist — wird also von
  vorn gefragt.

Hat es früher funktioniert und tut es jetzt nicht mehr, ist das eine Meldung
wert: vor der aktuellen Firmware gab es drei Wege, auf denen eine Freigabe
verworfen werden konnte, und Entschärfen und erneutes Scharfschalten war der
bekannte Behelf. Mit der aktuellen Firmware sollte er nicht nötig sein.

---

## Der Motor bleibt von selbst stehen

Drei Dinge halten einen Motor absichtlich an, und jedes meldet sich:

- **Die Leerlaufsperre.** Dreißig Sekunden ohne Bildschirmberührung im scharfen
  Zustand, dann entschärft sie und zählt die letzten fünf Sekunden oben rechts
  herunter. Das Display dunkelt dabei ab.
- **`CFG` öffnen.** Der Einstellungsbildschirm entschärft jedes Mal zwangsweise.
- **Die Herzschlag-Sicherung.** Reagiert die Anzeige-Firmware nicht mehr, stellt
  der zweite Kern binnen einer Viertelsekunde auf null. Das würde dir als
  eingefrorener Bildschirm auffallen, nicht nur als stehender Motor.

Bleibt er ohne eines davon stehen, verdächtige die Schutzfunktionen des ESC —
Unterspannungsabschaltung, Temperatur, Desync — und sieh dir `ESC STATUS` und
`ESC TEMP` an.

---

## `LINK` zeigt eine hohe Fehlerrate

Bernsteinfarben oberhalb von 5 %. Es liegt fast immer am Signalkabel: Länge,
Verlegung oder eine fehlende Masse. Kürzen, von den Motorphasen fernhalten, dann
**DSHOT KBAUD** herunterstellen.

Eine hohe Fehlerrate bei funktionierender Drehzahlanzeige ist nicht harmlos —
die fehlgeschlagenen Frames sind Frames, auf die der ESC nicht reagiert hat.

---

## Die Karte wird nicht erkannt

<img src="img/log-nocard.png" width="240" alt="Keine Karte">

Sieh auf die `MOUNT`-Zeile des SD-LOG-Bildschirms; sie unterscheidet Fälle, die
sonst gleich aussehen.

- **`3 NOT READY`** — auf dem Bus hat nichts geantwortet. Keine Karte, oder
  nicht richtig eingesteckt.
- **`13 NO FILESYSTEM`** mit einer Größe unter `CARD` — die Karte ist da und
  spricht, und das Dateisystem ist das Problem. Als FAT32 formatieren.
- **Karte nach dem Start eingesteckt** — `RETRY MOUNT` drücken. Die Karte wird
  nur einmal beim Einschalten eingebunden, und keine der Platinen hat einen
  Karten-Erkennungspin.

---

## Verlorene Frames in einer Aufzeichnung

`DROPPED FRAMES` über null heißt, dass die Aufzeichnung Lücken hat. Sieh dir
`BUF PEAK` und `WORST FLUSH` an: nähert sich der Höchststand der Puffergröße,
oder dauert ein Schreibvorgang länger, als der Puffer überbrücken kann, kommt
die Karte nicht mit.

Probiere zuerst eine andere Karte. Es ist fast immer die Karte.

---

## AM32 verbindet nicht

Der Bootloader lauscht nur kurz beim Einschalten. **Zieh den ESC-Akku ab und
steck ihn wieder an, während der AM32-Bildschirm angezeigt wird.** Vorher
funktioniert nicht.

- **`REPLY NOT UNDERSTOOD`** — die Verbindung lebt, die Rahmenbildung stimmt
  nicht. Meist kein AM32-ESC.
- **`NO VALID SETTINGS`** — es hat etwas geantwortet, aber der
  Einstellungsblock war nicht plausibel. Schreibe nichts.

---

## Meine Einstellungen setzen sich zurück

Sie wurden nie gespeichert. Polzahl und Gasobergrenze werden auf dem
Einstellungsbildschirm geändert, aber die Schaltfläche, die sie ins Flash
schreibt, ist `HOLD TO SAVE` im **SETUP**. `UNSAVED` unter dem Titel ist der
Hinweis darauf.

---

## KISS-Telemetrie ist auf meiner 2,8"-Platine aus

So wird sie ausgeliefert. Ein Tippen schaltet sie ein:
**CFG → SETUP → KISS TELEM**.

Die 2,8"-Platine führt genau zwei freie GPIOs heraus, und das ESC-Signal liegt
anfangs auf einem davon — KISS standardmäßig einzuschalten würde also den
einzigen übrigen Pin für ein Kabel verbrauchen, das die meisten gar nicht
angelötet haben. Die 2,0"-Platine hat eine ganze Kameraleiste voll freier Pins
und ist aus demselben Grund umgekehrt standardmäßig an.

Beim Einschalten wandert KISS von selbst auf den freien Pin; du musst nichts
auswählen. Der Pin-Schrittschalter weigert sich außerdem, auf dem Pin des
ESC-Signals zu landen, sodass beide nicht kollidieren können.

> **Auf der 2,8" war das einmal wirklich unmöglich, und ist es nicht mehr.**
> Solange der Empfänger ein Hardware-UART war, brauchte er einen Pin mit
> UART-RX-Funktion, und der einzige freie auf dieser Platine war GP29 — der Pin,
> auf dem das ESC bereits lag. KISS einzuschalten fand überhaupt keinen Pin, und
> die Prüfung schaltete es sofort wieder ab. Der Empfänger ist inzwischen eine
> PIO-Zustandsmaschine und tastet jeden GPIO ab. Wenn du irgendwo gelesen hast,
> dass KISS auf der 2,8" nicht funktioniert: darum ging es.

Schaltet es sich tatsächlich mit `KISS OFF: NEEDS A PIN OF ITS OWN` selbst ab,
sind wirklich beide Pins vergeben: verschiebe zuerst das ESC-Signal auf den
anderen und schalte dann KISS ein.

---

## Immer noch nicht weiter

Öffne ein Issue unter
[github.com/subtilitas/DshotDisplay/issues](https://github.com/subtilitas/DshotDisplay/issues).
Nützlich sind: welche Platine, welches Abbild (unified oder einzelplatinen), was
die `LINK`-Zeile zeigt, und um welchen ESC es sich handelt.
