# M5Tab5 Digifant Analyzer – Implementierungsplan V2

**Zielprojekt:** `M5Tab5_Digifant_Analyzer/`
**Verbindliche Architektur:** `ARCHITECTURE_V2.md`
**Aufgabe dieses Plans:** bewahrende Migration der funktionierenden Anwendung
auf genau eine RX-/Frame-/Decoder-/Snapshot-Pipeline.

**Status 2026-08-25:** Die funktionale V2-Migration einschließlich V2-015 und
der Logger-/IMU-/DLOG-Gates V2-016 bis V2-019 ist abgeschlossen und in
`M5Tab5_Digifant_Analyzer/verification.md` nachgewiesen. Dieser Plan bleibt als
historischer Ausführungsvertrag erhalten; die nachgelagerte Strukturkonsolidierung
steht in `REFACTORING_REVIEW_SOL_HIGH.md` und ist kein neues V2-TODO.

**Refactoring-Stand 2026-08-25:** Der erste Teil von R6 ist abgeschlossen:
`src/processing_service.h` kapselt Decoder, `MeasurementModel` und Snapshot-
Publikation; `processing_task_entry()` bleibt als unveränderter Runtime-
Taskadapter bestehen. Der zweite R6-Teil ist ebenfalls abgeschlossen:
`src/serial_consumer.h` kapselt Serial-Kommandos, Formatierung und die
bestehenden Diagnoseausgaben; `serial_snapshot_task_entry()` ist nur noch
Runtimeadapter. R7–R9 sind nicht begonnen.

## 1. Ausführungsvertrag

- Die Migration ist kein Rewrite. Funktionierende Transport-, KWP- und
  Decoderlogik bleibt bestehen.
- Vor jeder Änderung wird die entsprechende Lösung im funktionierenden
  `M5Tab5_Digifant_Proto/` und im aktuellen V2-Code geprüft.
- Die Referenz liefert Hardware-/Protokollverhalten, Timingwerte, Telegramme,
  Formeln und Captures. Ihre direkte Console-/Dashboard-Kopplung wird nicht
  übernommen.
- Es wird immer nur der nächste offene Schritt bearbeitet.
- Ein alter Runtime-Pfad wird erst entfernt, nachdem der Ersatz Hosttests und
  den geforderten realen ECU-Regressionslauf bestanden hat.
- Produktivcode wird nur unter `M5Tab5_Digifant_Analyzer/` geändert.
- Nach jedem Schritt werden alle bestehenden Hosttests, Sanitizer und der
  Targetcompile erneut ausgeführt.
- Hardwaretests werden nur als bestanden dokumentiert, wenn sie real liefen.
- Die fehlende Logic-Analyzer-Messung bleibt
  `PENDING-OPTIONAL-HARDWARE-EVIDENCE`; sie blockiert diese Umverdrahtung nicht.
- Ergebnisse werden ausschließlich in
  `M5Tab5_Digifant_Analyzer/verification.md` ergänzt.

## 2. Verbindliches Zielbild

```text
EspUsbHost RX callback
  → RxIngressRing
  → genau ein KWP-Runner
  → genau eine ValidatedFrameQueue
  → genau eine Processing-/Decoder-Task
  → MeasurementModel
  → immutable MeasurementSnapshot
  → eigene Mailbox → Serial Consumer
  → eigene Mailbox → Display Consumer
  → später eigene Mailbox → Bluetooth Consumer
  → später eigene Mailbox → Web Consumer
```

Kein Consumer besitzt einen Rückkanal, auf den KWP oder Decoder warten. Serial,
Display, Bluetooth und Webserver kennen weder RX-Ring noch KWP-Core noch
Decoder.

Der bestehende Langzeitlogger bleibt ein Downstream-Blattpfad. Die optionale
IMU-Erweiterung folgt ausschließlich diesem zusätzlichen Zielbild:

```text
MeasurementSnapshot → bounded LoggerSnapshotQueue ─┐
Tab5 IMU → bounded ImuSampleRing ──────────────────┼→ Logger-Task → DLOG V2
UI START/STOP/MARKER → bounded CommandQueue ───────┘
```

IMU-Daten werden nicht in `MeasurementModel` oder `MeasurementSnapshot`
aufgenommen. Der ECU-/KWP-/Decoderpfad bleibt bei allen folgenden TODOs
unverändert.

Die fachliche Snapshot-Vollständigkeit ist verbindlich: Der aktuelle
unterstützte Umfang besteht aus genau 26 festen ECU-Feldern (10 × Gruppe 000,
4 × jede Gruppe 001–004). MeasurementModel und MeasurementSnapshot bewahren
für jedes Feld Gruppe, Zone, Rawwert, Formel-ID, NWB, optionalen Dekodierwert,
Einheit/Semantik, Status sowie Timestamp, Sequenz, Sessionepoch und
Transportgeneration. Unbekannte Formeln/Bedeutungen bleiben Raw-only. Kein
Consumer darf Daten außerhalb des Snapshots beziehen oder die generische
Feldrepräsentation durch eigene Interpretationen ersetzen.

## 3. Bewahrte Ausgangsbasis

| Bisheriger Schritt | Nachgewiesener Stand | Behandlung |
|---|---|---|
| V2-001 Tab5-Smoke | Build, Upload, Serial und Display real grün | behalten |
| V2-002 K409 | Filter, Connect/Disconnect, Generation real grün | behalten |
| V2-003 Transport | Token, Completion, Retirement, Quiescence, RX-Poison grün | behalten |
| V2-004 KWP/5-Baud | realer ECU-Handshake funktional | behalten; LA optional offen |
| V2-005 Messgruppen | Identifikation und Gruppen 000–004 real grün | behalten |
| V2-006 Capture/Serial | bounded Diagnosequeue funktioniert | als Zwischenstand umverdrahten |
| V2-007 Decoder | Golden-Captures und belegte Formeln grün | behalten |
| V2-008 Snapshot/UI | Typen und Target-Smoke vorhanden | an reale Daten anschließen; 26-Feld-Snapshot nach V2-011/012 |
| V2-015 Entkopplung | einzige RX-/Frame-/Decoder-/Snapshotpipeline real grün | unverändert bewahren |
| Sprotz-DLOG V1 | bounded Snapshotlogger, SD START/MARKER/STOP und Host-Rücklesung grün | als Basis für V2-016–019 bewahren |
| V2-016 IMU-Port | fixed-size 25-Hz-Sampler real grün | bewahren |
| V2-017 Logger-Merge | bounded IMU-/Snapshot-/Command-Merge real grün | bewahren |
| V2-018 DLOG V2 | V1/V2-Format und Host-Rücklesung real grün | bewahren |
| V2-019 gemeinsame Abnahme | ECU, IMU, SD und UI ohne KWP-/Dropfehler real grün | bewahren |

Die vorhandenen Tests werden nicht gelöscht oder abgeschwächt. Tests, deren
alter Aufbau einen zu entfernenden Parallelpfad voraussetzt, werden zuerst auf
die neue öffentliche Grenze portiert und müssen dasselbe Verhalten weiter
prüfen.

Vor den Migrationsschritten werden bevorzugt diese Referenzen gelesen:

| Arbeit | Known-Good-Quelle |
|---|---|
| K409/FTDI und RX | `UsbCdcLink.cpp/.h`, aktueller EspUsbHost-Fork |
| KWP, 5-Baud, Echo/ACK, Gruppenplan | `EcuInitTester.cpp/.h` |
| Frames und Decoder | `ReplayData.h`, reale CSV-Captures, aktuelle Golden-Tests |
| Display | `Dashboard.cpp/.h` ausschließlich als Darstellungsreferenz |

## 4. Gemeinsames Stabilitätsgate

Nach jedem TODO gelten zusätzlich zu dessen gezielten Tests folgende Gates.

### Host

- alle `tests/*_test.cpp` mit C++20, `-Wall -Wextra -Wpedantic -Werror`;
- alle Hosttests mit ASan/UBSan;
- vorhandene Token-, Quiescence-, SPSC-/Poison-, KWP-, MeasurementPlan-,
  Parser-/Decoder- und Golden-Capture-Tests;
- statischer Architekturcheck auf verbotene Abhängigkeiten des aktuellen
  Schritts.

### Targetcompile

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer
```

### Reale ECU-Regression

Jeder Schritt, der Runtime-Verdrahtung, Tasks, Queues oder Consumer verändert,
benötigt anschließend einen 60-s-Lauf mit Tab5, K409 und Digifant-ECU:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT_RESULT=RUNNING
Gruppen 000–004 vorhanden
parser_rejected=0
byte_fault=0
action_failures=0
stale_completions=0
kein RX-Overflow
```

Frame-/Snapshotdrops sind nur in ausdrücklich erzwungenen Sättigungstests
zulässig. Der Lauf dokumentiert Build-ID/FQBN, Port, Zündung/Motorzustand,
Zähler und relevante Ausgabe.

## 5. Migrations-TODOs

### V2-009 – Genau einen produktiven RX-Pfad herstellen **[TARGET, HW-K409, HW-ECU]**

**Ziel:** `RxIngressRing` wird der einzige Übergabepunkt vom USB-Callback zum
KWP-Runner.

**Bestehen bleibt:** EspUsbHost-Fork, K409-Filter, Token-/Completionpfad,
KWP-Core, Byte Engine und MeasurementPlan.

**Umverdrahtung:**

- `on_serial_data()` publiziert ausschließlich epoch-/generationmarkierte
  `RxIngressItem`s in den Ring und weckt den Runner;
- der bestehende Measurement-/KWP-Owner konsumiert ausschließlich `tryPop()`;
- Completion und Disconnect bleiben im getrennten bounded
  `CriticalTransportEventRing`; derselbe Runner ist auch dort einziger
  Consumer und merged die Ereignisse mit RX nach Eventsequenz;
- Original-Batchtimestamp, Generation, Epoche und Eventsequenz werden bis in
  den Core weitergereicht;
- Overflow-Sticky und Poison lösen die vorhandene Session-Recovery aus.

**Entfernt nach GREEN:** produktive Nutzung von `wire_rx`, `wire_rx_count` und
jeder zweite Bytepfad. Testspezifische Injektion benutzt ebenfalls die
öffentliche Ringgrenze statt globaler Arrays.

**Tests zuerst:**

1. Hostintegration speist einen vollständigen bekannten KWP-Dialog nur über
   `RxIngressRing`; direkte Bytezufuhr muss der Test verbieten.
2. Ring voll mitten im Batch: Session faultet, alle Post-Gap-Bytes bleiben bis
   Reset verworfen.
3. Mid-callback-Reopen, alte Epoche/Generation und Notification-Coalescing.
4. Completion-/Disconnect-Eventring voll: sticky Fault, keine rekonstruierte
   Completion und keine neue Wire-Aktion vor Quiescence/Generation-Close.
5. Statischer Test schlägt fehl, solange `wire_rx` im produktiven KWP-Pfad
   gelesen wird.

**Hardwaregate:** Gemeinsames 60-s-ECU-Gate. Zusätzlich müssen
`rx_ingress_drops=0`, kein Poison und ein plausibler High-Watermark gemeldet
werden.

**Akzeptanz:** Genau ein Callbackproducer und ein Runnerconsumer; KWP bleibt
gegen die reale ECU stabil. Erst danach werden die alten RX-Arrays entfernt.

### V2-010 – Genau eine bounded Queue für validierte Frames **[TARGET, HW-ECU]**

**Ziel:** `ValidatedFrameQueue` ist die einzige Framegrenze zwischen KWP und
Downstream.

**Bestehen bleibt:** Framevalidierung, `KwpFrameEnvelope`, Framebytes by value,
Sequenz-/Zeit-/Sessioninformationen und Drop-newest-Policy.

**Umverdrahtung:** Der KWP-Owner publiziert nach erfolgreicher minimaler
Validierung direkt in `ValidatedFrameQueue`; der vorläufige Downstream-Task
konsumiert diese Queue.

**Entfernt nach GREEN:** produktive `PersistenceQueue`/`diagnostic_queue`,
`RawFrameRecord` als zweite Runtime-Hülle und jeder zweite Frameconsumer.
Dateien werden nur gelöscht, wenn keine weiterhin sinnvolle Testreferenz
abhängt.

**Tests zuerst:**

1. exakt 32 nutzbare Slots, FIFO, Wrap und Drop-newest;
2. `rx_sequence` wird auch bei Drop verbraucht;
3. Builderwiederverwendung verändert publizierte Frames nicht;
4. 60-s-FrameQueue-Sättigung mit angehaltenem Consumer: KWP-Zustand und
   ACK-Fortschritt bleiben unverändert, nur `frame_drop_full` steigt;
5. statischer Test findet genau eine produktive Instanz und genau einen
   Consumer der `ValidatedFrameQueue`.

**Hardwaregate:** Normaler 60-s-ECU-Lauf ohne Drops; danach kontrollierter
Sättigungslauf mit erwarteten Frame-Drops und weiterhin stabiler Session.

**Akzeptanz:** Kein Downstream-Zustand ist Voraussetzung für den KWP-Fortschritt.

### V2-011 – Entkoppelten Decoder und `MeasurementModel` anschließen **[TARGET, HW-ECU]**

**Ziel:** Parser/Decoder laufen ausschließlich in einer Processing-Task und
aktualisieren ein dauerhaftes Modell.

**Bestehen bleibt:** `kwp_application_parser.h`, belegte Digifant-Formeln,
Header-/Bodywissen, unbekannte Werte als Raw/Unknown und alle Golden-Captures.

**Umverdrahtung:**

- Processing konsumiert ausschließlich `ValidatedFrameQueue`;
- der bestehende `DiagnosticDecoder` wird minimal zum Processing-Decoder
  adaptiert, nicht neu erfunden;
- Headercache und `MeasurementModel` gehören allein dieser Task;
- Sequenzlücke, Sessionwechsel oder Disconnect invalidieren abhängige Werte;
- KWP erhält keinerlei Parser-/Decoderantwort.

**Neu:** `MeasurementModel` mit Wert, Rohwert, Einheit, Validität, Quelle,
Timestamp, Sequenz, Sessionepoch und Transportgeneration.

**Tests zuerst:**

1. vollständige Known-Good-Capture → deterministisches Modell;
2. Gruppen 000–004 und belegte Goldenwerte;
3. Header/Body, falsche Gruppe/Epoche, Sequenzlücke und Body ohne Header;
4. Unsupported/Unknown bleibt sichtbar Raw;
5. blockierter Decoder füllt nur die FrameQueue und verändert KWP nicht;
6. Link-/Include-Test: Decoder kennt weder EspUsbHost/KWP-Actions noch Serial,
   M5Display oder UI.

**Hardwaregate:** 60-s-ECU-Lauf; Modellupdates für alle Gruppen, keine
zusätzlichen KWP-Faults.

**Akzeptanz:** Interpretierte Werte leben unabhängig vom Frame- und
Kommunikationspfad im Single-Owner-Modell.

**Nachweis des fachlichen Vollständigkeitsschritts:** Dieser TODO ist für den
aktuell unterstützten ECU-Umfang abgeschlossen, sobald das Modell zehn
Group-000-Felder und vier Zonen je Gruppe 001–004 (insgesamt 26 feste
Einträge) by value bewahrt. Jedes Feld enthält Gruppe, Zone, Rawwert,
Formel-ID, NWB, optionalen Dekodierwert, Einheit/Semantik, Status und
Timestamp-/Sequenz-/Session-/Generationsprovenienz. Unbekannte Formeln bleiben
Raw-only. Die reale Abnahme ist in `verification.md` als
„Erweiterte ECU-Datenrepräsentation – 26 bounded Snapshot-Felder“ mit
`full_ecu_snapshot_test`, Host-/Sanitizer-Tests, Targetcompile und ECU-Lauf
dokumentiert. V2-011 Status: **PASS**.

### V2-012 – Immutable Snapshots und unabhängige Mailboxen **[TARGET, HW-ECU]**

**Ziel:** Processing erzeugt vollständige immutable Snapshots und verteilt sie
nonblocking an voneinander unabhängige Consumer-Mailboxen.

**Bestehen bleibt:** vorhandene `MeasurementSnapshot`-Felder und
Overwrite-latest-Idee.

**Umverdrahtung:** Der bisherige Snapshot-Smoke wird durch echte Modell-
Snapshots ersetzt. Serial und Display erhalten jeweils eine eigene Länge-eins-
Mailbox by value. Ein kompakter Protocolstatus wird einwegig in das Modell
übernommen.

**Tests zuerst:**

1. Snapshot ist pointerfrei und vollständig kopiert;
2. langsamer Serialleser beeinflusst Displaymailbox nicht und umgekehrt;
3. Overwrite liefert immer einen vollständigen alten oder neuen Snapshot;
4. Sessionwechsel/Disconnect/Stale/Invalid und Dropzähler;
5. Thread-/TSan-Modell oder Target-Stresstest verhindert Torn Reads;
6. Dummy-Drittconsumer lässt sich nur durch eine weitere Mailbox am Startup
   anschließen; Core-/Decoderdateien bleiben byteidentisch.

**Hardwaregate:** 60-s-ECU-Lauf mit echten Snapshotfolgen; gezielte 500-ms-
Pause eines Mailboxconsumers ohne KWP- oder Decoderänderung.

**Akzeptanz:** Das Modell wird einmal interpretiert; jeder Consumer erhält nur
eine eigene immutable Kopie.

**Ergebnis:** `MeasurementSnapshot` trägt nun dieselbe vollständige feste
26-Feld-Repräsentation wie das MeasurementModel, zusätzlich zu benannten
Convenience-Views. Kein Snapshotconsumer muss alle Felder darstellen, darf aber
keines außerhalb des Snapshots beziehen. Serial, Display und spätere
Bluetooth-/Webconsumer sind ausschließlich Snapshotconsumer. V2-012 Status:
**PASS**.

### V2-013 – Serial zu einem reinen Snapshotconsumer machen **[TARGET, HW-ECU]**

**Ziel:** Serial formatiert ausschließlich interpretierte Snapshots in einer
niedrig priorisierten Consumer-Task.

**Bestehen bleibt:** menschenlesbare Ausgabe der Gruppen/Messwerte und
Fehler-/Dropzähler.

**Umverdrahtung:** Formatierung und `Serial.print*` wechseln vollständig in den
Serial-Consumer. Raw-Frame-Parsing oder Decoderaufrufe im Serial-Task sind
verboten.

**Entfernt nach GREEN:** direkter Serialzugriff aus USB-Callbacks, Transport,
KWP-Runner, MeasurementSession, Processing und Decoder. Maschinenlesbare
Target-Testausgabe ist nur in einem benannten Testbuild nach Ende des
zeitkritischen Szenarios zulässig.

**Tests zuerst:**

1. Serial-Consumer-Golden aus einem festen Snapshot;
2. blockierter/fehlerhafter Serial-Sink und langsame Formatierung;
3. Snapshotoverwrites sind gezählt, erzeugen aber keine Frame-/RX-Drops;
4. Architekturcheck verbietet `Serial` in Transport/KWP/Decoder/Processing;
5. Serial-Consumer kompiliert ohne KWP-/Decoderheader.

**Hardwaregate:** Kontinuierliche echte Werte aus Gruppen 000–004; Serial wird
zeitweise künstlich blockiert, während KWP und Modell stabil weiterlaufen.

**Akzeptanz:** Serial ist ein austauschbarer Blattconsumer und kann vollständig
deaktiviert werden, ohne KWP, Decoder oder Display zu ändern.

**Ergänzung:** Die strukturierte Ausgabe darf alle 26 ECU-Felder als
`KWP_FIELD`-Records ausgeben; sie liest diese ausschließlich aus dem Snapshot.
Die nachgewiesene Ausgabe umfasst Gruppe, Zone, Rawwert, Formel, NWB, Wert,
Status und Provenienz. V2-013 bleibt dadurch ein reiner Blattconsumer.

### V2-014 – Display zu einem reinen Snapshotconsumer machen **[TARGET, HW-ECU]**

**Ziel:** Display und Touch verwenden ausschließlich die Displaymailbox und
lokalen UI-Zustand.

**Bestehen bleibt:** funktionierende M5Unified-Initialisierung und bewährte
Darstellungsideen des Prototyps. Fachformeln werden nicht aus dem alten
Dashboard übernommen, wenn sie bereits im Decoder liegen.

**Umverdrahtung:** Status, RPM, Temperaturen, Batterie, G69 Raw und Fehlerzustand
stammen nur aus `MeasurementSnapshot`.

**Entfernt nach GREEN:** `show_message()`- oder andere Displayaufrufe aus
Transport/KWP/Processing sowie direkte Decoder-/Modelzugriffe aus Views.

**Tests zuerst:**

1. Viewmodel-Golden für Valid/Stale/Disconnected/Unknown;
2. Architekturcheck: UI kennt Snapshot, aber nicht USB/KWP/Decoder;
3. wiederholte Renderstalls von mindestens 500 ms;
4. Serial erhält während Displaystall weiterhin unabhängige Snapshots.

**Hardwaregate:** 60-s-ECU-Lauf mit sichtbaren echten Werten und wiederholtem
Displaystall; KWP-, Decoder- und Serialzähler bleiben stabil.

**Akzeptanz:** Display kann abgeschaltet oder ersetzt werden, ohne Upstreamcode
zu ändern.

### V2-015 – Entkopplungs- und Erweiterungsabnahme **[TARGET, HW-ECU]**

**Ziel:** Die vereinfachte Pipeline als Ganzes freigeben und obsolete
Parallelpfade entfernen.

**Arbeit:**

- ungenutzte `wire_rx`-, Persistence-/Diagnostic- und Direktoutput-Pfade
  entfernen;
- nur nachweislich ungenutzte Hilfstypen/Tests bereinigen;
- keine neue Framework- oder Verzeichnisstruktur anlegen;
- einen Dummy-Bluetooth- und Dummy-Webconsumer jeweils über eine eigene
  Snapshotmailbox anschließen, ohne Netzwerkfunktion zu implementieren.

**Tests:**

1. alle Host-/Sanitizer-/Golden-/Transporttests;
2. statischer Pipelinecheck: ein RX-Pfad, ein KWP-Consumer, eine FrameQueue,
   ein Decoderowner, eigene Consumer-Mailboxen;
3. Decoder 60 s angehalten: nur Frame-Drops;
4. Serial dauerhaft blockiert und Display wiederholt 500 ms blockiert;
5. Dummy-Bluetooth/Web langsam: keine Wirkung auf andere Consumer;
6. realer 60-s-Normalbetrieb danach ohne RX-/Frame-/Snapshotfehler;
7. Targetcompile und `git diff --check`.

**Akzeptanz:**

```text
ECU/K409
→ RxIngressRing
→ KWP-Runner
→ ValidatedFrameQueue
→ Processing/Decoder
→ MeasurementModel/Snapshot
→ unabhängige Consumer
```

ist der einzige produktive Datenpfad. Transport-, KWP- und Decoderdateien
bleiben beim Hinzufügen eines weiteren Snapshotconsumers unverändert.

### V2-016 – Fixed-size IMU-Port und deterministischen Sampler ergänzen **[TARGET]**

**Ziel:** Die Tab5-IMU als rein beobachtende, bounded Datenquelle bereitstellen,
ohne Loggerdatei, ECU-Modell oder KWP zu verändern.

**Bestehen bleibt:** Der aktuelle DLOG-V1-Logger, seine Snapshotqueue,
START/STOP/MARKER, SD-Writer-Task und der Hostkonverter. Keine funktionierende
Loggerdatei wird ersetzt oder umgedeutet.

**Arbeit:**

1. Known-Good-M5Unified-Initialisierung und die tatsächlich verbaute Tab5-IMU
   anhand der lokal gepinnten Library/Targetausgabe bestimmen;
2. `cfg.internal_imu=true` nur im Targetbootstrap aktivieren;
3. pointerfreien `ImuSample` gemäß Architektur anlegen;
4. `IImuSource` nur als kleiner Testport und einen Targetadapter als alleinigen
   Besitzer von `M5.Imu` implementieren;
5. absolut getaktet mit 25 Hz samplen; keine Drift durch wiederholtes
   `delay(40)` re-anchoring;
6. Sensor-native Achsen, Sequenz, `esp_timer_get_time()` und Read-/Validitystatus
   erfassen; keine Fahrzustände oder Geschwindigkeit ableiten.

**Tests zuerst:**

1. `imu_sample_test`: Size-/Trivial-Copy-Assertions, Einheiten, Sequenzwrap und
   Invalidstatus;
2. `imu_sampler_test` mit FakeClock/FakeImu: absolute 25-Hz-Termine,
   verspäteter Wake, Readfehler, kein Catch-up-Endlosloop;
3. Architekturcheck: IMU-Code inkludiert weder KWP/Decoder/MeasurementModel,
   SD/File, Serial noch Display;
4. vollständige Host-/ASan-/UBSan-Regression.

**Targetgate:** `imu_sampler_target` mindestens 120 s auf echtem Tab5:
Stillstand, definierte 90°-Lageänderungen und kurze Handbewegung. Tatsächliche
Rate, Min/Max-Intervall, Readfehler und Achsrohwerte dokumentieren. Noch keine
Fahrzeugachsen behaupten.

**Akzeptanz:** 25 Hz innerhalb der gemessenen Targettoleranz; kein Heap-/String-
oder I/O-Zugriff pro Sample; das Abschalten der IMU ändert keinen bestehenden
Logger-/Snapshot-/KWP-Codepfad.

### V2-017 – Bounded `ImuSampleRing` und zeitlichen Logger-Merge implementieren

**Voraussetzungen:** V2-016 grün. Noch kein Dateiformatumbau.

**Ziel:** IMU-Samples nonblocking zur Logger-Task bringen und dort zusammen mit
Snapshot- und UI-Ereignissen begrenzt nach monotonem Timestamp ordnen.

**Arbeit:**

1. SPSC-Ring mit 257 Slots/256 Nutzplätzen, Drop-newest, Dropzähler und
   High-Watermark implementieren;
2. genau einen IMU-Producer und die Logger-Task als einzigen Consumer statisch
   erzwingen;
3. Logger-Merger mit höchstens je einem pending Snapshot, IMU-Sample und
   Command implementieren;
4. vor START liegende Samples verwerfen; bei gleichem Timestamp feste Ordnung
   `START → MARKER → ECU_SNAPSHOT → IMU_SAMPLE → STOP` verwenden;
5. STOP erst nach allen bereits angenommenen Records mit Timestamp `<= STOP`
   freigeben; niemals ungebunden auf einen Producer warten;
6. getrennte Snapshot-/IMU-/Command-Dropzähler in `LoggerStatus` aufnehmen.

**Tests zuerst:**

1. `imu_sample_ring_test`: FIFO, exakt 256, Wrap, Full/Drop, Millionenrecords
   unter TSan;
2. `logger_time_merge_test`: Reorder aller drei Queues, gleiche Timestamps,
   START/STOP-Grenzen, Marker während Burst, Producerstillstand;
3. 60-s-FakeClock-Stall des Loggerconsumers: nur lokale, gezählte Queue-Drops;
4. bestehende DLOG-V1-, Snapshot-, Consumer- und Pipelinechecks bleiben grün.

**Akzeptanz:** Kein Producer wartet; Merge-Speicher ist konstant; eine volle
IMU-Queue verändert weder Snapshotqueue noch Processing oder KWP.

### V2-018 – DLOG V2 mit IMU-Records und rückwärtskompatiblen Konverter umsetzen **[TARGET]**

**Voraussetzungen:** V2-017 grün.

**Ziel:** ECU-Snapshots, IMU-Samples und manuelle Ereignisse verlustfrei im
selben monotonen Zeitstrahl speichern, ohne bestehende V1-Dateien zu brechen.

**Arbeit:**

1. DLOG V2 gemäß Architektur mit explizitem Header, Recordtyp, Recordlänge,
   Schemaversion und bounded Payload definieren;
2. Recordtypen `ECU_SNAPSHOT`, `IMU_SAMPLE`, `START`, `STOP`, `MARKER` und
   `IMU_ORIENTATION` implementieren;
3. V1-Encoder unverändert lassen; V2 nur für neue IMU-fähige Logs verwenden;
4. `decode_sprotz_log.py` so erweitern, dass V1 und V2 automatisch erkannt
   werden; ECU-CSV und IMU-/Event-CSV oder eine eindeutig typisierte Long-CSV
   ausgeben;
5. Native Sensorachsen und Einheiten in Header/Orientationrecord dokumentieren;
   ohne verifizierte Einbaulage keine Bezeichnung Längs-/Querachse;
6. Write-/Flush-/Fullfehler weiter ausschließlich lokal behandeln.

**Tests zuerst:**

1. unveränderte reale V1-Datei
   `g0_s0_131013204_0.dlog`/Hash aus `verification.md` vollständig lesen;
2. V2-Golden mit allen Recordtypen, Grenzwerten und absichtlich
   nichtmonotonem Timestamp;
3. truncated Header/Record, unbekannter Typ, falsche Länge/Version und
   Bytekorruption deterministisch ablehnen;
4. Encode→Decode→CSV-Golden für 26 ECU-Felder plus IMU und Events;
5. Open-/Write-/Flush-/Storage-full sowie abrupt abgeschnittener letzter Record.

**Targetgate:** FAT32-microSD, mindestens 10 min IMU-Aufzeichnung ohne ECU,
mehrere MARKER und Tabwechsel; Datei physisch am Host rücklesen. Erwartet:
Rate/Records plausibel, monotone Zeit, `imu_queue_drops=0`, keine beschädigten
Records. Targetbuild und Upload sind verpflichtend.

**Akzeptanz:** Bestehende V1-Dateien bleiben byteidentisch lesbar; V2 enthält
alle angenommenen Records und kann ohne Targetsoftware ausgewertet werden.

### V2-019 – Gemeinsame ECU-/IMU-Logger-Abnahme **[TARGET, HW-ECU]**

**Voraussetzungen:** V2-016 bis V2-018 grün; V2-015 bleibt grün.

**Ziel:** Beweisen, dass SD-Schreiben, 25-Hz-IMU, MARKER und UI-Bedienung den
realen KWP-Betrieb nicht beeinflussen.

**Ablauf:**

1. Tab5 fest und dokumentiert im Fahrzeug positionieren; Sensorachsen und
   Einbaulage fotografisch/textlich festhalten;
2. 60 s Zündung an/Motor aus zur Stillstandsbasis;
3. Motor starten und mindestens 10 min kontinuierlich Gruppen 000–004 plus IMU
   loggen;
4. mehrere manuelle MARKER bei bekannten Bedienereignissen setzen;
5. wiederholt Tabs wechseln, Mitschreibansicht rendern und Serial aktiv lassen;
6. STOP, SD sauber entfernen, Datei am Host mit V2-Konverter prüfen;
7. vor/während/nach Logging KWP-, RX-, Frame-, Parser-, Byte-, Action-,
   Snapshot- und Loggerdropzähler vergleichen.

**Sicherheitsregel:** Keine absichtlich riskanten Fahrmanöver. Erste Abnahme
im Stand; ein späterer Fahrtest nur durch den Fahrer im sicheren öffentlichen
Verkehr bzw. auf geeignetem Gelände, ohne Bedienung des Tab5 während der Fahrt.

**Akzeptanz:**

- KWP bleibt durchgehend `RUNNING`; Gruppen 000–004 vollständig;
- `rx_drops=0`, `frame_drops=0`, `parser_rejects=0`, `byte_fault=0`,
  `action_failures=0`;
- 26 ECU-Felder pro Snapshot bleiben vollständig;
- IMU-Rate und monotone Timestamps entsprechen V2-016; keine ungeklärten
  IMU-Drops;
- START/STOP/MARKER sind zeitlich eindeutig ECU- und IMU-Records zuordenbar;
- SD-/UI-/Seriallast erzeugt keine Sessionunterbrechung;
- keine automatische Sprotz-Erkennung und keine unbelegte Fahrzustandssemantik
  wird eingeführt.

Nach PASS ist die Aufzeichnung für manuell markierte Fehlersuche freigegeben.
Automatische Ereigniserkennung ist ein separater, späterer Architektur- und
Validierungsschritt.

## 6. Stop-Regeln

Die Migration stoppt nur bei:

- Verlust der realen KWP-Funktion, der sich nicht auf die aktuelle Änderung
  zurückführen und beheben lässt;
- Verletzung von Token-/Completion-/Quiescence-/Generation-/Timingverträgen;
- RX-Overflow oder ACK-/Sessionfehler durch einen Downstream-Consumer;
- fehlendem verpflichtendem Target-/ECU-Nachweis;
- einer tatsächlich fehlenden Architekturentscheidung.

Ein fehlender Logic Analyzer bleibt die dokumentierte optionale
Verifikationslücke und ist kein Migrationsblocker.

## 7. Verifikation pro TODO

Jeder Eintrag in `verification.md` enthält:

- TODO-ID und Status;
- Known-Good-Referenzen und übernommene Fakten;
- geänderte/entfernte/umverdrahtete Dateien;
- RED-Test und erwartete Ursache;
- tatsächlich ausgeführte Host-, Sanitizer- und Targettests;
- reale ECU-Zähler und Gruppenabdeckung;
- Drop-/High-Watermark-/Faultwerte;
- offene Hardwareevidenz;
- nächstes TODO.

Ein TODO ist erst abgeschlossen, wenn sein gezielter Test, die vollständige
betroffene Regression und das jeweilige Hardwaregate tatsächlich grün sind.
