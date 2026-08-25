# Refactoring-Review: M5Tab5_Digifant_Analyzer

**Stand:** 2026-08-25

**Scope:** Lesbarkeit, Schlankheit, Wartbarkeit, Verantwortlichkeiten, Datenfluss, Taskgrenzen, Test-/Produktivtrennung und Dokumentationskonsistenz.

**Nicht Bestandteil:** Änderung von Produktivcode, Tests, Timingwerten, KWP-Verhalten oder Architekturverträgen.

## Kurzfazit

Die fachliche Zielarchitektur ist weiterhin tragfähig. Die produktive ECU-Pipeline ist erkennbar entkoppelt, bounded und durch ungewöhnlich gute Host-, Target- und reale ECU-Nachweise abgesichert. Ein Rewrite oder eine neue Architektur wäre nicht gerechtfertigt.

Die Implementierungsstruktur war zum Reviewzeitpunkt nach den Erweiterungen jedoch nicht mehr so klar wie die Architektur: Die 617-zeilige `.ino` ist gleichzeitig Composition Root, Taskregistrierung, Processing-Service, Serial-CLI, Targetdiagnose, USB-Portadapter, KWP-Runtime und Display-Recovery. Dazu kamen historische Abnahmehilfen im Produktivpfad, mehrere nur von Tests verwendete Schattenabstraktionen, layoutabhängige Architekturchecks, zwei vollständige identische Kopien des EspUsbHost-Forks und deutlich überholte Dokumentation.

Zwei Ownership-Probleme sind wichtiger als kosmetische Dateigrößen:

1. `LoggerCommandQueue` ist als SPSC implementiert, wird aber sowohl von der Display-Task als auch von der Serial-Task produziert. Das ist ein nicht durch den Queuevertrag gedeckter MPSC-Zugriff.
2. `ImuDiagnosticsSink::accepted` und `last` werden in der IMU-Task geschrieben und in der Serial-Task ohne atomare Übergabe oder Mailbox gelesen. Das ist eine unklare, potentiell racy Nebenroute neben dem eigentlichen IMU-Ring.

Deshalb sollte vor weiteren größeren Features eine kurze, gezielte Konsolidierungsphase stattfinden. Sie soll weder KWP-/Transportlogik neu schreiben noch Queuealgorithmen allgemein vereinheitlichen.

## Umsetzungsstatus R0–R4/R2, R6 und R8

Die empfohlene Reihenfolge wurde bis einschließlich R4 sowie R2 abgearbeitet;
R6-Processing, R6-Serial und der rein mechanische R8-Headerschnitt wurden
inzwischen abgeschlossen. R5 und R9 wurden nicht begonnen; R7 bleibt wegen
des separat dokumentierten, derzeit nicht reproduzierten
`xTaskPriorityDisinherit`-Asserts blockiert.

| Schritt | Status | Ergebnis |
|---|---|---|
| R0 | PASS | Architektur-, Plan-, README-, Semantik- und Verification-Dokumentation auf den nachgewiesenen Iststand aktualisiert; historische Nachweise bleiben als solche erkennbar. |
| R1 | PASS | `LoggerCommandQueue` ist jetzt eine bounded, nicht blockierende MPSC-Queue; der bewährte Snapshotpfad bleibt SPSC. Host- und TSan-Tests decken konkurrierende Produzenten ab. |
| R2 | PASS | IMU-Diagnose läuft über eine kleine by-value-Latest-Mailbox. Der Logger bleibt alleiniger Consumer des `ImuSampleRing`; ungeschützte taskübergreifende Zugriffe auf `accepted`/`last` entfallen. |
| R3 | PASS | Pipeline- und Logger-Guards prüfen Vertragsverletzungen quellbaumweit statt an festen Dateilayouts; bestehende Negativprüfungen und Hosttests bleiben aktiv. |
| R4 | PASS | Der byteidentische `third_party/.../src`-Doppelbaum wurde entfernt. `src/esp_usb_host_fork/` bleibt die einzige gebaute Source of Truth; Lizenz, Pin und Vorpatch-Hashes bleiben in `third_party/.../PIN.md`. |
| R6-Processing | PASS | `src/processing_service.h` besitzt Decoder und `MeasurementModel`, verarbeitet genau einen bounded Pollschritt und publiziert die bestehenden Logger-/Snapshotkopien. Serial bleibt bewusst in der `.ino`. |
| R6-Serial | PASS | `src/serial_consumer.h` kapselt die bestehenden Kommandos, Snapshot-/Logger-/SD-/IMU-Ausgaben und Formatierung; der Taskadapter in der `.ino` behält Task und Delay unverändert. |
| R8 | PASS | Snapshot-DTO und Mailbox/Fanout sowie Logger-Typen, Channels, DLOG-Format und Core liegen in flachen Verantwortungsheadern; `DisplayUi` behält API und Layout, seine Implementierung liegt nun in `display_ui.cpp`. |

Die R1/R2-Änderungen wurden kompiliert und auf dem realen Target
`/dev/cu.usbmodem2101` geprüft. In einem Lauf von mehr als 60 Sekunden wurden
KWP-Snapshots für Generation 1/Session 1 mit Gruppen 000–004 beobachtet;
`frame_drops`, `rx_drops`, `parser_rejects`, `byte_fault` und
`action_failures` blieben 0. Die IMU-Diagnose meldete fortlaufend gültige
Samples. Der Logger meldete den erwarteten Zustand `storage_present=0`, weil
bei diesem Gate keine SD-Karte eingelegt war; das ist unabhängig von KWP und
kein Refactoringfehler.

Damit sind die strukturellen Ownership- und Duplikatprobleme mit dem besten
Nutzen/Risiko-Verhältnis behoben. R5 sowie die späteren R7–R9-Schritte bleiben
bewusst ausstehend.

## Prüfgrundlage (Review-Baseline)

Geprüft wurden:

- `M5Tab5_Digifant_Analyzer.ino` und alle projektspezifischen Header unter `src/`;
- der zum Reviewzeitpunkt gepinnte EspUsbHost-Fork und seine zweite Kopie unter `third_party/`;
- alle 28 C++-Hosttests, der Python-DLOG-V2-Test und die statischen Pipelinechecks;
- `ARCHITECTURE_V2.md`, `IMPLEMENTATION_PLAN_V2.md`, `DIGIFANT_MEASUREMENT_SEMANTICS.md`;
- projektlokale `README.md`, `ARCHITECTURE.md` und `verification.md`.

Aktuell ausgeführt und grün:

- 28/28 C++-Hosttests mit C++20 und `-Wall -Wextra -Wpedantic -Werror`;
- `tests/dlog_v2_test.py`;
- `tools/check_pipeline_v2_015.sh` einschließlich V2-009/010/013/014;
- `tools/check_sprotz_logger.sh`.

Für die ursprüngliche Review-Baseline wurde kein neuer Target-/ECU-Lauf
ausgeführt. Die damals in `verification.md` dokumentierten realen Gates wurden
als bewahrenswerte Evidenz ausgewertet, nicht neu behauptet. Der vorstehende
Umsetzungsstatus dokumentiert die zusätzlichen R1/R2-Gates.

## 1. Aktuelle Verantwortungsverteilung

| Bereich | Tatsächlicher Owner/Ort | Bewertung |
|---|---|---|
| Composition Root | globale Objekte und `setup()` in der `.ino` | Grundsätzlich richtiger Ort, aber von Runtime-Implementierung überlagert |
| USB/K409-Port | EspUsbHost-Fork plus vier Callbacks in der `.ino` | Callbackarbeit kurz; Adapter- und Zustandslogik liegen aber im Bootstrap |
| RX-Ingress | `RxIngressRing` | Klare SPSC-/Poison-Verantwortung |
| kritische Transportereignisse | `CriticalTransportEventRing` plus `wait_completion()` in der `.ino` | Ring klar, Lifecycle/Recovery über lokale Funktionen und globale Atomics verteilt |
| 5-Baud-/Handshake-Core | `KwpProtocolCore`; Ausführung in `run_handshake()` | Core gut testbar; Targetausführung gehört strukturell nicht in den Composition Root |
| Messsession | `KwpMeasurementSession`; Schleife in `run_measurement()` | Session selbst weitgehend kohärent; Runtimeowner ist nur implizit die Arduino-Loop-Task |
| Reconnect-/Sessionorchestrierung | `run_session()` und globale Epochen-/Operation-ID-Zähler | Funktional kompakt, Ownership jedoch nicht als Komponente sichtbar |
| validierte Frames | `ValidatedFrameQueue` | Klare, bewiesene SPSC-Grenze |
| Processing/Decoder/Model | `ProcessingService` plus `processing_task_entry()` als Runtimeadapter | Decoder/Model/Snapshot-Ownership ist sichtbar; Serial bleibt separat in der Task |
| Snapshotverteilung | `LatestSnapshotMailbox` und `SnapshotConsumerFanout` | Bewiesene Entkopplung; DTO, Mailbox und feste Consumerliste sind in einer Datei gekoppelt |
| Serial | `SerialConsumer` plus `serial_snapshot_task_entry()` | Snapshotconsumer, CLI, Loggerstatus, SD-Diagnose und IMU-Diagnose sind in einer hosttestbaren Komponente gebündelt; der Task ist Runtimeadapter |
| Display | `DisplayUiModel`, `DisplayUi`, Display-Task | Gute Model/Target-Trennung; der Targetrenderer ist groß, aber thematisch überwiegend kohärent |
| Bluetooth/Web | zwei permanente Dummy-Tasks | Historischer V2-015-Abnahmepfad, keine Produktfunktion |
| IMU | `M5Tab5ImuSource`, `ImuSampler`, `ImuSampleRing`, IMU-Task | Samplingpfad gut abgegrenzt; Diagnose-Nebenkanal verletzt die klare Übergabe |
| Logger | `SprotzLoggerCore`, `LoggerTimeMerge`, `SprotzLoggerService`, `SdMmcLogSink` | Datenrichtung und SD-Ownership gut; Typen, Queue, Mailbox, V1/V2-Format und Core liegen zu dicht in `sprotz_logger.h` |
| Tests/Architekturguards | einzelne Testprogramme und textbasierte Shellchecks | Viele Verträge gut abgedeckt; mehrere Checks testen Dateilayout statt Architektur |

Der Hauptdatenfluss entspricht dem Zielbild:

```text
USB callback → RxIngressRing → Arduino loop/run_session
→ KwpProtocolCore/KwpMeasurementSession → ValidatedFrameQueue
→ processing task → DiagnosticDecoder → MeasurementModel
→ Snapshot-Mailboxen / LoggerSnapshotQueue
```

Wichtig ist die Abweichung in der Mitte: Ein produktiver Typ `KwpProtocolRunner` und eine Runner-Task mit dokumentierter Priorität 6 existieren nicht. Der reale Owner ist die Arduino-Loop-Task, deren Verhalten sich aus `.ino`, Arduino-Runtime und lokalen Funktionen zusammensetzt.

## 2. Konkrete Probleme und Auffälligkeiten

### 2.1 Hohe Relevanz

#### A. SPSC-Queue mit zwei Produzenten

`LoggerCommandQueue` ist ein Alias auf `FixedSpscQueue<LoggerCommand, 9>` (`src/sprotz_logger.h:52-101`). Produzenten sind:

- die Display-Task über `DisplayUi::sendLoggerCommand()` (`src/display_ui.h:467-476`);
- die Serial-Task für `START`, `STOP` und `MARKER` (`M5Tab5_Digifant_Analyzer.ino:146-197`).

Damit stimmen deklarierter Ownershipvertrag und Runtimeverdrahtung nicht überein. Die vorhandenen Logger-/TSan-Tests prüfen keinen gleichzeitigen Display-/Serial-Multiproducer-Zugriff. Das ist technische Schuld mit möglicher funktionaler Auswirkung, nicht nur eine Benennungsfrage.

#### B. Ungesicherter IMU-Diagnose-Nebenkanal

`ImuDiagnosticsSink` schreibt nach erfolgreichem Ring-Push `accepted` und `last` in der IMU-Task (`.ino:39-54`). Die Serial-Task liest beide Felder (`.ino:263-274`). Es gibt weder atomare Publikation noch SPSC-Mailbox noch festgelegten Snapshot-Lifecycle. Der eigentliche IMU-Datenpfad zum Logger ist sauber; gerade deshalb sollte dieser Diagnosepfad nicht daran vorbeiführen.

#### C. `.ino` ist mehr als ein Composition Root

Die Datei enthält mindestens diese voneinander unabhängigen Änderungsgründe:

- Hardware- und Displaystart;
- globale Objektverdrahtung;
- sieben Task-Einstiege;
- Serial-Kommandozeilenparser und Ausgabeformat;
- USB-Callbacks;
- KWP-Aktion-zu-EspUsbHost-Übersetzung;
- Completion-Warten und Recovery-Gates;
- Handshake-, Measurement- und Reconnect-Orchestrierung;
- V2-015-Stresscode und Dummyconsumer.

Die Länge von 617 Zeilen ist nur das Symptom. Das eigentliche Problem ist, dass Änderungen an Serial, Displaystart, Dummyconsumern oder IMU-Diagnose dieselbe zentrale Datei berühren wie die bewiesene KWP-Targetverdrahtung.

#### D. Dokumentierter Runner ist nicht die tatsächliche Runtime

`ARCHITECTURE_V2.md` beschreibt `EspUsbHostPort`, `FtdiK409Phy`, `KwpProtocolRunner`, `ProtocolStatusMailbox` und eine Runner-Task mit Priorität 6. Tatsächlich gibt es:

- direkte EspUsbHost-Nutzung in der `.ino`;
- Status über mehrere globale Atomics statt `ProtocolStatusMailbox`;
- `run_session()` in `loop()` statt einer expliziten Runner-Task;
- `KwpRunnerModel`, das ausschließlich in einem Hosttest verwendet wird und nicht die produktive Runtime modelliert.

Die Datenrichtung ist ähnlich, die Ownership- und Schedulingdarstellung aber nicht exakt. Bei Timinganalysen ist diese Abweichung relevant.

### 2.2 Mittlere Relevanz

#### E. Abnahme-Dummys laufen dauerhaft im Produkt

Bluetooth und Web sind noch keine Funktionen, belegen aber jeweils Mailbox, Taskhandle, Taskstack und permanente Task. Sie erhöhen außerdem den globalen `snapshotOverwrites`-Wert absichtlich. Das war für V2-015 sinnvoll, ist nach dessen PASS aber historischer Abnahmecode im Produktivpfad.

Der Compile-Schalter `V2_015_TARGET_STRESS` ist als gezielte Testinstrumentierung vertretbar. Die immer aktiven Dummy-Tasks sollten dagegen nicht als Produktarchitektur ausgegeben werden.

#### F. Architekturchecks verhindern sinnvolle Dateiauslagerung

Die Shellchecks suchen konkrete Textfragmente in der `.ino`, zum Beispiel:

- exakte globale Deklarationen in `check_pipeline_v2_015.sh:6-8`;
- `snapshot_fanout.serial()` und konkrete Tasknamen in V2-013/014;
- `rx_ingress.tryPop` und `ValidatedFrameQueue` direkt in der `.ino` in V2-009/010;
- den Logger-Tasknamen und eine konkrete `trySend`-Zeile in `check_sprotz_logger.sh`.

Damit würde eine rein mechanische, architekturgerechte Auslagerung rot, obwohl der Vertrag unverändert bliebe. Vor dem Refactoring müssen die Guards auf stabile Abhängigkeits- und Ownershipgrenzen umgestellt werden. Das darf die Verbote nicht lockern.

#### G. Test-only-Schattenmodelle stehen unter `src/`

Folgende Typen werden produktiv nicht verwendet:

- `GenerationTracker`;
- `OperationLifecycle`;
- `KwpReceiveService`;
- `KwpRunnerModel`;
- `UiState`/`consumeSnapshot`.

Sie werden nur von Tests beziehungsweise voneinander referenziert. Besonders `KwpRunnerModel` und `KwpReceiveService` vermitteln den Eindruck, den produktiven Runner zu testen, während die Runtime andere Pfade nutzt. `serial_snapshot_consumer_test.cpp` testet ebenfalls nur eine `LatestSnapshotMailbox`, nicht Serialformatierung oder Kommandoparsing.

Diese Tests sind nicht wertlos, aber ihre Aussage und Ablage sind missverständlich. Verträge sollten entweder an den tatsächlich verwendeten Komponenten geprüft oder die Modelle eindeutig als Testmodelle geführt werden.

#### H. Zwei vollständige, byteidentische EspUsbHost-Sourcebäume

`src/esp_usb_host_fork/` und `third_party/EspUsbHost-2.7.8/src/` enthalten jeweils 22.140 Zeilen und sind vollständig identisch (`diff -qr` ohne Abweichung; gleiche SHA-256-Werte für die Kernquellen). Gebaut wird die Kopie unter `src/`; `PIN.md` bezeichnet dagegen den `third_party`-Baum als lokalen Fork.

Das ist unnötige Masse und schafft das Risiko, künftig die falsche Kopie zu patchen. Lizenz, Pin-Metadaten und Vor-Patch-Hashes sind sinnvoll; zwei aktive Source-of-Truth-Kandidaten sind es nicht.

#### I. Mehrere große Header bündeln unabhängige Gründe zur Änderung

- `sprotz_logger.h` enthält öffentliche Loggertypen, eine generische SPSC-Queue, Statusmailbox/Fanout, Sink-Interface, DLOG-V1-Encoder, DLOG-V2-Encoder und Logger-Core.
- `measurement_snapshot.h` enthält ECU-Datenschema, Snapshot, Concurrent-Latest-Mailbox und festen Vierfach-Fanout.
- `display_ui.h` enthält die komplette Targetimplementierung inline.

Die Loggerdatei sollte entlang bereits vorhandener Verantwortlichkeiten geteilt werden. Bei Display ist dagegen zunächst nur ein Header-/Implementierungsschnitt sinnvoll; eine künstliche Klasse pro Tab wäre kein Gewinn.

#### J. Snapshot- und Domainnamen sind UI-zentriert

`MeasurementSnapshot`, `MeasurementField`, Einheiten, Semantik und Evidenz liegen im Namespace `digifant::ui`, obwohl Processing, Logger und DLOG-Format sie ebenfalls als fachlichen Datentransfervertrag verwenden. Das funktioniert, macht aber die Abhängigkeitsrichtung irreführend. Eine sofortige Namespace-Großumbenennung hätte viel Churn und wenig kurzfristigen Nutzen; sinnvoller ist zuerst die Trennung von DTO und Mailbox.

### 2.3 Niedrige Relevanz, aber echte Bereinigungskandidaten

- `run_measurement()` liefert praktisch nie `true`: Die Schleife endet erst, wenn der Zustand nicht mehr `Running` ist; anschließend prüft der Return erneut auf `Running`. `run_session()` ignoriert das Ergebnis ohnehin. Das ist ein historischer, bedeutungsloser Rückgabepfad.
- `process_record()` ist nur ein Drei-Zeilen-Wrapper. Allein wäre er nicht problematisch; in einem echten `ProcessingService` erhielte er eine klare Rolle.
- `MeasurementModel::onDisconnect()` und `onSessionLost()` werden produktiv nicht benutzt; die Runtime verwendet `onProtocolStatus()`. Drei parallele Status-APIs erhöhen Interpretationsspielraum.
- `KwpFrameEnvelope::requestId` und `dialogContext`, `SprotzLoggerCore::noteImuMerged()`, `KwpProtocolCore::nextWakeup()`/`sessionEpoch()` und einige Metrikgetter haben keine produktiven Nutzer. Vor Entfernen muss zwischen öffentlichem Testvertrag, geplanter Diagnose und echtem Altbestand unterschieden werden.
- `LoggerStatus::queueDrops` ist faktisch ein Legacy-Alias nur für `snapshotQueueDrops`, während daneben drei getrennte Dropzähler existieren. Der Name suggeriert eine Summe.
- API-Stile (`tryPush/tryPop`, `trySend/tryReceive`, `publish/receive`) und Namensräume sind nicht vollständig einheitlich. Die Unterschiede tragen teilweise Semantik; eine globale Umbenennung ist nicht lohnend.

## 3. Besonders lange oder überladene Dateien und Funktionen

Die projektspezifischen Header unter `src/` umfassen 3.915 Zeilen, die `.ino` weitere 617. Die 22.140 Zeilen des Forks je Kopie sind Fremd-/Portcode und getrennt zu bewerten.

| Datei/Funktion | Umfang | Bewertung |
|---|---:|---|
| `M5Tab5_Digifant_Analyzer.ino` | 617 Zeilen | Überladen; zentraler Wartbarkeitshotspot |
| `serial_snapshot_task_entry()` | vorher ca. 140 Zeilen, jetzt dünner Adapter | Die Blattaufgaben liegen in `SerialConsumer`; Ausgabe-/Kommandosemantik bleibt zentral und testbar |
| `run_measurement()` | ca. 54 Zeilen | Nicht zu lang, aber kritische Orchestrierung mit Transportsubmission, Completion und Telemetrie |
| `run_handshake()` | ca. 39 Zeilen | Kritisch, kohärent; eher zusammen mit Target-KWP-Runtime kapseln als intern zerlegen |
| `setup()` | ca. 37 Zeilen | Viele wiederholte Taskstarts; als Composition Root noch vertretbar, sobald Implementierungen ausgelagert sind |
| `src/display_ui.h` | 584 Zeilen | Groß, aber überwiegend ein kohärenter Targetrenderer; `.h/.cpp`-Schnitt fehlt |
| `DisplayUi::drawTile()` | ca. 51 Zeilen | Darstellungslogik mit Verfügbarkeit, Wertformat und Evidenz; testbare Text-/Viewlogik teilweise noch im Renderer |
| `src/sprotz_logger.h` | 512 Zeilen | Eindeutig mehrere Verantwortlichkeiten; sinnvoll teilbar |
| `SprotzLoggerCore` | ca. 166 Zeilen | Kohärenter Writer-Core, wird aber von Format- und Infrastrukturtypen umstellt |
| `src/kwp_measurement_session.h` | 329 Zeilen | Lang, aber kritische zusammenhängende State Machine; nicht nach Zeilenzahl zerlegen |
| `src/kwp_byte_engine.h` | 284 Zeilen | Kritischer, gut getesteter Zustandsautomat; Länge allein kein Refactoringgrund |
| `src/sprotz_logger_target.h` | 239 Zeilen | Zwei klare Targetverantwortungen; akzeptabel, optional `.cpp` für Implementierung |
| `src/measurement_model.h` | 208 Zeilen | Fachlich kohärent; Semantik-Mapping könnte datengetriebener sein, aber derzeit gut prüfbar |
| `src/measurement_snapshot.h` | 174 Zeilen | Zwei Verantwortlichkeiten: Datenschema und Concurrent-Distribution |

## 4. Überflüssige oder überlappende Abstraktionen

### Klar überflüssig oder historisch

1. Die zweite vollständige EspUsbHost-Sourcekopie.
2. Permanente Bluetooth-/Web-Dummy-Tasks nach abgeschlossenem V2-015-Gate.
3. Der bedeutungslose `bool`-Return von `run_measurement()` samt ignorierter Variable.
4. `ui_state.h` als nicht produktiv verwendeter Vorläufer des heutigen `DisplayUiModel`.
5. `KwpRunnerModel`/`KwpReceiveService` als Schatten eines Runners, den die Runtime nicht verwendet.

### Überlappend, aber nicht blind zusammenlegen

Es existieren mehrere ähnliche bounded Datenstrukturen:

- `RxIngressRing`;
- `CriticalTransportEventRing`;
- `DropNewestQueue`/`ValidatedFrameQueue`;
- `FixedSpscQueue` für Logger;
- `ImuSampleRing`;
- `LatestSnapshotMailbox` und `LoggerStatusMailbox`.

Die Implementierungen überschneiden sich mechanisch, besitzen aber verschiedene Verträge: Poison, sticky Overflow, Sequenzverbrauch bei Drop, FIFO versus Latest-Overwrite sowie unterschiedliche Producer-/Consumerzahlen. Eine allgemeine Queue-Bibliothek würde hier mehr Risiko als Nutzen erzeugen. Allenfalls die zwei dreislotigen Latest-Mailboxen können später auf einen kleinen, bereits bewiesenen `LatestValueMailbox<T>`-Baustein zurückgeführt werden. Das ist nicht Teil der ersten Refactoringwelle.

Auch DLOG V1 und V2 sind keine unnötige Doppelstruktur: V1-Lesbarkeit ist ein Vertrag und V2 bettet das bewährte ECU-Snapshotpayload ein. Eine Bereinigung darf den Encoder nicht beiläufig neu definieren.

### Doppelte Parserarbeit

`KwpMeasurementSession` ruft `parseKwpFrame()` für Messplan/Framevalidierung auf; `DiagnosticDecoder` ruft denselben Parser downstream erneut auf. Das wirkt redundant und steht sprachlich neben der Architekturregel „Parser vollständig hinter der FrameQueue“. Der Messplan benötigt jedoch Titel und Payloadstruktur im KWP-Pfad. Eine Änderung an dieser Stelle berührt Protokollfortschritt und sollte nicht als Schlankheitsrefactoring erfolgen. Besser ist, die Dokumentation zwischen protokollnotwendiger Blockklassifikation und fachlicher Downstream-Decodierung präzise zu unterscheiden. Ein späteres gemeinsames Metadatenobjekt wäre nur mit neuen KWP-/ECU-Regressionen vertretbar.

## 5. Dokumentationsabweichungen

### `README.md`

- Beschreibt noch den V2-001-Smoke mit Ausgaben, die im aktuellen Sketch nicht mehr existieren.
- Behauptet am Ende, Serial und Display des V2-001-Gates seien offen; `verification.md` markiert V2-001 längst als PASS.
- Dokumentiert Logger und DLOG nur teilweise, aber nicht den aktuellen DLOG-V2-/IMU-Stand, die Serialbefehle oder die vollständige Testausführung.
- Ist damit als Einstieg in den aktuellen Produktstand nicht zuverlässig.

### `ARCHITECTURE_V2.md`

- Abschnitt „Aktuelle Abweichungen und Migration“ beschreibt entfernte Altpfade (`wire_rx`, `PersistenceQueue`, `diagnostic_task`) als aktuellen Stand.
- Die Definition of Done enthält zahlreiche offene Checkboxen, obwohl `verification.md` V2-009 bis V2-019 als PASS dokumentiert.
- Komponenten- und Tasknamen entsprechen teils dem Zielbild, nicht der Implementierung: kein produktiver `EspUsbHostPort`, `FtdiK409Phy`, `KwpProtocolRunner` oder `ProtocolStatusMailbox`; KWP läuft aus `loop()` statt einer expliziten Priority-6-Task.
- Die Organisationsskizze enthält jetzt die real vorhandenen `processing_service.h` und `serial_consumer.h`; `display_consumer.h` bleibt bewusst nicht angelegt.
- Bluetooth/Web werden als spätere Consumer dargestellt, laufen aber bereits als Dummy-Tasks.
- „Queue Länge eins“ ist semantisch Latest-Value korrekt, die reale racy-freie Implementierung nutzt aber drei interne Slots. Das sollte erklärt werden.

### `IMPLEMENTATION_PLAN_V2.md`

- Ist noch als laufender Migrationsplan formuliert, obwohl die Verification V2-015 bis V2-019 abgeschlossen hat.
- Nur V2-011/012 besitzen im Plan explizite PASS-Ergebnisse; der Abschluss der übrigen Schritte ist nur indirekt oder ausschließlich in `verification.md` sichtbar.
- Die Ausgangsbasis nennt V2-015 und Sprotz V1, während V2-016 bis V2-019 inzwischen ebenfalls abgeschlossen sind.
- Der Plan sollte als abgeschlossener historischer Plan markiert und mit einer kompakten Statusmatrix versehen werden; neue Refactorings gehören nicht als weitere V2-Migrationstodos hinein.

### Projektlokales `ARCHITECTURE.md`

- Ist sehr kurz und beschreibt noch eine frühe Arbeitsreihenfolge bis UI.
- Es nennt weder die fertige Snapshot-/Logger-/IMU-Pipeline noch die tatsächlichen Runtimekontexte.
- Als lokale Navigationsseite ist es derzeit zu alt, obwohl seine Grundregeln weiterhin richtig sind.

### `DIGIFANT_MEASUREMENT_SEMANTICS.md`

- Die 26-Feld-/Evidenztrennung stimmt mit der Implementierung überein.
- Die Liste „Noch gezielt zu verifizieren“ enthält mindestens bereits erledigte Punkte: RPM-Korrelation, ECU-Spannungsvergleich und G69-Monotonietest sind in `verification.md` dokumentiert.
- Offene und abgeschlossene Evidenz sollte getrennt markiert werden, ohne bestehende Unsicherheiten bei Last, Lambda, Geschwindigkeit oder absolutem G69-Winkel zu überdecken.

### `verification.md`

- Ist die vollständigste und wertvollste Quelle, aber als 2.370-zeiliges chronologisches Journal schwer als aktueller Status zu lesen.
- Historische Zwischenstände wie `BLOCKED-HARDWARE`, `SOFTWARE-PARTIAL` und spätere PASS-Nachträge sind korrekt als Historie, wirken ohne Statusindex aber widersprüchlich.
- V2-016 hat keinen eigenen klaren Hauptabschnitt; der Abschluss wird erst am Ende von V2-019 zusammengefasst.
- Empfohlen ist ein unveränderter historischer Verlauf plus eine aktuelle Status-/Artefaktmatrix am Anfang, nicht das Umschreiben alter Nachweise.

## 6. Bereiche, die ausdrücklich nicht refaktoriert werden sollten

### Transport- und KWP-Zustandsautomaten

`KwpProtocolCore`, `KwpByteEngine`, `MeasurementPlan` und das innere Verhalten von `KwpMeasurementSession` sollten nicht wegen Dateilänge oder Stil zerlegt werden. Ihre Zustandsübergänge, Zeitanker, Echo-/ACK-Behandlung und Tokenbeziehungen sind funktional gekoppelt und real bewiesen. Eine spätere Kapselung der Targetverdrahtung darf diese Klassen zunächst nur unverändert verwenden.

### Bounded Queue-/Ringverträge

`RxIngressRing`, `CriticalTransportEventRing`, `ValidatedFrameQueue`, `ImuSampleRing`, Loggerqueues und die dreislotige Snapshotmailbox dürfen nicht in eine universelle Queueabstraktion gepresst werden. Poison-, sticky-, drop-newest-, Sequenz- und Latest-Overwrite-Semantik sind unterschiedlich und testrelevant.

### Decoder, Semantik und 26-Feld-Snapshot

`kwp_application_parser.h`, `digifant_decoder.h`, `DiagnosticDecoder`, das Feldmapping im `MeasurementModel` und die 26-Feld-Repräsentation sind durch Golden-Captures, reale ECU-Daten und dokumentierte Evidenzgrade abgesichert. Strukturänderungen dürfen keine Formel, Zone, Rohwertprovenienz oder Evidenzbewertung „vereinfachen“.

### Logger-Merge und DLOG-Formate

`LoggerTimeMerge`, DLOG V1/V2, Recordreihenfolge, Checksummen, feste Größen und Hostkonverter bilden einen bewiesenen Vertrag. Dateien dürfen organisatorisch geschnitten werden; Formatbytes und Mergeverhalten sollen unverändert bleiben.

### DisplayUiModel

Die Trennung aus portablem `DisplayUiModel` und Targetrenderer ist sinnvoll. Der Renderer ist lang, aber sein gemeinsamer Layout-/Spritezustand rechtfertigt eine Klasse. Keine neue View-/Presenter-/Controller-Hierarchie pro Tab einführen.

### Inhalt des EspUsbHost-Forks

Eine Kopie darf entfernt werden; der tatsächlich gebaute gepinnte Fork selbst, seine Token-/Completion-Patches und sein Verhalten dürfen dabei nicht geändert werden.

## 7. Priorisierter, risikoarmer Refactoringplan

Die Schritte sind absichtlich klein. Nach jedem Schritt bleibt die Anwendung baubar und die vorhandene Evidenz gültig. Schritte, die Runtimeverdrahtung, Tasks, Queueownership oder Consumer ändern, erhalten gemäß V2-Vertrag ein reales ECU-Gate.

### R0 – Dokumentation auf einen eindeutigen Ist-Stand bringen

| Feld | Inhalt |
|---|---|
| Priorität | **P0 – sofort, vor Codebewegungen** |
| Konkretes Problem | Zielbild, Ist-Implementierung und Verificationstatus widersprechen sich; spätere Reviews können deshalb falsche Annahmen über Runner, Tasks und offene Gates treffen. |
| Betroffene Dateien/Klassen | `README.md`, `ARCHITECTURE_V2.md`, `IMPLEMENTATION_PLAN_V2.md`, lokales `ARCHITECTURE.md`, `DIGIFANT_MEASUREMENT_SEMANTICS.md`, Kopf von `verification.md` |
| Vorgeschlagene Änderung | Aktuelle Runtimekarte und Statusmatrix ergänzen; alte Verificationeinträge als Historie behalten; erledigte Semantikexperimente markieren; Zielkomponenten klar als „Zielname“ oder „tatsächlicher Typ“ kennzeichnen. |
| Erwarteter Nutzen | Sofort bessere Wartbarkeit und geringeres Risiko, eine bewiesene Grenze aufgrund falscher Dokumentation umzubauen. |
| Risiko | Sehr niedrig; Gefahr besteht nur im versehentlichen Umschreiben historischer Evidenz. |
| Absichernde Tests | Link-/Pfadprüfung, Markdownprüfung, `git diff --check`; keine Produktivtests erforderlich. |
| Reales Target-/ECU-Gate | **Nein.** |

### R1 – Logger-Command-Ingress auf den tatsächlichen Multiproducervertrag bringen

| Feld | Inhalt |
|---|---|
| Priorität | **P0 – Ownershipkorrektur** |
| Konkretes Problem | Eine SPSC-Queue wird gleichzeitig von Serial- und Display-Task produziert. |
| Betroffene Dateien/Klassen | `.ino`, `LoggerCommandQueue`/`FixedSpscQueue`, `DisplayUi`, `SprotzLoggerService`, `LoggerTimeMerge`, Logger- und Decouplingtests |
| Vorgeschlagene Änderung | Einen kleinen, festen, nonblocking MPSC-Command-Ingress verwenden oder zwei explizite SPSC-Quellen in der Logger-Task deterministisch zusammenführen. Keine allgemeine Messaging-Schicht bauen. Kapazität, Reject-/Dropzähler, Timestamps und Tie-Break bleiben explizit. |
| Erwarteter Nutzen | Ownership stimmt mit Runtime überein; keine undefinierten Head-Updates bei gleichzeitiger Touch-/Serialbedienung. |
| Risiko | Mittel, weil Loggerkommando-Reihenfolge und START/STOP-Grenzen betroffen sind. |
| Absichernde Tests | Bestehende `sprotz_logger_test`, `logger_time_merge_test`, `v2_015_decoupling_test`, DLOG-V2-Test; neu: TSan-Test mit zwei gleichzeitigen Produzenten, Queue-full und deterministischer START/MARKER/STOP-Reihenfolge. |
| Reales Target-/ECU-Gate | **Ja.** Mindestens gezielter Touch+Serial+SD-Lauf; wegen Task-/Queueänderung zusätzlich 60-s-ECU-Regression mit allen Null-Drop-/Faultzählern. |

### R2 – IMU-Diagnose über eine klare Taskgrenze führen

| Feld | Inhalt |
|---|---|
| Priorität | **P0/P1 – Ownershipkorrektur** |
| Konkretes Problem | Serial liest `accepted` und `last` direkt aus einem in der IMU-Task veränderten Sinkobjekt. |
| Betroffene Dateien/Klassen | `.ino`, `ImuDiagnosticsSink`, `ImuSampler`, optional Snapshot-/Statusmailbox |
| Vorgeschlagene Änderung | Sink auf die eine Aufgabe „nonblocking in `ImuSampleRing` publizieren“ reduzieren. Für Serial entweder ausschließlich den bereits publizierten `LoggerStatus::imuSamplesMerged` verwenden oder eine eigene Latest-IMU-Diagnosemailbox by value ergänzen. Keine zweite IMU-Ring-Consumerroute schaffen. |
| Erwarteter Nutzen | Eindeutiger IMU-Owner, racy-freie Diagnose, Datenfluss entspricht wieder der Dokumentation. |
| Risiko | Niedrig bis mittel; Sampling und Logger-Ring bleiben unverändert, nur Diagnoseausgabe ändert ihre Übergabe. |
| Absichernde Tests | `imu_sampler_test`, `imu_sample_ring_test`, `logger_time_merge_test`, `sprotz_logger_test`; neu: Concurrent-Publish/Serial-Receive unter TSan und Golden für `IMU_STATUS`. |
| Reales Target-/ECU-Gate | **Ja.** 120-s-IMU-Rate/Serialprüfung; wegen Runtimeverdrahtung gemeinsam mit einem 60-s-ECU-Lauf ausführbar. |

### R3 – Architekturguards refactoringfest machen

| Feld | Inhalt |
|---|---|
| Priorität | **P1 – Voraussetzung für sichere Auslagerung** |
| Konkretes Problem | Tests suchen konkrete Namen und Zeilen in der `.ino` und blockieren dadurch architekturgerechte Dateibewegungen. |
| Betroffene Dateien/Klassen | `tools/check_pipeline_v2_009.sh` bis `_015.sh`, `check_sprotz_logger.sh`, Testausführung/README |
| Vorgeschlagene Änderung | Checks auf verbotene Abhängigkeiten, Instanz-/Ownergrenzen und Sourcebaumweite umstellen; nicht auf einen bestimmten Dateinamen. Einen einzigen dokumentierten Host-Testtreiber ergänzen. Serialtest erst dann „Consumer-Test“ nennen, wenn Parser/Formatter tatsächlich geprüft werden. |
| Erwarteter Nutzen | Refactorings können klein bleiben, ohne Tests abzuschwächen; Architekturregeln werden genauer statt layoutabhängig. |
| Risiko | Niedrig, sofern jeder bisherige Negativfall als Fixture erhalten bleibt. |
| Absichernde Tests | Alle bestehenden statischen Checks müssen vor/nach der Umstellung grün sein; zusätzlich Mutationsfixtures, die je einen verbotenen zweiten RX-/Frame-/Decoder-/SD-/Serialpfad absichtlich erkennen. |
| Reales Target-/ECU-Gate | **Nein** für reine Test-/Scriptänderung. |

### R4 – Eine einzige Source of Truth für EspUsbHost behalten

| Feld | Inhalt |
|---|---|
| Priorität | **P1 – sehr gutes Nutzen/Risiko-Verhältnis** |
| Konkretes Problem | Zwei byteidentische Sourcebäume mit je 22.140 Zeilen; unklarer kanonischer Patchort. |
| Betroffene Dateien/Klassen | `src/esp_usb_host_fork/`, `third_party/EspUsbHost-2.7.8/src/`, `PIN.md`, Lizenz-/Hashdokumentation |
| Vorgeschlagene Änderung | Die gebaute Kopie unter `src/esp_usb_host_fork/` als einzige Source of Truth behalten; unter `third_party` nur Lizenz, Pin-/Upstreammetadaten und Hashmanifest führen. Alternativ den Build nachweislich auf genau den `third_party`-Baum umstellen, aber niemals beide behalten. Vor Löschung vollständige Gleichheit automatisiert prüfen. |
| Erwarteter Nutzen | Rund 22.140 redundante Zeilen weniger; kein Risiko, die falsche Kopie zu ändern. |
| Risiko | Niedrig, wenn die gebauten Bytes und Includepfade unverändert bleiben. |
| Absichernde Tests | SHA-256-/Treevergleich vor der Änderung, alle Hosttests, statische Checks, Targetcompile. |
| Reales Target-/ECU-Gate | **Nein** bei nachweislich unverändertem gebauten Fork; ein K409-Enumerationssmoke ist optional, kein voller ECU-Lauf erforderlich. |

### R5 – V2-015-Dummyconsumer aus dem Produktionsbuild entfernen

| Feld | Inhalt |
|---|---|
| Priorität | **P1** |
| Konkretes Problem | Zwei nichtfunktionale Consumer-Tasks und Mailboxen laufen dauerhaft und verfälschen Produkttelemetrie. |
| Betroffene Dateien/Klassen | `.ino`, `DummySnapshotConsumer`, `SnapshotConsumerFanout`, V2-015-Stressmodus und -Tests |
| Vorgeschlagene Änderung | Dummyconsumer auf Host-/Stressbuild begrenzen. Der Produktionsfanout enthält nur reale Consumer; die Erweiterbarkeit wird durch einen generischen Fanouttest oder einen expliziten Testbuild bewiesen. Keine Bluetooth-/Webframeworks anlegen. |
| Erwarteter Nutzen | Weniger Tasks/Stacks, klarere Telemetrie, saubere Trennung von Abnahme und Produkt. |
| Risiko | Niedrig bis mittel, weil Tasktopologie und Overwritezähler geändert werden. |
| Absichernde Tests | `v2_015_decoupling_test`, Snapshotmailboxtests, refactoringfeste Pipelineguards; Target-Stressbuild mit langsamen Drittconsumern bleibt erhalten. |
| Reales Target-/ECU-Gate | **Ja.** Produktions- und Stressbuild, 60-s-ECU-Lauf; erwartete Overwrites je Build dokumentieren. |

### R6 – Processing und Serial als echte, kleine Komponenten auslagern

| Feld | Inhalt |
|---|---|
| Priorität | **P1/P2** |
| Konkretes Problem | Decoder-/Modelownership und Serialblattlogik sind nur als globale Objekte und lange Taskfunktion in der `.ino` erkennbar. |
| Betroffene Dateien/Klassen | `.ino`, neu beziehungsweise gemäß bestehender Architekturskizze `processing_service.*`, `serial_consumer.*`; `DiagnosticDecoder`, `MeasurementModel`, Snapshot-/Loggerkanäle |
| Vorgeschlagene Änderung | Zuerst einen `ProcessingService` extrahieren, der Decoder und Model besitzt und genau einen bounded Pollschritt ausführt. Danach Serial-Kommandoerkennung und Formatierung in einen Serialconsumer verschieben; Task-Entry bleibt ein dünner Targetadapter. Keine Basisklassenhierarchie und kein neues Verzeichnis. |
| Erwarteter Nutzen | `.ino` wird echter Composition Root; Single-Owner-Regeln werden im Typ-/Objektgraph sichtbar; Serialformat und CLI werden hosttestbar. |
| Risiko | Mittel, weil Runtimeverdrahtung und Publikationsreihenfolge berührt werden können. |
| Absichernde Tests | Alle Decoder-/Model-/Snapshottests, `serial_snapshot_consumer_test` zu echten Parser-/Formatter-Goldens erweitern, V2-015-Decoupling, refactoringfeste Architekturguards, komplette Host-/Sanitizersuite. |
| Reales Target-/ECU-Gate | **Ja.** 60-s-ECU-Lauf plus blockierter Serialconsumer und Displaystall; Zähler mit V2-015 vergleichen. |

### R7 – KWP-Targetorchestrierung kapseln, ohne State Machines zu ändern

| Feld | Inhalt |
|---|---|
| Priorität | **P2 – erst nach R0 bis R6** |
| Konkretes Problem | KWP-Owner, Operation-IDs, Reconnectpolicy, Transportadapter und Completionwait sind über Globals/Funktionen der `.ino` verteilt; Dokumentation behauptet einen Runner. |
| Betroffene Dateien/Klassen | `.ino`, `run_handshake`, `run_measurement`, `run_session`, `submit_action`, `wait_completion`, EspUsbHost-Callbacks; neuer flacher Target-Runtimebaustein |
| Vorgeschlagene Änderung | Exakt die bestehende Orchestrierung in einen einzigen Target-KWP-Runtimeowner verschieben. Zunächst weiter aus `loop()` aufrufen; keine neue Task, keine Prioritätsänderung, keine Timingwertänderung und keine Vereinheitlichung von Handshake-/Measurement-State-Machines. Erst danach Dokumentation auf den realen Typnamen aktualisieren. |
| Erwarteter Nutzen | Klare Ownership von Generation, Sessionepoch, Operation-ID und Recovery; `.ino` verliert den riskantesten Verantwortungsblock, ohne Protokollalgorithmen neu zu schreiben. |
| Risiko | Mittel bis hoch, obwohl es eine mechanische Verschiebung ist; Timestamp-, Completion- und Quiescencebeziehungen sind empfindlich. |
| Absichernde Tests | Gesamte KWP-/Byteengine-/Session-/Reconnect-/Ring-/Lifecycle-Suite, Pipelinechecks, ASan/UBSan; neue Targetadaptertests für Tokenmapping und Completionfilter. |
| Reales Target-/ECU-Gate | **Ja, zwingend.** Normaler und Reconnect-Lauf mit Gruppen 000–004 und allen Null-Faultzählern; bei Änderung des Scheduling zusätzlich die vollständigen Timing-/Stallgates. |

### R8 – Große Header entlang vorhandener Verantwortlichkeiten schneiden

| Feld | Inhalt |
|---|---|
| Priorität | **P2** |
| Konkretes Problem | Logger- und Snapshotheader bündeln Datentypen, Concurrency, Format und Core; Displayimplementierung liegt vollständig im Header. |
| Betroffene Dateien/Klassen | `sprotz_logger.h`, `measurement_snapshot.h`, `display_ui.h`, optional vorhandene/neu flache `.cpp`-Dateien |
| Vorgeschlagene Änderung | Snapshot-DTO von Mailbox/Fanout trennen; Logger-Typen, Binärformat und Core in wenige flache Dateien schneiden; `DisplayUi`-Methoden in `.cpp` verschieben. Öffentliche Typen, Bytes und Verhalten unverändert lassen. Nicht pro Klasse oder Tab eine Datei erzeugen. |
| Erwarteter Nutzen | Kürzere Compile-Abhängigkeiten, klarere Verantwortlichkeiten, isoliertere Reviews für DLOG versus Queue versus UI. |
| Risiko | Niedrig bis mittel; Arduino-Build-/Linkdetails und statische Puffer müssen geprüft werden. |
| Absichernde Tests | Vollständige Hostsuite, DLOG-Fixture/Decoder, DisplayUiModeltests, Targetcompile, Größen-/Hashvergleich erzeugter DLOG-Testrecords. |
| Reales Target-/ECU-Gate | **Nein** für nachweislich mechanische Header-/CPP-Verschiebung; Display-/SD-Smoke auf Target empfohlen. Sobald Verhalten oder Runtimeverdrahtung mitgeändert wird: **Ja** gemäß V2-Gate. |

### R9 – Test-only-Schattenabstraktionen bereinigen

| Feld | Inhalt |
|---|---|
| Priorität | **P2/P3** |
| Konkretes Problem | Tests gegen ungenutzte Modelle erzeugen falsche Sicherheit und Produkt-`src` enthält historische Testhilfen. |
| Betroffene Dateien/Klassen | `GenerationTracker`, `OperationLifecycle`, `KwpReceiveService`, `KwpRunnerModel`, `UiState`, zugehörige Tests |
| Vorgeschlagene Änderung | Für jeden Typ entscheiden: an die echte Runtime anschließen und dort testen, durch einen Test der realen Komponente ersetzen oder eindeutig als Testfixture aus `src` entfernen. Keine Tests ersatzlos löschen; jeder behauptete Vertrag muss einer produktiven Implementierung zugeordnet bleiben. |
| Erwarteter Nutzen | Testnamen und Coverage entsprechen wieder dem tatsächlich ausgelieferten Code; weniger historische Übergangsschichten. |
| Risiko | Niedrig für Ablage/Benennung, mittel beim Ersatz durch produktionsnähere Integrationstests. |
| Absichernde Tests | Coverage-/Vertragsmatrix vorher/nachher; vollständige Hostsuite und Architekturguards. |
| Reales Target-/ECU-Gate | **Nein** bei reiner Testbereinigung und unverändertem Produktbinary; **Ja**, falls ein bisher ungenutzter Lifecycle in die Runtime übernommen wird. |

## Empfohlene Reihenfolge

```text
R0 Dokumentation
→ R1 Logger-Command-Ownership
→ R2 IMU-Diagnose-Ownership
→ R3 refactoringfeste Guards
→ R4 Fork deduplizieren
→ R5 Dummys isolieren
→ R6 Processing/Serial extrahieren
→ R7 KWP-Runtime kapseln
→ R8 Header schneiden
→ R9 Schattenmodelle bereinigen
```

R4 kann unabhängig parallel zu den konzeptionellen Arbeiten vorbereitet werden, sollte aber als eigener Commit mit Byte-/Hashnachweis bleiben. R7 darf nicht mit Queue-, Timing- oder KWP-Verhaltensänderungen kombiniert werden.

## Gesamteinschätzung

### Aktueller Wartbarkeitszustand

**Funktional stark, strukturell mittel.** Die Architekturidee und ihre kritischen Datenverträge sind deutlich besser als der aktuelle Dateischnitt. Das Projekt ist nicht „rewrite-reif“, aber die Composition- und Teststruktur hat mit den Erweiterungen sichtbar technische Schuld aufgebaut.

### Größte technische Schulden

1. Nicht eingehaltene Ownershipverträge an Logger-Commandqueue und IMU-Diagnose.
2. Zu viele Runtimeverantwortlichkeiten und impliziter KWP-Owner in der `.ino`.
3. Historische Abnahme-/Schattenpfade und layoutabhängige Tests im beziehungsweise gegen den Produktivbaum.
4. Zwei vollständige Forkkopien und uneindeutige Source of Truth.
5. Dokumentation, die Zielbild, alte Migration und aktuellen PASS-Stand vermischt.

### Top-3-Refactorings mit bestem Nutzen/Risiko-Verhältnis

1. **Logger-Command-Ownership korrigieren** – höchster Korrektheitsnutzen; klar begrenzte Änderung mit gezieltem Concurrencytest.
2. **EspUsbHost-Fork deduplizieren** – sehr großer Schlankheitsgewinn bei praktisch unveränderten gebauten Bytes.
3. **Guards refactoringfest machen und danach Processing/Serial aus der `.ino` extrahieren** – größter Wartbarkeitsgewinn, ohne KWP-State-Machines oder Architekturregeln anzufassen.

Die IMU-Diagnosekorrektur sollte zeitlich ebenfalls in die erste Welle und kann zweckmäßig mit dem Logger-Ownership-Gate gemeinsam auf dem Target geprüft werden.

### Vor weiteren Features zuerst refaktorieren?

**Ja, aber gezielt und kurz.** Vor neuen Features sollten mindestens R0 bis R4 sowie die IMU-Diagnosekorrektur abgeschlossen sein. Danach kann Featurearbeit wieder parallel zu den kleineren Strukturverbesserungen laufen. Ein vollständiger Umbau der `.ino`, eine neue Frameworkschicht oder eine Generalisierung aller Queues ist weder notwendig noch ratsam.
