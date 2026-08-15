[English](Telemetry) | **Deutsch**

# Telemetrie

Alles auf dem Tester-Bildschirm außer der Akkuspannung kommt vom ESC. Diese
Seite handelt davon, woher jeder Wert stammt und wie weit man ihm trauen darf.

<img src="img/tester-armed.png" width="240" alt="Telemetriekacheln mit eintreffendem EDT">

---

## Zweieinhalb Quellen

**eRPM** läuft über das Signalkabel selbst und funktioniert mit jedem ESC, der
bidirektionales DShot beherrscht. Funktioniert die Verbindung überhaupt, hast du
Drehzahl.

**EDT** — Extended DShot Telemetry — überträgt Spannung, Strom, Temperatur,
Stress und ein Statusbyte über dasselbe Kabel, verschachtelt zwischen den
eRPM-Frames. Es braucht Unterstützung in der ESC-Firmware und muss eingeschaltet
werden. Darum kümmert sich die Firmware; siehe unten.

**KISS-Telemetrie** ist ein optionales drittes Kabel vom Telemetrie-Pad des ESC.
Sie ist feiner (0,01 V und 0,01 A gegenüber 0,25-V- und 1-A-Schritten bei EDT)
und liefert verbrauchte mAh, wofür EDT gar keinen Platz hat. Siehe
[das dritte Kabel](#das-optionale-dritte-kabel).

---

## Die Kacheln

| Kachel | Quelle | Anmerkung |
|---|---|---|
| **VOLTAGE** | KISS, wenn frisch, sonst EDT | Die Marke in der Beschriftungszeile sagt, welche |
| **CURRENT** | KISS, wenn frisch, sonst EDT | Dasselbe |
| **ESC TEMP** | KISS, wenn frisch, sonst EDT | Wird ab 90 °C rot |
| **STRESS** | Nur EDT | 0–255, die Lastangabe des ESC. Bernstein oberhalb von 200 |
| **ESC STATUS** | Nur EDT | `OK` / `WARN` / `ERROR` / `ALERT`. Die genaue Bedeutung hängt von der ESC-Firmware ab |
| **LINK** | Keine von beiden — unsere eigene Zählung | Gute Pakete pro Sekunde und die Prüfsummenfehlerrate der letzten Sekunde |

Verbrauchte **mAh** erscheinen in der Ecke der Temperaturkachel, und nur mit
KISS.

<img src="img/tester-live.png" width="240" alt="Alle Kacheln gefüllt">

### Die kleine Marke `KISS` / `EDT`

Die Kacheln für Spannung und Strom tragen in ihrer Beschriftungszeile eine
Marke: cyan `KISS`, gedämpft `EDT`. Das ist keine Verzierung. `12,25 V` aus EDT
heißt „irgendwo in einem 0,25-V-Fenster"; derselbe Wert aus KISS heißt „auf
0,01 V genau". Fällt eine Marke von `KISS` auf `EDT` zurück, ist das
Telemetriekabel verstummt und die Anzeige gerade fünfundzwanzigmal gröber
geworden, ohne dass sich die Zahl sichtbar geändert hätte.

---

## Warum eine Kachel `--` zeigt

Jeder Messwert verfällt. Ein Wert, der innerhalb seines Zeitfensters nicht
erneuert wurde, wird zu `--`, statt seine letzte Zahl zu halten, und die
Drehzahl-Ziffern werden dunkelrot.

Das ist wichtiger, als es klingt. Zieh den ESC ab oder tausch ihn gegen einen
anderen, und eine Anzeige, die die alten Werte behält, zeigt keine veralteten
Daten — sie zeigt eine plausible, selbstbewusste Messung von Hardware, die gar
nicht angeschlossen ist. `--` ist die ehrliche Antwort.

<img src="img/tester-stale.png" width="240" alt="Telemetrie verfallen">

`--` heißt also eines von dreien:

- der ESC beherrscht überhaupt kein EDT (Drehzahl bekommst du trotzdem);
- EDT wird beherrscht, ist aber noch nicht eingeschaltet — beobachte das Feld
  auf dem Einstellungsbildschirm und gib ihm einen Moment;
- der ESC hat aufgehört zu antworten.

Eine **verfallene Statuskachel zeigt nie `OK`**. Sie zeigt `--`. Eine
Warnanzeige darf nicht in Richtung „alles in Ordnung" ausfallen.

---

## EDT einschalten

Musst du nicht. Das macht die Firmware, und zwar immer wieder.

Die Regel lautet „funktioniert es", nicht „haben wir gefragt": solange ein ESC
eRPM liefert und eine Sekunde lang kein EDT-Frame eingetroffen ist, geht die
Freigabe jede Sekunde erneut hinaus. Bricht die Verbindung ab, wird der Zustand
zurückgesetzt — ein später angeschlossener, neu gestarteter oder ausgetauschter
ESC bekommt seine eigene Freigabe, statt den Rest der Sitzung stillschweigend
ohne Telemetrie zu laufen.

Ein Versuch wird nur unternommen, wenn der ESC ihn auch ausführen könnte: die
Verbindung steht lange genug, dass der ESC fertig gestartet ist, der Tester ist
entschärft, der Motor steht still, und es liegt kein anderer Befehl auf dem
Kabel. Das ist wichtig, weil ein DShot-Befehl erst ausgeführt wird, nachdem er
in einer Folge gleicher Frames angekommen ist — ein Versuch zum falschen
Zeitpunkt ist ein verschenkter Versuch.

<img src="img/config-edt-on.png" width="240" alt="EDT ON"> <img src="img/config-edt-off.png" width="240" alt="EDT OFF">

Bleibt das Feld rot, obwohl der ESC EDT nachweislich beherrscht, siehe
[Fehlersuche](Fehlersuche#edt-bleibt-aus).

---

## Das optionale dritte Kabel

<img src="img/tester-kiss.png" width="240" alt="Kacheln aus KISS-Telemetrie gespeist">

Die meisten ESCs haben ein Telemetrie-Pad, das auf Anforderung ein 10-Byte-Frame
mit 115200 Baud sendet. Es anzuschließen bringt feinere Spannung und Strom sowie
den Verbrauch in mAh.

| | 2,0"-Platine | 2,8"-Platine |
|---|---|---|
| **Telemetrie-RX** | GP5 — Stiftleiste P1, Pin 10 | GP28 — J4 Pin 11 |

Es wird nur empfangen — die Platine treibt diese Leitung nie — und jeder freie
GPIO genügt. Einschalten unter **CFG → SETUP → KISS TELEM**, den Pin in der
Zeile darunter setzen. Der Pin-Schrittschalter überspringt den Pin des
ESC-Signals, sodass beide nicht kollidieren können.

Wird das Kabel während des Betriebs abgezogen, fallen die Marken auf `EDT`
zurück und die Anzeigen werden gröber. Sie frieren nicht auf dem letzten feinen
Wert ein, aus demselben Grund, aus dem die Kacheln leer werden: eine ruhige,
präzise Zahl von einem Sensor, der aufgehört hat zu melden, ist das Schlechteste
aus beidem.

---

## LINK, und wie eine schlechte Verbindung aussieht

`998/S` bei `0% ERR` ist eine gesunde 1-kHz-Verbindung. Die Fehlerrate wird
oberhalb von 5 % bernsteinfarben.

Eine schlechte Verbindung liegt meist am Signalkabel: zu lang, ungeschirmt oder
neben etwas Störendem verlegt. Zuerst kürzen. Geht das nicht, stelle im SETUP
**DSHOT KBAUD** auf 300 — die LINK-Zeile auf diesem Bildschirm ist live, du
siehst die Wirkung also ohne die Seite zu verlassen.
