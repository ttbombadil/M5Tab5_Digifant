# Touch-Refactoring und schrittweise Hardware-Isolation

Stand: 2026-08-30 – abgeschlossen

## Ziel

Die Bedienoberfläche der `M5Tab5_Audio_Probe` soll alle Zustände dauerhaft
per Touch durchlaufen können. Hardwarefunktionen werden anschließend einzeln
zugeschaltet. Die erste Stufe, bei der Touch instabil wird, grenzt die Ursache
ein.

Der Touch-Test verwendet dabei nicht mehr eine separate Spielzeugoberfläche,
sondern denselben Zustandsautomaten und denselben Renderer wie die spätere
Audio-Probe.

## Verbindliche Architekturregeln

1. Nur der Arduino-`loop()` ruft `M5.update()` auf.
2. Nur ein Touch-Adapter liest `M5.Touch`.
3. M5Unified und direkter M5GFX-Touch werden nicht parallel verwendet.
4. Touch und serielle Testbefehle erzeugen dieselben `UiEvent`-Ereignisse.
5. Der Zustandsautomat enthält keine Display-, Audio-, SD- oder I²C-Aufrufe.
6. Der Renderer zeichnet nur, wenn der UI-Zustand als geändert markiert ist.
7. Ein Display-Refresh darf keine Touch-Reinitialisierung auslösen.
8. Touch-Recovery wird nur nach einem nachgewiesenen Controllerfehler ausgeführt.
9. Audioaufnahme wird als schrittweise bearbeiteter Service implementiert;
   keine verschachtelten Aufrufe von `touchPoll()` in Audio-Warteschleifen.
10. Pro Isolationsstufe wird genau eine neue Hardwarewirkung freigeschaltet.

## Zielstruktur

```text
TouchAdapter ──┐
SerialAdapter ─┼─> UiEvent ─> UiController ─> UiState
TestAdapter ───┘                    │
                                    └─> EffectRequest

EffectRunner ──> Renderer
             ├─> AudioService
             ├─> StorageService
             └─> TouchRecovery
```

## Einheitliche Zustandsfolge

```text
LIST
  -> DETAIL
  -> CAPTURING
  -> STOP_CONFIRM
  -> RESULT
  -> DETAIL oder LIST
```

In den ersten Stufen sind `CAPTURING` und `RESULT` simuliert. Dadurch lässt
sich die vollständige Bedienlogik testen, ohne Mikrofon-, I²C- oder SD-Effekte
auszulösen.

## Messgrößen jeder Stufe

- erkannte physische Touch-Flanken
- angenommene und verworfene UI-Ereignisse
- aktueller und vorheriger UI-Zustand
- GPIO23-Flanken
- ST7123-ACK nur bei expliziter Diagnose
- Boot-/Reset-Zähler und Resetursache
- Anzahl und Dauer der Display-Updates
- Audio-Start, Audio-Stop und Aufnahmeblöcke ab der jeweiligen Stufe

Serielle Ereignisse beweisen nur den Zustandsautomaten. Der physische
Touchcontroller gilt erst nach einem manuellen Dauertest als bestanden.

## Stufenplan

### Schritt 0 – Gesicherter Ausgangspunkt

Status: `[x]`

- Commit des bisherigen Audio-/Touch-Stands erstellen.
- Generierte Build- und Cache-Verzeichnisse entfernen.
- Fachfremde Änderungen im Worktree unangetastet lassen.

Abnahme:

- Sicherungscommit vorhanden.
- Quellcode, Tests und Dokumentation sind weiterhin verfügbar.
- Generierte Dateien sind nicht Bestandteil des Git-Stands.

### Schritt 1 – Reiner Zustandsautomat

Status: `[x]`

- `UiState`, `UiEvent` und Übergangsfunktion aus dem Sketch lösen.
- Übergangsfunktion ohne Arduino- oder M5-Abhängigkeit implementieren.
- Simulation der Aufnahme und des Ergebnisses einbauen.
- Hosttests für alle erlaubten und abgelehnten Übergänge ergänzen.

Abnahme:

- Jede Zustandsfolge ist per Unit-Test vollständig durchlaufbar.
- Ungültige Ereignisse verändern den Zustand nicht.
- Kein Hardwareaufruf befindet sich im Zustandsautomaten.

### Schritt 2 – Touch-only-Baseline

Status: `[x]` – automatische und physische Abnahme bestanden. Bei etwa 200 Touchabfragen/s
verstummte die Touch-Datenquelle nach 51 Ereignissen. Mit 50 Hz wurden 213
Touches ohne Ausfall erkannt; CPU, Heap, Press-Gate und Interruptleitung
blieben stabil. Speaker-Initialisierung war wirkungslos und ist entfernt.
Ein Flash-/CPU-Reset liess den separat versorgten Controller jedoch in einem
undefinierten Zustand. Mit einmaligem TP_RST-Impuls beim Boot wurden ohne
manuellen Reset 109 Touches in rund 11 Minuten erkannt. Gate, Interrupt,
Heap und CPU blieben stabil; Lautsprecher und Mikrofon waren deaktiviert.

- Nur `M5.begin()`, Display und M5Unified-Touch aktivieren.
- Lautsprecher, Mikrofon, SD, Busdiagnose und Recovery deaktivieren.
- Einen einzigen Touch-Leseweg verwenden. Wie in der nachweislich stabilen
  Touch-Probe wird die Druckflanke aus `isPressed()` und der Freigabe gebildet.
- Touchcontroller hoechstens alle 20 ms abfragen; Interruptflanken und den
  gecachten M5Unified-Status passiv protokollieren.
- Den separat versorgten Touchcontroller bei jedem Programmstart einmal ueber
  TP_RST zuruecksetzen und erst danach an M5Unified binden.
- Serielle Ereignisinjektion über denselben `UiEvent`-Pfad führen.
- Nur notwendige optische Teilaktualisierungen zeichnen.

Abnahme automatisch:

- Build erfolgreich.
- Zustandsfolge mehrfach seriell ohne Abweichung durchlaufen.
- Kein Reset und kein Zustandsverlust im seriellen Leerlauftest.

Abnahme manuell:

- Mindestens 100 einzelne Touches beziehungsweise 20 vollständige
  Zustandsrunden ohne Ausfall.
- Kein Brownout, Neustart oder dauerhaft fehlender Touch.

### Schritt 3 – Vollständiger Renderer

Status: `[x]` – Renderer und Touch-Watchdog bestanden. Der Renderer bestand 120 serielle Teilupdates, danach lieferte
der ST7123 nach 58.918 blinden Leerlaufabfragen keine Touchdaten. Der
Touch-Service wurde auf GPIO23-gesteuertes Lesen umgestellt. Trotzdem fiel der
ST7123 nach rund 38 Minuten bei exakt null Datenabfragen aus. Aktueller Test:
Firmware-Liveness aktuell alle 2 s; TP_RST nur nach NACK/ungueltiger Antwort. In der
abschliessenden Abnahme wurden ueber rund 3,65 Stunden 118 Touches erkannt.
Alle acht Health-Ausfaelle wurden automatisch wiederhergestellt; kein
CPU-Reset, Brownout, Heapverlust oder dauerhafter Touchausfall trat auf.

- Vollständige statische Sechs-Zeilen-Oberfläche aktivieren.
- Zustandsabhängige Inhalte und Fortschrittsanzeige ergänzen.
- Vollbildaufbau nur beim Start; danach gezielte Teilupdates.
- Dauer jedes Display-Updates protokollieren.

Abnahme:

- Touch-only-Abnahme erneut vollständig bestehen.
- Kein Touch-Rebind und keine Recovery nach dem Zeichnen.

### Schritt 4 – Mikrofon nur konfigurieren

Status: `[x]` – passive Audiovorbereitung und Langzeitabnahme bestanden. Die
Mikrofonparameter wurden gesetzt und 1.920.000 Byte Zielpuffer sowie 16.000
Byte Blockpuffer im PSRAM reserviert. Der Sketch enthaelt in dieser Stufe
keinen Aufruf von `M5.Mic.begin()`, `record()` oder `end()`. In rund 13,6
Stunden wurden 367 physische Touches verarbeitet. Alle 34 erkannten
ST7123-Ausfaelle wurden ohne CPU-Reset wiederhergestellt; Heap und freier
PSRAM blieben konstant.

- Mikrofonparameter setzen und Speicher reservieren.
- `M5.Mic.begin()`, `record()` und `end()` noch nicht aufrufen.

Abnahme:

- Touch-only-Abnahme erneut bestehen.
- Kein Brownout und keine Änderung der Touch-Erreichbarkeit.

### Schritt 5 – Mikrofon-Lebenszyklus

Status: `[x]` – zentraler Mikrofon-Lebenszyklus bestanden. Eintritt in
`CAPTURING` erzeugt genau einen `StartMicrophone`-EffectRequest, Verlassen
genau einen `StopMicrophone`-EffectRequest; aufgenommen wird noch nicht.
Insgesamt bestanden 32/32 Start-/Stop-Zyklen, davon 20 seriell und 12 per
Touch. Die manuellen Zyklen umfassten 48 angenommene Touches. Kein Startfehler,
Reset, Brownout, Touch-Recovery oder Speicherverlust trat auf; das Mikrofon
war am Ende gestoppt.

- `M5.Mic.begin()` und `M5.Mic.end()` als EffectRequests ausführen.
- Noch keine längere Aufnahme speichern.
- Zustand und Resetursache vor und nach jedem Aufruf protokollieren.

Abnahme:

- 20 Start-/Stop-Zyklen ohne Touchverlust oder Brownout.
- Touch bleibt nach jedem `M5.Mic.end()` ohne Recovery bedienbar.

### Schritt 6 – Kurze Audioaufnahme

Status: `[x]` – nichtblockierende Kurzaufnahme bestanden. Der Audio-Service
zeichnet maximal vier Bloecke zu je 250 ms auf und wird nur einmal pro
`loop()` fortgeschrieben. Touch, Healthcheck und Serial laufen vor jedem
Audio-Tick. 20 automatische und fuenf physische Aufnahmen bestanden mit 25/25
Mikrofonstarts und -stopps. 91 Bloecke wurden abgeschlossen; drei beim
manuellen Stop laufende Bloecke wurden kontrolliert abgebrochen. Alle 16
physischen Touches wurden angenommen; kein Audiofehler, Reset, Brownout,
Controller-NACK oder Speicherverlust trat in der finalen Abnahme auf.

- Aufnahme in kurzen, kontrollierten Blöcken aktivieren.
- Audio-Service über `tick()` fortschreiben.
- UI-Loop und Touch-Abfrage bleiben unabhängig ausführbar.

Abnahme:

- Aufnahme per Touch start- und stoppbar.
- 20 Wiederholungen ohne Touchverlust, Brownout oder Controller-NACK.

### Schritt 7 – Vollständige 20–30-s-Aufnahme und Auswertung

Status: `[x]` – Vollaufnahme und inkrementelle Auswertung bestanden. PCM wird
in 250-ms-Bloecken in den 30-s-Zielpuffer kopiert; Min/Max, RMS-Summe, DC,
Near-Full-Scale und Clipping werden dabei fortlaufend aktualisiert. Eine
Pegelreserve-Warnung entsteht nur aus Near-Full-Scale-/Clippingdaten, nicht
aus geringer Lautstaerke oder kurzer Dauer. Der automatische 30-s-Test
erreichte 480.000 Frames bei 15.811,6 Hz ohne Pegelwarnung. Der physische
Touch-Stop erreichte 324.000 Frames beziehungsweise 20,25 s bei 15.813,5 Hz,
RMS 51/58 und null Clipping-/Near-Full-Scale-Ereignissen. Stop-Bestaetigung,
Wiederholen und Testwechsel blieben erreichbar; kein CPU-Reset trat auf.

- vollständige Aufnahmedauer aktivieren.
- RMS, Peak, Min/Max, DC, Clipping und effektive Samplerate auswerten.
- Pegelreserve nur aus Pegel-/Clippingdaten ableiten.

Abnahme:

- Alle sechs Testzustände bedienbar.
- Wiederholen, Liste und Stop-Bestätigung funktionieren dauerhaft.
- Leise Umgebung erzeugt keine falsche Pegelreserve-Warnung.

### Schritt 8 – SD/WAV und Abschluss

Status: `[x]` – chunkweises WAV-Schreiben und Rueckpruefung bestanden. Der
Storage-Service schreibt pro `loop()` hoechstens 16 KiB und haelt Touch,
Serial und Healthcheck dadurch erreichbar. Zwei reale Dateien mit 208.000 und
304.000 PCM-Datenbytes wurden vollstaendig geschrieben. Die finale Datei
wurde erneut geoeffnet; Groesse und 44-Byte-WAV-Header waren korrekt
(`verified=yes`). Nach dem Gesamtablauf reagierte die UI seriell und nach rund
54 Minuten Leerlauf auch physisch. Zwei ST7123-Ausfaelle wurden automatisch
wiederhergestellt; kein CPU-Reset, Brownout oder Speicherfehler trat auf.

- SD-Mount und WAV-Schreiben als letzten Hardwareeffekt ergänzen.
- Fehler beim Schreiben dürfen Touch und Zustandsautomat nicht blockieren.
- README und Handlungsliste auf den verifizierten Endstand bringen.

Abnahme:

- Gesamtablauf einschließlich WAV mehrfach bestanden.
- Keine offenen Touch-Recovery-Hilfskonstruktionen im normalen Bedienpfad.

## Vorgehen bei einem Fehler

Wenn eine Stufe erstmals fehlschlägt:

1. Keine weitere Funktion zuschalten.
2. Fehler mit exakt derselben Bedienfolge mindestens dreimal prüfen.
3. Letzte bestandene und erste fehlerhafte Stufe vergleichen.
4. Nur den neu hinzugekommenen Effekt instrumentieren.
5. Nach der Korrektur die vollständige Abnahme dieser Stufe wiederholen.

## Commit-Regel

Jede bestandene Stufe erhält einen eigenen Commit. Nicht bestandene
Experimente werden nicht mit der nächsten Stufe vermischt. Dadurch bleibt der
letzte nachweislich funktionierende Hardwarestand jederzeit wiederherstellbar.
