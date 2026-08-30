# Touch-/Audio-Probe: Handlungsliste

Stand: 2026-08-30 – abgeschlossen

Ziel: Die sporadische Touch-Bedienung der `M5Tab5_Audio_Probe` reproduzierbar
analysieren und stabilisieren. Jede Aktion wird erst nach einem nachvollzieh-
baren Test als erledigt markiert.

## Statusdefinition

- `[ ]` offen
- `[~]` in Arbeit
- `[x]` erledigt
- `[!]` benötigt einen manuellen Gerätetest

## Evidenz vor Beginn

- `[x]` `M5Tab5_Touch_Probe` erkennt Touch dauerhaft.
- `[x]` Die Audio-Probe erkennt den ersten Touch mit `source=M5Unified`.
- `[x]` Danach bleiben `touch=enabled` und `driver=present`, aber es erscheinen
  teilweise keine weiteren `TOUCH_RAW`-Ereignisse.
- `[x]` Während langer Aufnahmen wurde ein Brownout (`E BOD`) beobachtet.
- `[x]` Die Audio-Probe verwendet deutlich mehr Touch-/Display-/Audio-
  Zustandslogik als die funktionierende Touch-Probe.

## Zusammenfassung der vorangegangenen Analyse

### Vergleich der Projekte

Die funktionierende `M5Tab5_Touch_Probe` ist technisch bewusst einfach:

- Touch wird einmal über `M5.Touch.begin()` initialisiert.
- `M5.update()` wird regelmäßig und ohne lange blockierende Abschnitte aufgerufen.
- Die Anwendung liest Touchdaten, erkennt die Flanke „nicht gedrückt -> gedrückt“
  und führt anschließend die Aktion aus.
- Anzeigeaktualisierungen sind klein bzw. klar vom Touch-Leseweg getrennt.

Die anderen funktionierenden Projekte folgen demselben Grundmuster: Touch wird
vor einer eventuellen Display-Redraw-Drosselung verarbeitet. Die Audio-Probe
wich davon ab:

- Sie enthielt zusätzliche Audioausgabe und wiederholte Touch-Recovery.
- GPIO23 wurde zeitweise als Ausgang behandelt, obwohl GPIO23 beim Tab5 als
  Touch-Interrupt-Leitung verwendet wird.
- Vollbild-Redraws, Audioaufnahme, I²C-Zugriffe und Touch-Reinitialisierung
  lagen im selben Bedienpfad.
- Die Aufnahme ist zeitweise blockierend und kann die normale Touch-Abfrage
  verzögern.
- Ein angezeigter Zustand `enabled/driver=present` bewies nicht, dass der
  ST7123-Touchcontroller noch tatsächlich über I²C antwortet.

### Fehlerbild

Das Fehlerbild war nicht konstant: Nach einem Neustart waren mehrere Touches
möglich, manchmal wurde bereits nach dem ersten Touch kein weiterer erkannt.
Nach einer Aufnahme oder einem Ergebnisbild trat der Fehler ebenfalls auf.
Serielle Touch-Injektionen konnten die Zustandsmaschine dagegen wiederholt
durchlaufen. Daraus folgt:

1. Ein einfacher Hit-Test- oder Buttonkoordinatenfehler ist unwahrscheinlich.
2. Ein reiner UI-Zustandsfehler ist nicht die alleinige Ursache.
3. Der physische Touch-Leseweg bzw. der Touchcontroller muss getrennt vom
   Aktions- und Displaypfad geprüft werden.

### Technische Ursachenhypothesen

Die Hypothesen wurden nach ihrer Aussagekraft priorisiert:

1. **ST7123/I²C nicht mehr erreichbar.** Das ist durch den Bus-Test im
   Fehlerzustand konkret belegt: Der IO-Expander `0x43` antwortete, der
   ST7123 an `0x55` jedoch weder über Hardware- noch über Software-I²C.
2. **Störung durch den Audio-/Mikrofonpfad.** Während längerer Aufnahmen wurde
   ein ESP32-Brownout (`E BOD`) beobachtet. Ein Spannungseinbruch kann den
   Touchcontroller oder den gemeinsamen Bus zurücksetzen.
3. **Gemeinsamer I²C-Bus und Recovery.** Mikrofon, IO-Expander und Touch teilen
   Ressourcen. Eine Recovery während oder unmittelbar nach Audiooperationen
   kann den Touchbus in einem ungültigen Zustand hinterlassen.
4. **GPIO23-Konflikt.** GPIO23 darf nicht als normaler Ausgang geschaltet
   werden, weil er die Touch-Interrupt-Leitung ist.
5. **Mehrere Touch-Lesewege.** M5Unified und ein direkter M5GFX-Fallback dürfen
   nicht konkurrierend denselben Controller lesen, solange nicht nachgewiesen
   ist, dass beide Wege gefahrlos kombinierbar sind.
6. **Display-Refresh/Timing.** Vollbildaktualisierungen können die Reaktion
   verzögern, erklären aber allein keinen fehlenden ST7123-ACK.
7. **Edge-Filter.** Ein Filter kann eine Aktion unterdrücken, erklärt aber
   ebenfalls keinen fehlenden Controller-ACK. Er ist deshalb erst nach der
   Hardware-/Busprüfung zu bewerten.

### Was die bisherigen Tests beweisen – und was nicht

- Die 7 Host-Unit-Tests prüfen WAV-Daten, Audiometrik und ein modelliertes
  Touch-Flankenverhalten. Sie prüfen nicht den realen ST7123.
- Der erfolgreiche Build prüft Syntax, Typen und Linkage, nicht das Verhalten
  des angeschlossenen Geräts.
- Serielle `TOUCH`-Befehle prüfen die UI-Aktionslogik. Sie ersetzen keinen
  Fingerkontakt und umgehen den physischen Touchcontroller.
- `touch=enabled` und `driver=present` zeigen nur den Softwarezustand. Der
  separate Bus-Test ist für die tatsächliche Erreichbarkeit maßgeblich.
- Der erfolgreiche Touch-Test im separaten Touch-Probe-Projekt zeigt, dass
  Display und Touchhardware grundsätzlich funktionieren können. Er schließt
  eine Störung im Audio-Probe-Pfad nicht aus.

### Konsequenz für die weitere Bearbeitung

Die weitere Bearbeitung muss als A/B-Isolation erfolgen und darf nicht aus
aufeinanderfolgenden, gleichzeitig wirksamen Änderungen bestehen:

- zuerst Touch-Probe ohne Mikrofon- und Recovery-Einflüsse,
- danach Display-Refresh isoliert,
- danach kurze Mikrofonaufnahme,
- anschließend Bus-ACK und echte Koordinatenlieferung vergleichen.

Eine Meldung `driver=ready` wird erst dann als erfolgreiche Recovery gewertet,
wenn der ST7123 anschließend wieder per I²C antwortet und reale Touchdaten
liefert.

## Arbeitsplan

### A. Reproduzierbare Diagnose

- `[x]` Zeitstempel und UI-Zustand in jede Touch-/Display-Diagnose aufnehmen.
- `[x]` Display-Refreshdauer, Touchstatus und Controllerzustand im laufenden
  Status erfassen.
- `[x]` Controller-ACK, ST7123-Statusregister und Koordinatenlieferung trennen.
- `[x]` Touchfehler ohne Mikrofon reproduzieren: Nach dem ersten Feld-Touch
  wurde `STATE=DETAIL` ausgegeben, unmittelbar danach aber ein
  `E BOD: Brownout detector was triggered` beobachtet.
- `[x]` Touchfehler nur mit Mikrofon reproduzieren; während einer Aufnahme
  wurden fehlende Touchdaten und ein Brownout beobachtet.

### B. Software-Isolation

- `[x]` M5Unified-Touch ohne direkten M5GFX-Fallback testen.
- `[x]` Einen einzigen Touch-Owner für `M5.update()` und `touchPoll()` herstellen.
- `[x]` Eigenen Edge-Filter gegen `wasPressed()` vergleichen.
- `[x]` Vollbildaufbau beim Boot und zustandsbezogene Teilupdates vergleichen.
- `[x]` Touch-Recovery aus dem normalen Anzeigeweg heraushalten.

### C. Audio-/Aufnahmepfad

- `[x]` Aufnahme ohne Audioausgabe und ohne unnötige I²C-Recovery testen.
- `[x]` Touch während blockweise fortgeschriebenem `M5.Mic.record()`
  verifizieren.
- `[x]` `M5.Mic.end()` und anschließende Touch-Erreichbarkeit prüfen.
- `[x]` Brownout im refaktorierten Aufnahmeweg prüfen; keiner trat in den
  Abnahmen auf.

### D. Tests und Abschluss

- `[x]` Host-Unit-Tests ausführen und um Touch-Zustandsfälle ergänzen.
- `[x]` Firmware kompilieren und ohne Upload auf Warnungen/Fehler prüfen.
- `[x]` Bereits geflashte Firmware mit Boot-/Busdiagnose seriell geprüft;
  ein neuer Diagnoseflash ist erst nach der nächsten Quellcodeänderung nötig.
- `[x]` Manueller Test: mehrere Feldwechsel ohne Aufnahme.
- `[x]` Manueller Test: simulierte Aufnahme starten und stoppen.
- `[x]` Manueller Test: simuliertes Ergebnis, Wiederholung und Feldwechsel.
- `[x]` Abschließende Empfehlung erst nach bestandenen Gerätetests geben.

## Abbruch-/Entscheidungskriterien

- `TOUCH_RAW` fehlt und Controller-ACK fehlt: I²C/Reset/Versorgung prüfen.
- `TOUCH_RAW` fehlt, Controller-ACK ist vorhanden: ST7123-Zustand bzw. Display-
  und Touch-Leseweg isolieren.
- `TOUCH_RAW` ist vorhanden, aber keine Aktion folgt: Edge-Filter/Hit-Test prüfen.
- Brownout tritt auf: Erst Versorgung/Audiopfad stabilisieren, danach Touch
  weiter bewerten.

## Verifizierte Touch-only-Baseline vom 29.08.2026

Die Isolation hat zwei voneinander unabhaengige Fehlerbedingungen belegt:

1. Bei einer Touchabfrage etwa alle 5 ms verstummte die physische
   Touch-Datenquelle nach 51 Ereignissen, waehrend CPU, Heap und serieller
   Zustandsautomat weiterliefen. Mit einer Begrenzung auf 20 ms wurden in
   einem A/B-Test 213 Touches ohne Ausfall erkannt.
2. Ein Flash-/CPU-Reset setzte den separat versorgten ST7123 nicht verlaesslich
   zurueck. Software meldete dann weiterhin `enabled` und `driver=present`,
   aber GPIO23 zeigte keine Flanke und es kamen keine Koordinaten. Ein
   einmaliger Hardwareimpuls ueber TP_RST beim Boot erzeugt nun einen
   definierten Ausgangszustand.

Die damalige Kurzzeit-Baseline ohne Speaker, Mikrofon, SD und Laufzeit-Recovery
bestand ohne manuellen Reset 109 Touches in rund elf Minuten. Davon wurden 106
UI-Ereignisse angenommen und drei wegen der aktuellen Zustandszone korrekt
abgewiesen. Endzustand: Gate frei, GPIO23 High, 278 Interruptflanken,
Touchcache leer, Heap konstant bei 423688 Byte, kein Brownout und kein Reset.

### Renderer- und Langzeitabnahme

Ein weitergehender Leerlauftest zeigte, dass der ST7123 auch ohne
Koordinaten-Polling nach laengerer Zeit NACK liefern kann. Der finale
Touch-Service liest Koordinaten deshalb nur bei GPIO23 Low, prueft jedoch alle
aktuell zwei Sekunden das Firmware-Register. TP_RST wird ausschliesslich nach NACK
oder ungueltiger Firmwareantwort ausgeloest.

Dieser Stand bestand 120 serielle Renderer-Teilupdates und anschliessend 118
physische Touches ueber rund 3,65 Stunden. Acht Controllerausfaelle wurden vom
Healthcheck erkannt und vollstaendig wiederhergestellt. Endwerte: 2631
Healthchecks, 1046 Interruptflanken, Heap 423640 Byte, maximal 39,5 ms fuer den
Boot-Vollaufbau, kein CPU-Reset und kein Brownout.

### Passive Audiovorbereitung

In der naechsten Isolationsstufe wurden ausschliesslich die Mikrofonparameter
gesetzt und zwei PSRAM-Puffer reserviert. Mikrofonstart, Aufnahme und Stop
blieben per Quelltexttest ausgeschlossen. Neun Hosttests, der Tab5-Build und
125 serielle Zustandswechsel bestanden.

Die physische Langzeitabnahme erreichte rund 13,6 Stunden und 367 Touches ohne
CPU-Reset, Brownout, Heap- oder PSRAM-Verlust. Der ST7123 fiel in dieser Zeit
34-mal voruebergehend aus; alle 34 Fehler wurden vom Firmware-Healthcheck
erkannt und durch TP_RST behoben. Die passive Audiokonfiguration hat die
Touch-Erreichbarkeit damit nicht verschlechtert.

### Mikrofon-Lebenszyklus ohne Aufnahme

Mikrofonstart und -stop werden nun ausschliesslich als zentrale
`EffectRequest`-Folgen aus den Zustandsuebergaengen erzeugt. Jeder Effekt
protokolliert Dauer, Erfolg, Resetursache, GPIO23, Heap und freien PSRAM. Ein
Quelltexttest stellt sicher, dass `record()` in dieser Stufe nicht vorkommt.

20 seriell und 12 per Touch ausgeloeste Start-/Stop-Zyklen bestanden. Alle
32 Starts und 32 Stopps waren erfolgreich. Die Touchabnahme verarbeitete 48
von 48 Ereignissen; der jeweils naechste Touch nach `M5.Mic.end()` blieb
erreichbar. Es gab keinen Reset, Brownout, Touch-Watchdog-Eingriff oder
Speicherverlust.

### Kurze blockweise Audioaufnahme

Die Aufnahme laeuft nun als `tick()`-Service ohne blockierende Warteschleife.
Ein Durchlauf besteht aus maximal vier 250-ms-Bloecken. Touch, serieller
Adapter und Touch-Healthcheck werden vor jedem Audio-Tick bedient. Ein
manueller Stop beendet Mikrofon und UI auch waehrend eines laufenden Blocks;
der unvollstaendige Block wird nicht als abgeschlossen gezaehlt.

Die finale Abnahme bestand 20 automatische und fuenf physische Aufnahmen.
Alle 25 Mikrofonstarts und -stopps waren erfolgreich, 91 Bloecke wurden
abgeschlossen und drei laufende Bloecke kontrolliert abgebrochen. Alle 16
physischen Touches wurden angenommen. Es trat kein Audiofehler, Reset,
Brownout, ST7123-NACK, Touch-Recovery oder Speicherverlust auf. Ein im ersten
Versuch nach einer abgeschlossenen Aufnahme beobachteter, eigenstaendiger
ST7123-NACK wurde korrekt wiederhergestellt; um den wahrgenommenen Stillstand
zu begrenzen, prueft der Watchdog nun alle zwei statt alle fuenf Sekunden.

### Vollaufnahme und Pegelauswertung

Die volle Aufnahme verwendet weiterhin denselben nichtblockierenden
250-ms-Service und schreibt jeden abgeschlossenen Block in den vorab
reservierten 30-s-PCM-Puffer. Pegelstatistiken werden beim Kopieren
inkrementell gebildet; nach Aufnahmeende entsteht dadurch keine lange
Auswertungspause. Zu kurze Dauer und geringe Lautstaerke sind getrennte
Ergebnisse. `PEGELRESERVE` wird nur bei Near-Full-Scale- oder Clippingdaten
gesetzt.

Der automatische ruhige 30-s-Test erreichte 480.000 Frames, 15.811,6 Hz
effektive Rate, RMS 43/44 und null Clipping bei `level_warning=no`. Der
physische Test wurde nach 324.000 Frames beziehungsweise 20,25 s per Touch
gestoppt und bestaetigt. Ergebnis: 15.813,5 Hz, RMS 51/58, keine
Near-Full-Scale-Samples, keine Clippingereignisse und keine Pegelwarnung.
Touch blieb auch nach langer Wartezeit ohne CPU-Reset bedienbar.

## Historischer Befund vor dem Refactoring

Der Fehler tritt bereits beim ersten Wechsel von `LIST` nach `DETAIL` auf,
also noch vor dem Start einer Mikrofonaufnahme:

```text
TOUCH_RAW down source=M5Unified ... action=1 selected=1
STATE=DETAIL TEST=1/6
TOUCH_RAW release ...
E BOD: Brownout detector was triggered
```

Damit ist die bisherige Annahme „erst der Mikrofonpfad verursacht den Fehler“
widerlegt. Der Audio-/Mikrofonpfad bleibt als zusätzlicher Risikofaktor bestehen,
ist aber nicht notwendig, um den Absturz auszulösen.

Der Vergleich mit der funktionierenden Touch-Probe zeigt den wesentlichen
Strukturunterschied:

| Punkt | Touch-Probe | Audio-Probe |
|---|---|---|
| Touch-Abfrage | `M5.update()` -> `getCount()` -> `getDetail()` | grundsätzlich gleich, aber mit zusätzlichem Fallback, Diagnose und Zustandslogik |
| Loop-Takt | ca. 5 ms | ca. 15 ms plus Diagnose-/Aktionspfad |
| Anzeige nach Touch | optischer Teil-Refresh | vollständiges `drawScreen()` mit Bildschirm-Löschung und kompletter Liste |
| Audio/Mikrofon | nicht initialisiert | M5-Mikrofon konfiguriert und später gestartet/gestoppt |
| Touch-Recovery/I²C | keine | zusätzliche I²C-/Controller-Recovery vorhanden |
| Ergebnis | mehrere Touches stabil | erster Touch kann Reset/Brownout auslösen |

Die Touch-Abfrage selbst ist damit nicht der erste Verdächtige. Die nächste
Software-Isolation muss den ersten Feldwechsel mit minimaler Anzeigeaktualisierung
testen: zunächst keine vollständige Bildschirm-Löschung, danach einzeln die
Mikrofoninitialisierung und die Recovery-Pfade. Ein `driver=present` nach dem
Neustart ist dabei nur ein Softwareindikator; entscheidend sind Touchereignis
und Resetfreiheit nach dem ersten Redraw.

## Abschließender Messstand

- Neun Hosttests und der Build für `esp32:esp32:m5stack_tab5` bestehen.
- Touch, serieller Adapter und Gerätefunktionen verwenden denselben zentralen
  Zustands- und Effektpfad.
- Die 20–30-s-Aufnahme läuft in 250-ms-Blöcken; Touch bleibt start-, stop- und
  wiederholbar.
- Leise Aufnahmen erzeugen keine falsche Pegelreserve-Warnung.
- WAV wird in 16-KiB-Schritten geschrieben und danach anhand von Dateigroesse
  und Header zurückgelesen. Der finale Gerätetest schrieb und verifizierte
  304.000 PCM-Datenbytes fehlerfrei.
- Ein physischer Touch nach WAV-Ausgabe und rund 54 Minuten Leerlauf wechselte
  korrekt von `LIST` nach `DETAIL`. Zwei ST7123-NACKs wurden in dieser Zeit
  automatisch behoben; kein CPU-Reset oder Brownout trat auf.

## Ergebnis

Die ursprüngliche Kopplung von Touch, Vollbild-Redraw, Audio und Recovery ist
aufgelöst. Der normale Bedienpfad enthält nur einen Touch-Owner und keine
blockierende Audio- oder Speicherschleife. Die einzige Recovery ist der
gezielte TP_RST nach einem tatsächlich fehlgeschlagenen ST7123-Healthcheck.
Damit sind die geplanten Isolations- und Refactoring-Schritte abgeschlossen.
