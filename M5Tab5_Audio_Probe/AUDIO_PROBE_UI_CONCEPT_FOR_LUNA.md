# Bedienkonzept: Statische Testliste für M5Tab5 Audio-Probe

Status: **Umsetzungsvorgabe für Luna**.

## Grundentscheidung

Der Probe ist eine statische Testliste, keine lineare Assistentenstrecke.
Alle sechs Motor-Audio-Tests sind gleichzeitig sichtbar und einzeln anwählbar.
Der Benutzer kann jeden Test unabhängig starten, stoppen und wiederholen.
Touch ist primär; die serielle Konsole ist ein gleichwertiger Fallback.

## Warum der aktuelle Entwurf ersetzt wird

- Der Hauptinhalt nutzt nur einen kleinen oberen Displaybereich.
- Lange Texte und die dicht gepackte Fortschrittszeile sind schlecht lesbar.
- `ZURUECK` löst heute einen Reset auf Schritt 1 aus; Text und Wirkung stimmen nicht überein.
- Touchs haben kein sichtbares Pressed-Feedback.
- Die UI sagt nicht klar genug, wann Motor, Drehzahl und Aufnahme zu bedienen sind.
- 10 s sind für den Motor-Proof zu kurz; Ziel ist 20–30 s.

## Startseite: alle Tests ohne Scrollen

```text
┌────────────────────────────────────────────────────────────────┐
│ AUDIO-MOTORTESTS                           0 / 6 FERTIG         │
├────────────────────────────────────────────────────────────────┤
│ 1  MOTOR AUS       Hintergrund aufnehmen          [ OFFEN ]     │
│ 2  LEERLAUF         Motor stabil im Leerlauf       [ OFFEN ]     │
│ 3  1000 rpm         Drehzahl konstant halten       [ OFFEN ]     │
│ 4  1500 rpm         Drehzahl konstant halten       [ OFFEN ]     │
│ 5  2000 rpm         Drehzahl konstant halten       [ OFFEN ]     │
│ 6  2500 rpm         Drehzahl konstant halten       [ OFFEN ]     │
├────────────────────────────────────────────────────────────────┤
│ Tippe einen Test für Anleitung, Status und START / STOP.        │
└────────────────────────────────────────────────────────────────┘
```

Jede Zeile ist eine große Touchfläche und zeigt Nummer, Titel, Kurzauftrag und Status.
Statusfarben: `OFFEN` dunkelgrau, `LÄUFT` gelb, `FERTIG` grün, `WARNUNG` orange, `FEHLER` rot.
Eine fertige Zeile bleibt antippbar, damit sie wiederholt werden kann.

## Verbindliche Testliste

Standarddauer: **20 s**. Maximaldauer: **30 s**. Die UI ändert diese Zeiten nicht.

| Nr. | Titel | Vorher tun | Während der Aufnahme |
|---:|---|---|---|
| 1 | Motor aus | Motor aus; Türen/Fenster in Messkonfiguration schließen. | Hintergrund 20 s erfassen. |
| 2 | Leerlauf | Motor starten; stabilen Leerlauf abwarten. | Leerlauf 20 s halten. |
| 3 | 1000 rpm | Etwa 1000 rpm einstellen und stabilisieren. | 20 s konstant halten. |
| 4 | 1500 rpm | Etwa 1500 rpm einstellen und stabilisieren. | 20 s konstant halten. |
| 5 | 2000 rpm | Etwa 2000 rpm einstellen und stabilisieren. | 20 s konstant halten. |
| 6 | 2500 rpm | Etwa 2500 rpm einstellen und stabilisieren. | 20 s konstant halten. |

Nach Test 6 lautet die Anleitung: **„Motor in Leerlauf, dann ausschalten. WAV auf SD speichern?“**

## Detailansicht eines ausgewählten Tests

```text
┌────────────────────────────────────────────────────────────────┐
│ TEST 5 / 6                                      STATUS: OFFEN    │
│ 2000 rpm                                                        │
├────────────────────────────────────────────────────────────────┤
│ VORHER TUN:                                                     │
│ Drehzahl auf etwa 2000 rpm bringen.                             │
│ Erst starten, wenn die Drehzahl stabil ist.                      │
│                                                                  │
│ AUFNAHME: 20 bis 30 Sekunden · 16 kHz · Stereo                  │
│ Letztes Ergebnis: noch keines                                   │
├────────────────────────────────────────────────────────────────┤
│ [ ZUR TESTLISTE ]       [ AUFNAHME START ]       [ ABBRECHEN ] │
└────────────────────────────────────────────────────────────────┘
```

Die Reihenfolge ist immer: `VORHER TUN`, `STARTBEDINGUNG`, `AUFNAHME`, `LETZTES ERGEBNIS`.
Texte sind kurze, bewusst umbrochene Display-Zeilen; kein automatischer Fließtext.

## Aufnahme: Start, Stop und Zeitfenster

`AUFNAHME START` startet nur den gewählten Test. Ein kurzer Bestätigungston endet vor `M5.Mic.begin()`.
Der Bildschirm wechselt vor dem ersten Sample sichtbar nach `LÄUFT`.

```text
┌────────────────────────────────────────────────────────────────┐
│ TEST 5 / 6 · 2000 rpm                         ● AUFNAHME LÄUFT  │
├────────────────────────────────────────────────────────────────┤
│ Drehzahl bei etwa 2000 rpm konstant halten.                     │
│                                                                  │
│                     17,2 s                                      │
│              ████████████░░░░░░░░                                │
│ Mindestdauer 20 s · Zielbereich 20–30 s                          │
├────────────────────────────────────────────────────────────────┤
│ [ DEAKTIVIERT ]       [ AUFNAHME STOP ]       [ DEAKTIVIERT ]   │
└────────────────────────────────────────────────────────────────┘
```

- Stop vor 20 s: orange Bestätigung mit `WEITER AUFNEHMEN` und `STOP BESTÄTIGEN`.
- Stop zwischen 20 und 30 s: beendet den Test und startet die Auswertung.
- Nach 30 s: automatischer Stop.

## Ergebnisansicht

```text
┌────────────────────────────────────────────────────────────────┐
│ TEST 5 / 6 · 2000 rpm                           ✓ FERTIG        │
├────────────────────────────────────────────────────────────────┤
│ Dauer 20,1 s · Rate 15979 Hz · 321600 Frames/Kanal              │
│ CH0 RMS 123 · Peak 2100     CH1 RMS 118 · Peak 1980             │
│ Clipping 0 · Near-FS 0 · Pegelreserve OK                         │
├────────────────────────────────────────────────────────────────┤
│ [ ZUR TESTLISTE ]       [ WIEDERHOLEN ]       [ WAV SPEICHERN ] │
└────────────────────────────────────────────────────────────────┘
```

`FERTIG` gilt nur bei zwei nicht-konstanten Kanälen, Rate innerhalb ±2 % und ohne Capture-Fehler.
Clipping oder Near-Full-Scale ist `WARNUNG`, nicht stiller Erfolg; dann zeigt die UI `WIEDERHOLEN EMPFOHLEN`.
`WAV SPEICHERN` ist erst nach Capture aktiv und zeigt Fortschritt sowie Erfolg oder `SD-FEHLER`.

## Buttons und Feedback

Es gibt immer drei große, sichtbare Touch-Buttons. Sichtbare Fläche und Touch-Zone sind pixelgenau identisch.

| Zustand | Links | Mitte | Rechts |
|---|---|---|---|
| Detail | `ZUR TESTLISTE` | `AUFNAHME START` | `ABBRECHEN` |
| Aufnahme | deaktiviert | `AUFNAHME STOP` | deaktiviert |
| Ergebnis | `ZUR TESTLISTE` | `WIEDERHOLEN` | `WAV SPEICHERN` |
| Fehler | `ZUR TESTLISTE` | `ERNEUT VERSUCHEN` | `ABBRECHEN` |

`ABBRECHEN` verlangt einen zweiten Bestätigungsschirm.
`ZURUECK` darf nur die Ansicht verlassen und niemals Testdaten löschen oder auf Test 1 zurücksetzen.

Jeder gültige Touch liefert innerhalb von 150 ms:

1. Der berührte Button erhält mindestens 180 ms die Pressed-Akzentfarbe.
2. Eine große Feedbackzeile zeigt zum Beispiel `TEST 5 GEWÄHLT`, `AUFNAHME STARTET` oder `STOP BESTÄTIGT`.
3. Ein kurzer Bestätigungston ertönt; während Capture gibt es keine Töne.

## Schrift und Layout

- Kein sichtbarer Text kleiner als `TextSize(2)`.
- Seitentitel, Testtitel, Zeitcounter, Status und Buttons mindestens `TextSize(3)`.
- Listenansicht: Header maximal 12 % der Höhe, sechs Testzeilen mindestens 78 %, Hinweis maximal 10 %.
- Detail-, Aufnahme- und Ergebnisansicht: Header 12 %, Inhalt 58 %, Buttons 30 % der Höhe.
- Kein `TextSize(1)`, kein großer Leerraum und keine dicht gepackte Gesamtfortschrittszeile.

## Serielle Alternative

Die Konsole bedient denselben Zustand wie Touch.

| Touchaktion | Serieller Befehl |
|---|---|
| Test auswählen | `SELECT 1` bis `SELECT 6` |
| Aufnahme starten | `START` |
| Aufnahme stoppen | `STOP` |
| Zur Testliste | `LIST` |
| Test wiederholen | `REPEAT` |
| WAV speichern | `WAV` |
| Status | `STATUS` |
| Abbruch bestätigen | `ABORT CONFIRM` |

`LIST` gibt alle sechs Tests mit Status aus. Antworten enthalten Testnummer und Folgezustand, etwa `STATE=CAPTURING TEST=5/6`.

## Technische Umsetzungsvorgaben

- Der aktuelle einzelne 10-s-`M5.Mic.record(...)`-Aufruf kann kein echtes `STOP` umsetzen.
- Luna verwendet vorab reservierte kurze PCM-Abschnitte, etwa 250 ms, kopiert sie in den vorab reservierten PSRAM-Zielpuffer und pollt zwischen Abschnitten Touch und Konsole.
- Das ist **kein** `AudioBlockPool` und kein `AudioWriterTask`.
- Der PSRAM-Puffer ist für 30 s Stereo-PCM auszulegen. Im Capture-Pfad gibt es keine Allokation und keinen SD-Zugriff.
- Stop beendet nach dem aktuellen kurzen Abschnitt; Dauer und Samplezahl verwenden nur tatsächlich erfasste Frames.
- Speaker und Mic laufen nie gleichzeitig. UI-Töne enden vor `M5.Mic.begin()` oder nach `M5.Mic.end()`.
- Kein Analyzer-Code, DLOG, `AudioBlockPool`, `AudioWriterTask` oder automatische Sprotz-Erkennung.

## Abnahmekriterien für Luna

1. Nach dem Boot ist die komplette Sechs-Test-Liste ohne Scrollen sichtbar.
2. Jeder Test besitzt feste Nutzeranweisungen und ist unabhängig wählbar.
3. Jeder Test ist per Touch und seriell start- und stoppbar.
4. Listenstatus und Detailstatus sind konsistent.
5. Jeder Touch zeigt Pressed-Farbe, Feedbacktext und zulässigen Ton.
6. Während Capture zeigt die UI Sollaktion, Zeit, Zielbereich 20–30 s und Fortschrittsbalken.
7. Alle sichtbaren Texte sind groß; kein `TextSize(1)`.
8. Ergebnis, Warnung und Fehler sind ohne Konsole verständlich.
9. Build für `esp32:esp32:m5stack_tab5`, Hosttests und ein Target-Smoketest für `SELECT`, `START`, `STOP`, Wiederholung und WAV bestehen.
