# Touch-Refactoring und schrittweise Hardware-Isolation

Stand: 2026-08-29

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

Status: `[ ]`

- Vollständige statische Sechs-Zeilen-Oberfläche aktivieren.
- Zustandsabhängige Inhalte und Fortschrittsanzeige ergänzen.
- Vollbildaufbau nur beim Start; danach gezielte Teilupdates.
- Dauer jedes Display-Updates protokollieren.

Abnahme:

- Touch-only-Abnahme erneut vollständig bestehen.
- Kein Touch-Rebind und keine Recovery nach dem Zeichnen.

### Schritt 4 – Mikrofon nur konfigurieren

Status: `[ ]`

- Mikrofonparameter setzen und Speicher reservieren.
- `M5.Mic.begin()`, `record()` und `end()` noch nicht aufrufen.

Abnahme:

- Touch-only-Abnahme erneut bestehen.
- Kein Brownout und keine Änderung der Touch-Erreichbarkeit.

### Schritt 5 – Mikrofon-Lebenszyklus

Status: `[ ]`

- `M5.Mic.begin()` und `M5.Mic.end()` als EffectRequests ausführen.
- Noch keine längere Aufnahme speichern.
- Zustand und Resetursache vor und nach jedem Aufruf protokollieren.

Abnahme:

- 20 Start-/Stop-Zyklen ohne Touchverlust oder Brownout.
- Touch bleibt nach jedem `M5.Mic.end()` ohne Recovery bedienbar.

### Schritt 6 – Kurze Audioaufnahme

Status: `[ ]`

- Aufnahme in kurzen, kontrollierten Blöcken aktivieren.
- Audio-Service über `tick()` fortschreiben.
- UI-Loop und Touch-Abfrage bleiben unabhängig ausführbar.

Abnahme:

- Aufnahme per Touch start- und stoppbar.
- 20 Wiederholungen ohne Touchverlust, Brownout oder Controller-NACK.

### Schritt 7 – Vollständige 20–30-s-Aufnahme und Auswertung

Status: `[ ]`

- vollständige Aufnahmedauer aktivieren.
- RMS, Peak, Min/Max, DC, Clipping und effektive Samplerate auswerten.
- Pegelreserve nur aus Pegel-/Clippingdaten ableiten.

Abnahme:

- Alle sechs Testzustände bedienbar.
- Wiederholen, Liste und Stop-Bestätigung funktionieren dauerhaft.
- Leise Umgebung erzeugt keine falsche Pegelreserve-Warnung.

### Schritt 8 – SD/WAV und Abschluss

Status: `[ ]`

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
