# R9 Vertragsmatrix

Diese Matrix wurde vor den R9-Änderungen erstellt. Sie trennt produktive
Komponenten von historischen Test-Schatten und legt die geplante Entscheidung
für jeden untersuchten Typ fest.

| Typ/Test | Behaupteter Vertrag | Tatsächlich getesteter Produktcode | Produktive Nutzung | Entscheidung |
|---|---|---|---|---|
| `GenerationTracker` / `k409_device_filter_test.cpp` | Generation bei gültiger K409-Verbindung fortschreiben | Nur `GenerationTracker`; die Runtime erhöht `transport_generation` direkt und nutzt `k409::matches()` | Nein | REMOVE; `k409::matches()` bleibt KEEP |
| `OperationLifecycle` / `transport_operation_lifecycle_test.cpp` | Generationsgebundene Operationen annehmen, abschließen und auslaufen lassen | Nur eigenständiges Modell; produktiv gelten `KwpProtocolCore`-/`KwpMeasurementSession`-Tokens | Nein | REPLACE durch Test gegen reale Session/Core-Komponenten |
| `KwpReceiveService` / Anteil in `kwp_byte_engine_test.cpp` | Bytes konsumieren und vollständige Frames melden | Nur Wrapper im historischen `KwpRunnerModel`; produktiv verarbeitet `KwpMeasurementSession` den RX-Pfad | Nein | REMOVE; reale Session-/Byte-Engine-Verträge bleiben getestet |
| `KwpRunnerModel` / `kwp_runner_model_test.cpp` | Ereignisse in KWP-Aktionen und Zustände übersetzen | Nur Schatten-Orchestrator; Runtime verwendet `KwpMeasurementSession` und EspUsbHost-Anbindung | Nein | REPLACE durch reale Session-Lifecycle-Tests |
| `UiState` / `ui_snapshot_test.cpp` | Snapshot empfangen und UI-Verbindungsstatus ableiten | Nur `UiState`; Runtime verwendet Snapshot-Mailbox/Fanout und `DisplayUiModel` | Nein | REMOVE; Test auf reale Mailbox-/Display-Grenzen verschieben |
| `k409::matches()` | K409-VID/PID filtern | Direkter Aufruf in USB-Callbacks der `.ino` | Ja | KEEP |
| `KwpByteEngine`-Protokolltests | Byte-/Frame-State-Machine korrekt betreiben | Wird von produktivem KWP-Sessionpfad verwendet | Ja | KEEP; Test als Produktvertrag kennzeichnen |

Die Entscheidung REMOVE betrifft ausschließlich nicht produktiv verwendete
Schattenabstraktionen. Für jeden entfernten Test wird eine reale
Produktkomponente im bestehenden oder einem klar benannten Ersatztest geprüft;
kein behaupteter Vertrag wird ersatzlos gestrichen.

## Matrix nach R9

| Typ/Test | Tatsächliche Prüfung nach R9 | Ergebnis |
|---|---|---|
| `GenerationTracker` | `k409_device_filter_test.cpp` prüft `k409::matches()` direkt, wie sie die `.ino` verwendet | Schattenklasse entfernt; Produktfilter getestet |
| `OperationLifecycle` | `kwp_measurement_session_lifecycle_test.cpp` prüft reale Session-Tokens, stale Completion und Disconnect | Schattenklasse/Test ersetzt |
| `KwpReceiveService` | `kwp_byte_engine_test.cpp` prüft die produktive Byte-Engine; `kwp_measurement_session_test.cpp` prüft den realen RX-Ingress | Wrapper entfernt; Vertrag erhalten |
| `KwpRunnerModel` | `kwp_protocol_core_token_test.cpp` und `kwp_measurement_session_lifecycle_test.cpp` prüfen reale Core-/Session-Orchestrierung | Schattenmodell/Test ersetzt |
| `UiState` | `snapshot_runtime_boundary_test.cpp` prüft `SnapshotConsumerFanout`; `display_ui_model_test.cpp` prüft das reale UI-Modell | Wrapper/Test ersetzt |

Die Zahl der C++-Hosttestquellen bleibt bei 30: zwei Schatten-Tests wurden
durch zwei produktionsnahe KWP-Tests ersetzt und der UI-Test durch einen
Fanout-Grenztest. Produktive Runtime, ABI und Datenformate bleiben unverändert;
der Arduino-Compile ergab erneut 917530 Byte Flash und 157012 Byte globale
Daten.
