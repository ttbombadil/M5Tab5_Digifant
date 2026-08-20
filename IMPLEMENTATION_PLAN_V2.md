# M5Tab5 Digifant Analyzer – Implementierungsplan V2

**Zielprojekt:** `M5Tab5_Digifant_Analyzer/`

**Architekturquelle:** `ARCHITECTURE_V2.md`

**Vorgehen:** Greenfield, bottom-up, testgetrieben vom ECU-/K-Line-Zugriff bis zur UI

**Status:** Ausführungsplan; noch kein Produktivcode

## 1. Verbindlicher Ausführungsvertrag

Dieser Plan konkretisiert die bereits entschiedene Architektur. Ein ausführender Agent darf Komponenten, Taskgrenzen, Ownership, Queue-Policies, Operationstoken, Deadline-Ownership oder Recovery-Semantik nicht neu entwerfen. Stellt sich eine Annahme aus `ARCHITECTURE_V2.md` als falsch heraus, wird der betroffene TODO als blockiert dokumentiert und die Architekturfrage eskaliert; sie wird nicht still im Code umgangen.

Die alte Anwendung `M5Tab5_Digifant_Proto/` wird weder refaktoriert noch gelinkt oder inkludiert. Sie darf nur gelesen werden, um funktionierendes Hardware-/Protokollverhalten, Decoderformeln und Captures zu verifizieren. Jede übernommene Tatsache oder Testsequenz erhält einen Provenienzeintrag. Alte Klassen und ihre Abhängigkeitsstruktur werden nicht kopiert.

### 1.1 Reihenfolge und Stop-Regeln

1. TODOs werden streng in Nummernfolge bearbeitet, sofern ein TODO nicht ausdrücklich parallele, bereits erfüllte Voraussetzungen nennt.
2. Ein TODO ist erst abgeschlossen, wenn alle angegebenen Tests tatsächlich ausgeführt wurden und grün sind.
3. Nach jedem TODO laufen mindestens der neue gezielte Test und alle zuvor grünen Tests der betroffenen Schicht.
4. Ein Hardwaretest ist ein Gate. `Mock`, Replay, Emulator oder bloßer Compile-Erfolg ersetzt ihn nicht.
5. Ein fehlender K409, eine fehlende ECU oder ein fehlender Logic Analyzer ergibt `BLOCKED-HARDWARE`, niemals `PASS` oder `SKIPPED`.
6. Bei einem fehlgeschlagenen kritischen Vertrag – insbesondere Completion-Korrelation, Quiescence oder Timingfenster – werden keine höheren KWP-/UI-Schichten begonnen.
7. Nach Startup darf der Critical Path weder dynamisch allozieren noch formatiert loggen. Ein Verstoß stoppt die Folgearbeiten.
8. Der Agent ändert außerhalb von `M5Tab5_Digifant_Analyzer/` keine bestehende Projektdatei. Referenzdaten werden kopiert, nicht am Original editiert.

### 1.2 Definition „TODO erledigt“

Für Produktivcode gilt jeweils:

1. **RED:** Zuerst wird ein kleiner Verhaltenstest geschrieben und ausgeführt. Er muss wegen des fehlenden/falschen Verhaltens scheitern, nicht wegen einer kaputten Testumgebung.
2. **GREEN:** Nur die für den Test nötige Implementierung wird ergänzt; der gezielte Test und die Schichtsuite laufen grün.
3. **REFACTOR:** Namen/Struktur dürfen bereinigt werden, ohne den Vertrag zu verändern; danach laufen die gleichen Tests erneut.
4. **Evidenz:** Kommando, Ergebnis, relevante Messwerte und Artefakt-Hash werden in `docs/verification/verification_log.md` dem TODO zugeordnet.
5. **Review:** `git diff --check` ist sauber; es gibt keine versehentliche Abhängigkeit auf den Altcode.

Für reine Scaffold-/Dokumentations-TODOs ersetzt ein deterministischer Struktur- oder Konsistenzcheck RED/GREEN. Für Hardware-TODOs wird zuerst die Auswertung mit einer absichtlich ungültigen Fixture/Trace als RED geprüft; GREEN verlangt anschließend die reale Messung.

### 1.3 Testklassen

- **HOST:** Standard-C++ auf dem Entwicklungsrechner, ohne Arduino, FreeRTOS, EspUsbHost oder M5Stack.
- **HOST-SAN:** dieselben Tests mit ASan/UBSan; Concurrency-Modelle zusätzlich mit TSan, soweit die Plattform dies unterstützt.
- **TARGET:** echter M5Stack Tab5; kein Fahrzeug erforderlich, sofern nicht zusätzlich gekennzeichnet.
- **HW-K409:** echter Tab5 plus AutoDia K409/FT232R.
- **HW-ECU:** echter Tab5, K409 und Digifant-ECU/Fahrzeug.
- **HW-LA:** physische K-Line-/USB-relevante Messung mit Logic Analyzer/Oszilloskop.
- **SOAK:** zeitlich definierter Dauerlauf; darf nicht durch eine kurze Simulation ersetzt werden.

### 1.4 Einheitliche Befehle nach Aufbau der Toolchain

Die TODOs 003 und 004 stellen folgende stabile Einstiegspunkte bereit:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug --output-on-failure

cmake --preset host-asan-ubsan
cmake --build --preset host-asan-ubsan
ctest --preset host-asan-ubsan --output-on-failure

cmake --preset host-tsan
cmake --build --preset host-tsan
ctest --preset host-tsan --output-on-failure

./tools/build_target.sh
./tools/run_target_test.sh <suite>
./tools/check_architecture.sh
```

Gezielte Hosttests werden mit `ctest --preset host-debug -R '^<testname>$' --output-on-failure` ausgeführt. `run_target_test.sh` liest Boardport und Testtimeout aus der nicht eingecheckten Datei `.target-test.env`, flasht den Testbuild und akzeptiert ausschließlich eine maschinenlesbare abschließende `PASS`-Zeile mit Suite, Build-ID und Zählerwerten. Timeout, Reboot, fehlende Felder oder `SKIP` sind Fehler.

## 2. Phasen und harte Gates

| Phase | Ergebnis | Harte Freigabe für die nächste Phase |
|---|---|---|
| 1 | reproduzierbares Projekt und nachweislich geeigneter USB-/FTDI-/K409-Port | korrelierte TX-/Control-Completion, Quiescence, Hot-Unplug-Lifetime und physische Low-Level-Timingbasis sind grün |
| 2 | vollständig autonomer, host-testbarer KWP1281-Core | 5-Baud-Wellenform ist physisch belegt; Emulator-, Fault- und Fuzztests sind grün |
| 3 | Runner, bounded Kanäle und entkoppelte Raw-Frame-Erfassung | 60-s-Consumer-Stall ändert den KWP-Fortschritt nicht; nur gezählte Frame-Drops treten auf |
| 4 | Parser, Digifant-Domainmodell, Snapshots und Observability | Golden-Replay liefert deterministische Messwerte; Loggerausfall beeinflusst Protocol nicht |
| 5 | UI und Gesamtfreigabe | reale ECU-, Logic-Analyzer-, Stress-, Hotplug- und Soak-Abnahme erfüllen die DoD |

## 3. TODOs

## Phase 1 – Projektbasis und Low-Level-Kommunikation

### TODO V2-001 – Greenfield-Projektwurzel anlegen

**Ziel:** Als allerersten Arbeitsschritt den unabhängigen Unterordner `M5Tab5_Digifant_Analyzer/` erzeugen und die Greenfield-Grenze sichtbar machen.

**Voraussetzungen:** Keine.

**Konkrete Arbeit:** Ordner anlegen; darin zunächst nur eine kurze `README.md` mit Projektname, Verweis auf `../ARCHITECTURE_V2.md`, Greenfield-Regel und Verbot von Includes/Links auf `../M5Tab5_Digifant_Proto/` erstellen. Keine Altdatei kopieren.

**Tests/Verifikation:** `test -d M5Tab5_Digifant_Analyzer`; `test -f M5Tab5_Digifant_Analyzer/README.md`; `rg -n 'EcuInitTester|UsbCdcLink|Dashboard|Console|SimulatedLink' M5Tab5_Digifant_Analyzer` darf keinen Architekturbaustein ausweisen.

**Akzeptanzkriterium:** Die neue, eigenständig benannte Projektwurzel existiert und enthält ausschließlich die Greenfield-Grenzdokumentation.

### TODO V2-002 – Verzeichnis- und Modulgerüst erzeugen

**Ziel:** Die in `ARCHITECTURE_V2.md` festgelegten Schichten als leeres, überprüfbares Gerüst abbilden.

**Voraussetzungen:** V2-001.

**Konkrete Arbeit:** Unter `src/` die Bereiche `core/{time,messaging,result}`, `protocol/kwp1281`, `domain/digifant`, `application`, `observability`, `platform/{esp32,host}` und `ui` anlegen; außerdem `tests/{unit,protocol,decoder,integration,fuzz,soak,support}`, `testdata/{captures,golden}`, `docs/{verification,hardware}` und `tools`. Noch keine Fachimplementierung anlegen.

**Tests/Verifikation:** Ein Strukturtest `tools/check_layout.sh` prüft exakt die Pflichtordner und verbietet Source-Symlinks oder relative Includes in das Altprojekt. Den Check ausführen.

**Akzeptanzkriterium:** `check_layout.sh` endet mit Exitcode 0; alle Architekturmodule haben einen eindeutigen Zielort und keine Altcodekopplung.

### TODO V2-003 – Reproduzierbaren Host-Build und CTest-Basis schaffen

**Ziel:** Jede plattformunabhängige Änderung lokal und im CI identisch bauen und testen können.

**Voraussetzungen:** V2-002.

**Konkrete Arbeit:** Top-Level-`CMakeLists.txt` und `CMakePresets.json` für C++20, Ninja, `host-debug`, `host-asan-ubsan` und `host-tsan` anlegen. Host und Target müssen denselben portablen C++20-Sprachumfang kompilieren; GNU-Erweiterungen dürfen im gemeinsamen Core nicht benötigt werden. Einen minimalen, netzwerkfreien Test-Runner auf Basis separater CTest-Executables bereitstellen; keine zur Configure-Zeit heruntergeladene Dependency. Warnings-as-errors für eigenen Code aktivieren.

**Tests/Verifikation:** RED: ein absichtlich falscher Smoke-Assert muss von CTest als Fehler erkannt werden. GREEN: Assert korrigieren; alle drei Presets konfigurieren/bauen, `v2_host_smoke` in Debug und ASan/UBSan ausführen. TSan muss mindestens konfigurieren; ist TSan auf dem Host technisch nicht unterstützt, wird dies mit Compilerdiagnose dokumentiert und auf einer unterstützten CI-Plattform zwingend ausgeführt.

**Akzeptanzkriterium:** Frischer Build aus leerem Buildordner ist offline reproduzierbar; `v2_host_smoke` besteht und kein Produktivheader zieht Arduino-/FreeRTOS-Header ein.

### TODO V2-004 – Target-Build und maschinenlesbaren Target-Test-Runner schaffen

**Ziel:** Das reale Tab5-Binary und spätere Hardwaretests mit einem stabilen Kommando bauen/auswerten.

**Voraussetzungen:** V2-003; lokal installierter Arduino-ESP32-Core 3.3.10.

**Konkrete Arbeit:** `M5Tab5_Digifant_Analyzer.ino` als dünnen Bootstrap, versionierte Boardkonfiguration für `esp32:esp32:m5stack_tab5` mit den bekannten Optionen, `tools/build_target.sh` und `tools/run_target_test.sh` anlegen. Buildmodus (`production` oder benannte Testsuite) wird compile-time gewählt. Der Runner speichert Build-ID, Core-/Libraryversionen und serielles Ergebnis in der Verification-Logstruktur.

**Tests/Verifikation:** RED: Runner mit einer Fixture ohne terminale Ergebniszeile muss fehlschlagen. GREEN: `./tools/build_target.sh`; anschließend **[TARGET]** `./tools/run_target_test.sh target_smoke` auf echtem Tab5.

**Akzeptanzkriterium:** Der Production-Skeleton-Build kompiliert; das reale Board bootet und `target_smoke` liefert genau ein gültiges PASS-Resultat. Ein bloßer Hostbuild genügt nicht.

### TODO V2-005 – Architektur- und Hot-Path-Guards automatisieren

**Ziel:** Verbindliche Schichtgrenzen und verbotene Hot-Path-Konstrukte früh maschinell absichern.

**Voraussetzungen:** V2-003.

**Konkrete Arbeit:** `tools/check_architecture.sh` plus kleine statische Checks erstellen: `protocol/` darf Platform/UI/FreeRTOS/Arduino nicht inkludieren; `domain/` darf UI nicht kennen; Protocol Runner/Callbacks dürfen keine `String`, Heap-API, formatierte Ausgabe, Storage- oder UI-Aufrufe enthalten. Altprojektnamen und Includes außerhalb der V2-Wurzel verbieten. Ausnahmen müssen leer starten und explizit begründet werden.

**Tests/Verifikation:** RED: je eine temporäre Testfixture mit verbotener Include-Richtung und `printf` im Hot-Path wird erkannt. GREEN: Fixtures entfernen und `./tools/check_architecture.sh` sowie `v2_dependency_rules` ausführen.

**Akzeptanzkriterium:** Alle derzeitigen Dateien bestehen den Guard; jede spätere TODO-Abnahme führt ihn erneut aus.

### TODO V2-006 – Referenz- und Verifikationsledger anlegen

**Ziel:** Altcode-Fakten, Hardwareannahmen und Testevidenz nachvollziehbar trennen.

**Voraussetzungen:** V2-001.

**Konkrete Arbeit:** `docs/reference_ledger.md`, `docs/protocol_assumptions.md` und `docs/verification/verification_log.md` anlegen. Jeder Eintrag nennt Quelle/Zeilen oder Capture-Hash, Status `verified-reference`, `assumption` oder `measured-v2`, und den verifizierenden TODO. Alte Implementierung bleibt reine Referenz.

**Tests/Verifikation:** Ein Konsistenzcheck verlangt für jede produktive Timingkonstante und jede kopierte Capturedatei eine Ledger-ID. Check zunächst gegen eine absichtlich unreferenzierte Fixture rot, danach grün ausführen.

**Akzeptanzkriterium:** Fakten, Annahmen und V2-Messwerte sind getrennt; keine Referenzbeobachtung wird als normative ECU-Grenze ausgegeben.

### TODO V2-007 – Feste Basistypen, Fehler- und Zeitverträge definieren

**Ziel:** Pointerfreie, trivially-copyable Grundtypen für Zeit, IDs, Resultate und Faults bereitstellen.

**Voraussetzungen:** V2-003, V2-005.

**Konkrete Arbeit:** `IMonotonicClock`, 64-Bit-Zeittypen, halb offene `TimeWindow [not_before, deadline)`, `FaultDomain`, `FaultCode`, `FaultRecord`, generation-/epoch-sichere IDs und kleine `Result`-Typen implementieren. Gleichheit an `deadline` ist abgelaufen; ID-Wrap ergibt kontrollierten Restart/Fault.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_time_window` und `v2_fault_types`: vor `not_before`, an beiden Rändern, nach Deadline, 64-Bit-Grenzen, Trivial-Copy-/Size-`static_assert`s und ID-Wrap.

**Akzeptanzkriterium:** Tests und Sanitizer sind grün; Basistypen enthalten keine Heapobjekte, Strings oder Platformheader.

### TODO V2-008 – FakeClock, FakeTransport und FaultScript als Testports aufbauen

**Ziel:** Ab dem ersten Protokollschritt exakt denselben Core deterministisch ohne Fahrzeug treiben können.

**Voraussetzungen:** V2-007.

**Konkrete Arbeit:** `FakeClock`, aufzeichnenden `FakeTransport/ActionExecutor`, `BoundedFakeSink` und ein minimales `FaultScript` mit Drop/Delay/Duplicate/Reorder/Disconnect erstellen. Acceptance, Terminalereignis, Cancel und Quiescence müssen unabhängig steuerbar sein; keine realen Sleeps.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_fake_transport`: Aktionen samt Token aufzeichnen, Completion bewusst vor/nach RX liefern, Zeit deterministisch springen, vollen Sink erzwingen.

**Akzeptanzkriterium:** Ein Test kann jede Transportphase und Ereignisreihenfolge reproduzierbar vorgeben; Testsupport hängt nicht von Platformcode ab.

### TODO V2-009 – EspUsbHost 2.7.8 reproduzierbar pinnen und Patchgrenze festlegen

**Ziel:** Die in der Architektur benötigte, von der öffentlichen API nicht gelieferte Completion-/Quiescence-Semantik auf eine prüfbare Bibliotheksbasis stellen.

**Voraussetzungen:** V2-004, V2-006.

**Konkrete Arbeit:** Exakte EspUsbHost-2.7.8-Quelle samt Hash und Lizenz als gepinnte Dependency/Fork unter `third_party/` oder als reproduzierbar angewandter Patchsatz einbinden. `docs/esp_usb_host_contract.md` dokumentiert nur die nötige Patchfläche: tokenisierte Bulk-OUT-/EP0-Terminalereignisse, Cancel/Drain, Lebensdauer bei Disconnect, deaktivierbarer CDC-Schattenring und callbackfreies Logging. Keine privaten Member per Präprozessor öffnen.

**Tests/Verifikation:** Dependency-Hashcheck und Targetcompile aus einer Umgebung, die nicht zufällig die global installierte Bibliothekskopie bevorzugt. Negativtest mit falschem Hash muss fehlschlagen.

**Akzeptanzkriterium:** Der Build verwendet nachweislich ausschließlich die gepinnte Version; Upstream-Diff und Lizenz sind sichtbar und minimal begrenzt.

### TODO V2-010 – Generischen SPSC-Ring mit festem Speicher implementieren

**Ziel:** Den lockfreien Single-Producer-/Single-Consumer-Grundkanal mit korrekter Cross-Core-Speicherordnung bereitstellen.

**Voraussetzungen:** V2-003, V2-007.

**Konkrete Arbeit:** Statisch dimensionierten Ring implementieren: Producer schreibt Payload und publiziert Head mit Release, Consumer liest Head mit Acquire; jede Seite schreibt nur ihren Index. Kapazität und effektiv nutzbare Slots sind compile-time sichtbar. Kein `volatile` als Synchronisationsersatz.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_spsc_ring`: leer/voll, FIFO, Wrap, exakt `N-1` Nutzslots, kein Überschreiben. Danach `v2_spsc_ring_stress` unter TSan mit Millionen geordneten Records.

**Akzeptanzkriterium:** Alle Werte kommen exakt einmal und geordnet an; TSan meldet keine Race; Ring alloziert nicht.

### TODO V2-011 – RX-Ingress-Records und Poison-/Reset-Handshake implementieren

**Ziel:** Eine Byte-Lücke darf nie durch Weiterparsen späterer Bytes verborgen werden.

**Voraussetzungen:** V2-010.

**Konkrete Arbeit:** `RxIngressItem` mit Byte, Batchtimestamp, Transportgeneration, Ingress-Epoche und `transport_event_sequence:uint64` definieren. 512-Slot-Ring, `Open/Poisoned`, sticky Overflow, Drop-all-after-gap und Reset mit neuer Epoche implementieren. Callback snapshottet Zustand/Epoche einmal pro Batch; Reopen nur bei nachgewiesener Producerquieszenz.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_rx_ingress_poison`: Full mitten im Batch, weitere Batches drop-only, Mid-callback-Reopen, Reset erst bei Quiescence, alte Epoche nach Reset verworfen, Wrap führt zu Restart.

**Akzeptanzkriterium:** Kein Byte eines poisonenden oder alten Batches kann in die neue Epoche gelangen; Overflow und Drops sind exakt gezählt.

### TODO V2-012 – Publication-Cutoff gegen False-Timeout implementieren

**Ziel:** Ein rechtzeitig begonnener Cross-Core-Callback darf nicht zwischen Empty-Check und `advance(now)` verloren gehen.

**Voraussetzungen:** V2-011, V2-007.

**Konkrete Arbeit:** `publication_epoch`, Producer-active/quiescent-Protokoll und Cutoff-Tupel `(now_us, publication_epoch, last_transport_event_sequence)` umsetzen. Clockgleichheit wird durch Epoche/Sequenz total geordnet; Event rechtzeitig nur bei `event_time < deadline`.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit deterministischen Barrieren in `v2_publication_cutoff`: Callback startet zwischen letztem Empty-Check und Cutoff; gleiches Mikrosekunden-Timestamp; Epoch-/Sequenzwrap; Callback-WCET-Überschreitung.

**Akzeptanzkriterium:** Kein False-Timeout; der Consumer pumpt bei instabilem Cutoff erneut und fällt bei ungebunden aktivem Producer typisiert aus statt zu spinnen.

### TODO V2-013 – CriticalTransportEventRing und Full-Fault implementieren

**Ziel:** Lifecycle- und Terminalereignisse verlustsicher oder explizit fatal übertragen, ohne Semantik in Notifications zu verstecken.

**Voraussetzungen:** V2-010, V2-007.

**Konkrete Arbeit:** 32-Record-SPSC-Ring für Connect, Disconnect, TX-/Control-Terminalereignisse mit Generation, Token, Zeitpunkt und gemeinsamer Quellsequenz erstellen. Notification bleibt Wake-Hinweis. Full setzt nicht überschreibbares Overflow-Sticky und schließt das Submission-Gate.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_transport_event_ring`: FIFO/Wrap, coalesced/lost wake bei vorhandenen Records, exakt voll/überlaufend, kein Erraten einer Completion, falscher Producerzugriff durch API unmöglich.

**Akzeptanzkriterium:** Ohne Ringoverflow geht kein Ereignis durch Notification-Coalescing verloren; bei Overflow wird die Generation deterministisch invalidiert.

### TODO V2-014 – FTDI-Geräteauswahl und Transportgeneration integrieren **[HW-K409]**

**Ziel:** Genau den K409 (`VID 0x0403`, `PID 0x6001`) auswählen und jede Handle-Inkarnation eindeutig identifizieren.

**Voraussetzungen:** V2-004, V2-009, V2-013.

**Konkrete Arbeit:** Minimalen `EspUsbHostPort` für Enumeration, ausgewählte Adresse/Interface, Connect/Disconnect und bootmonotone `transport_generation` implementieren. Reuse derselben USB-Adresse erzeugt eine neue Generation. Callback-Logging bleibt compile-time aus.

**Tests/Verifikation:** HOST-Negativ-/Filtertest `v2_usb_device_filter`. Danach real **[HW-K409]** `./tools/run_target_test.sh usb_enumeration_generation`: falsches USB-Gerät ignorieren, K409 verbinden, trennen, erneut verbinden.

**Akzeptanzkriterium:** Nur der FT232R-K409 öffnet das Gate; Reconnect liefert eine neue Generation; alte Lifecycle-Records ändern den neuen Zustand nicht.

### TODO V2-015 – Serial-RX-Callback an den poisoned Ingress anbinden **[TARGET, HW-K409]**

**Ziel:** EspUsbHost-RX minimal, pointerfrei und nach dem bewiesenen SPSC-Vertrag veröffentlichen.

**Voraussetzungen:** V2-011 bis V2-014.

**Konkrete Arbeit:** Einen unverändert registrierten Serial-Callback für eine Hostinstanz anbinden; vor Einfügen auf Adresse/Interface filtern, einmal pro USB-Batch timestampen, Items kopieren, Batch vollständig publizieren und genau einmal normal per Task-Notification wecken. USB-Pointer nie speichern. SPSC-Versionannahme im Adapter statisch dokumentieren.

**Tests/Verifikation:** HOST-Adaptertest mit fremdem Endpoint und Callback-Batches. **[HW-K409]** Targettest `rx_callback_contract` erzeugt reale Echo-/RX-Batches und prüft Batchtimestamp, Sequenz, Generation, Filter, High-Watermark und Callback-WCET; bewusst forcierter kleiner Testring prüft Poison.

**Akzeptanzkriterium:** Alle akzeptierten Bytes erscheinen geordnet als Wertrecords; fremde Daten fehlen; Callback blockiert/loggt/allozert nicht; realer Overflow bleibt bis Reset poisoned.

### TODO V2-016 – Operationstoken und Active/Retiring/Retired-Lifecycle implementieren

**Ziel:** Stale oder doppelte Async-Ergebnisse dürfen keinen späteren Turn beeinflussen.

**Voraussetzungen:** V2-007, V2-008, V2-013.

**Konkrete Arbeit:** `TransportOpToken` exakt mit `transport_generation`, `session_epoch`, `semantic_turn_id`, bootweitem `transport_op_id` und `operation_kind` umsetzen. Lifecycle `Active → Retiring → Retired`; nur Active darf Semantik fortschreiben, Retiring nur Ressourcen/Quiescence, Retired nur zählen.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_transport_op_token`: jedes einzelne falsche Feld, duplicate Terminal, Terminal nach Timeout, Terminal aus alter Session/Generation, ID-Wrap.

**Akzeptanzkriterium:** Nur exakt passendes Active-Event ändert Turnzustand; Retiring kann ausschließlich Retirement abschließen; Ressource wird nie durch bloßen Timeout frei.

### TODO V2-017 – Korrelierte Bulk-OUT-TX-Lane im EspUsbHost-Port bereitstellen **[HW-K409]**

**Ziel:** Die gepinnte Bibliothek um eine wirklich korrelierte, vorallokierte TX-Completion erweitern.

**Voraussetzungen:** V2-009, V2-013 bis V2-016.

**Konkrete Arbeit:** Vier vorallokierte DMA-fähige Slots unterstützen, aber über `FtdiK409Phy` genau eine semantische Lane exponieren. Submit gibt synchron `Accepted`, `RejectedNoEffect` oder `OutcomeUnknown`; Terminalcallback publiziert denselben Token. Aggregierte Pending-/Completed-Zähler werden nie als Completion interpretiert.

**Tests/Verifikation:** HOST-Portmodell `v2_serial_tx_lane` prüft zweite Submission, Sloterschöpfung und Tokenrückgabe. **[HW-K409]** `serial_tx_correlated_completion` sendet kontrollierte Bytes und weist pro Operation genau eine passende Terminalmeldung nach; `max_semantic_in_flight` bleibt 1.

**Akzeptanzkriterium:** Jede akzeptierte Operation hat genau ein korrelierbares Terminalergebnis; kein zweiter Slot wird als Protokollfenster benutzt.

### TODO V2-018 – Vorallokierte EP0-Composite-Control-Lane implementieren **[HW-K409]**

**Ziel:** Baud, Latency, DTR/RTS und Break/Mark als serialisierte, korrelierte FTDI-Operationen ausführen.

**Voraussetzungen:** V2-016, V2-017.

**Konkrete Arbeit:** Einen vorallokierten EP0-Slot plus festen Composite-State implementieren. Mehrteilige Konfiguration ist nach außen eine Operation; Suboperationen tragen Composite-Token/Subsequenz und laufen seriell. Break-on/off muss vollständige 8N1-Data-Characteristics erhalten. Kein dynamischer Standard-Controlpfad im Betrieb.

**Tests/Verifikation:** HOST `v2_ftdi_composite_control`: Teilfehler, duplicate/stale Subcompletion, falsche Reihenfolge, kein Overlap, Gesamtcompletion erst nach letztem Teil. **[HW-K409]** `ftdi_control_lane` für Baud, 1-ms-Latency, DTR/RTS und Break/Mark.

**Akzeptanzkriterium:** `max_control_in_flight == 1`; Composite-Erfolg wird erst vollständig gemeldet; Heap-/Allocation-Instrumentierung bleibt nach Startup null.

### TODO V2-019 – Quiescence-, Cancel- und bekannte-Leitung-Barriere implementieren **[HW-K409]**

**Ziel:** Ein logisch abgebrochener Transfer darf später weder Byte noch Break/Baud in eine neue Session tragen.

**Voraussetzungen:** V2-016 bis V2-018.

**Konkrete Arbeit:** PHY-Zustand `Quiescing` und begrenzte Recovery-Deadline umsetzen. Nach Timeout/`OutcomeUnknown` keine neue Wire-Aktion; konkrete Bulk-/EP0-Operation canceln/drainen, terminal pensionieren und Mark/8N1/Baud korreliert herstellen. Ist Quiescence nicht beweisbar, Generation schließen.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_transport_quiescence`: später physisch wirksamer Transfer, Cancel-Erfolg/-Fehler, Retiring-Terminal, Quiesce-Timeout. **[HW-K409]** `transport_quiescence` erzwingt Timeout/Disconnect an Bulk OUT und EP0.

**Akzeptanzkriterium:** Zwischen ungeklärtem Ausgang und Quiescence/neuer Generation erscheint keine neue Wire-Operation; alte Terminalevents beleben keinen Turn wieder.

### TODO V2-020 – Hot-Unplug- und Shutdown-Lifetime absichern **[HW-K409]**

**Ziel:** Disconnect darf weder Use-after-free noch Submission gegen bereits abgebauten Pool/EP0 erzeugen.

**Voraussetzungen:** V2-014 bis V2-019.

**Konkrete Arbeit:** Submission-Gate bei Disconnect atomar schließen; Port, Ringe und Notification-Handle mit Boot-Lifetime halten; Transferreferenzen erst nach Retirement abbauen. Systemshutdown stoppt/joint Clienttask und Callbacks vor Handle-Invalidierung.

**Tests/Verifikation:** HOST-Interleavingtest `v2_port_lifetime` unter ASan/TSan. **[HW-K409]** `hot_unplug_transport_states`: Unplug während RX, Bulk-OUT, jeder Composite-Suboperation und Quiescence; danach Reconnect.

**Akzeptanzkriterium:** Kein Crash, Hang, UAF oder Submission nach Gate-Close; neue Verbindung hat neue Generation und keine alten Events wirken.

### TODO V2-021 – Target-Uhr und spinfreien Deadline-Wake implementieren **[TARGET]**

**Ziel:** Core-eigene Mikrosekundenfenster zuverlässig wecken, ohne Timersemantik in den Runner zu verschieben.

**Voraussetzungen:** V2-007, V2-004.

**Konkrete Arbeit:** Cross-core linearizable `EspMonotonicClock`, frühes Abrunden grober Tick-Waits und einen beim Startup vorallokierten High-Resolution-One-Shot für positiven Sub-Tick-Rest implementieren. Timer callback setzt nur Notification; stale Wake ist bedeutungslos.

**Tests/Verifikation:** HOST-Modell `v2_runner_wake_math`. **[TARGET]** `deadline_wake`: Cross-core Monotonie, Auflösung, frühe/späte/stale Wakes, wiederholte Sub-Tick-Fenster, kein Null-Tick-Busy-Spin, Wake-Jitterhistogramm.

**Akzeptanzkriterium:** Kein geplanter Wake liegt nach `next_wakeup()`; Sub-Tick-Reste blockieren spinfrei; Clockwerte gehen cross-core nie rückwärts.

### TODO V2-022 – TaskBootstrap und kontrollierte Prioritäten implementieren **[TARGET]**

**Ziel:** Schedulingrelation nicht annehmen, sondern setzen und vor Protocolstart verifizieren.

**Voraussetzungen:** V2-004, V2-021.

**Konkrete Arbeit:** Beide EspUsbHost-Tasks explizit auf Startwert 7, Protocol Runner 6, Processing 3, Arduino-Loop/UI 2 und Logger 1 konfigurieren; Werte zurücklesen. Keine Core-Affinity setzen. Abweichung sperrt KWP-Start als Startupfault.

**Tests/Verifikation:** HOST-Konfigurationstest `v2_task_priority_policy`. **[TARGET]** `task_bootstrap` prüft Namen, reale Prioritäten, Blockverhalten, Stack-High-Watermarks und dass der Runner im Idle nicht busy-spinnt.

**Akzeptanzkriterium:** `USB host = USB client > runner > processing > UI > logger` ist auf dem finalen Build nachgewiesen; alle Tasks besitzen initiale Stackreserve.

### TODO V2-023 – Physische FTDI-TX-/Control-Grundgrenzen messen **[HW-K409, HW-LA]**

**Ziel:** Vor KWP-Implementierung belastbare Bounds für Submit→Completion→K-Line und Break/Mark erhalten.

**Voraussetzungen:** V2-017 bis V2-022; kalibrierter Logic Analyzer und sicherer K-Line-Benchaufbau.

**Konkrete Arbeit:** Instrumentierten Low-Level-Testbuild verwenden; Bulk-OUT-Byte, Break-on/off, Baud-/Line-Coding und Latency-Control jeweils vielfach ausführen. Submit-, Completion- und physische Flankenzeit erfassen; Min/Max/Jitter und Cancel/Drain-Verhalten dokumentieren. Noch keine ECU-Toleranz behaupten.

**Tests/Verifikation:** RED: Trace-Auswerter lehnt fehlende Token/Flanken und künstliche Grenzverletzung ab. GREEN: reale Trace mit `./tools/check_logic_trace.sh low_level <capture>` auswerten; Hash und Geräteaufbau dokumentieren.

**Akzeptanzkriterium:** Für jede Operation existieren korrelierte gemessene Bounds; unklare oder unbounded Completion/physische Wirkung blockiert Phase 2.

## Phase 2 – KWP1281-Kommunikation

### TODO V2-024 – KwpProtocolCore-Ereignis-/Aktionspumpe anlegen

**Ziel:** Einen reinen Core schaffen, der Events verarbeitet und tokenisierte Aktionen bis Quieszenz ausgibt.

**Voraussetzungen:** Phase 1 vollständig grün.

**Konkrete Arbeit:** Core-Shell mit `handle(event, now)`, `advance(now)`, `next_wakeup()` und festem Aktionsspeicher implementieren. Jede ausgelöste Aktionsfolge plus synchrones Submitresultat wird abgeschlossen, bevor das nächste RX-Event akzeptiert wird. `ArmDeadline` existiert nicht.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_protocol_core_pump`: deterministische Aktion, synchrones Submitresultat vor nächstem RX, keine zweite aktive Operation, stale Wake ohne Wirkung, Platformheader-Guard.

**Akzeptanzkriterium:** Derselbe Core läuft ausschließlich mit FakeClock/FakeTransport; höchstens eine semantische Aktion ist aktiv und alle Zeitfenster bleiben Core-owned.

### TODO V2-025 – KwpTimingProfile und physisch→software Zeitfenster implementieren

**Ziel:** Alle Timingwerte zentral, versioniert und konservativ aus Hardwarebounds ableiten.

**Voraussetzungen:** V2-007, V2-023, V2-024.

**Konkrete Arbeit:** `KwpTimingProfile` mit BusIdle, 5-Baud-Zellen, Sync/KB, `~KB2`, Echo, ECU-ACK, RX-inverse-ACK, Turnaround, Keep-Alive, Completion und Quiescence erstellen. Physische Fenster über gemessene Ingress-/Egress-Min/Max in Submitfenster transformieren; leeres garantiertes Fenster ist Konfigurationsfehler.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_timing_profile`: Minimum/Maximum, Callback-Batchverzug, Egressjitter, physisches Minimum nicht blind auf Callbackzeit addieren, leeres Fenster, `[not_before, deadline)`-Rand.

**Akzeptanzkriterium:** Keine Magic Number liegt in Engine/UI; Profil-ID und Bounds sind auslesbar; ungültiges Profil verhindert Protocolstart.

### TODO V2-026 – Configuring und BusIdle implementieren

**Ziel:** Jeder Initversuch beginnt mit neuer Sessionepoch, bekannter Leitung und nachgewiesener Busruhe.

**Voraussetzungen:** V2-024, V2-025.

**Konkrete Arbeit:** `Disconnected → Configuring → BusIdle` implementieren. Vor jedem Versuch Sessionepoch vergeben; Baud/Latency/DTR/RTS/Mark/8N1 tokenisiert abschließen; neue Ingress-Epoche erst unmittelbar vor BusIdle öffnen. Eingehendes Byte startet/invalidiert Idlefenster gemäß Policy.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_config_bus_idle`: Controls in Reihenfolge, Fehler/Timeout nach Quiescing, 2600-ms-Referenz nur Profilwert, Byte kurz vor Idle-Ende, neue Epoch bei jedem Versuch.

**Akzeptanzkriterium:** 5-Baud-Aktion kann nur nach vollständiger Konfiguration und ununterbrochenem BusIdle-Fenster entstehen.

### TODO V2-027 – Absolute 5-Baud-Wellenform im Core implementieren

**Ziel:** Adresse `0x01` als zehn absolute Zellen ohne kumulierenden USB-Control-Jitter erzeugen.

**Voraussetzungen:** V2-025, V2-026.

**Konkrete Arbeit:** Startbit, acht Datenbits LSB-first und Stopbit modellieren. `t0` aus bestätigter Startflankenoperation; weitere Sollgrenzen `t0+n*bit_period`; gleiche Pegel ohne redundanten Controltransfer. Jede echte Flanke hat Submit-/Completionfenster; überlappende oder verspätete Controloperation führt zu Quiescing. Stop-Mark vollständig halten; `pending_sync` mit genau einem Slot.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_5baud_waveform`: exakte Pegelfolge 0x01, identische Pegel, frühe/späte/fehlende Completion, Stop-Hold, frühes 0x55 in `pending_sync`, zweites Byte als Fault.

**Akzeptanzkriterium:** FakeTransport zeigt die absolute, nicht re-anchored Wellenform; kein Control-Overlap; WaitSync erst nach konservativem Stop-Hold.

### TODO V2-028 – 5-Baud-Wellenform physisch freigeben **[HW-K409, HW-LA]**

**Ziel:** Beweisen, dass FTDI-EP0 mit dem K409 die geforderten K-Line-Zellbreiten tatsächlich erreicht.

**Voraussetzungen:** V2-027; ECU zunächst nicht erforderlich.

**Konkrete Arbeit:** Target-Core mit realem PHY ausführen; K-Line-Flanken, Token, Submit und Completion gemeinsam capturen. Mindestens Best-/Normal-/Stress-Scheduling messen; gleiche Pegel und vollständigen Stop-Mark-Hold prüfen.

**Tests/Verifikation:** Trace-Checker zunächst an manipulierter Trace rot, dann reale **[HW-LA]** Trace mit `check_logic_trace.sh 5baud`. Falls ECU-Grenzen noch unbekannt sind, werden hier nur die physischen Adapterwerte freigegeben; das produktive Timingprofil erhält seine endgültige ECU-Freigabe erst in V2-066.

**Akzeptanzkriterium:** Effektive Wellenform entspricht dem Profil und besitzt begrenzten Jitter. Ist EP0 nicht geeignet, stoppt die Umsetzung hier zur Architekturentscheidung.

### TODO V2-029 – Sync-/Keybyte-Handshake und `~KB2` implementieren

**Ziel:** `0x55`, KB1/KB2 und inverse KB2-Antwort mit korrektem Zeitanker abwickeln.

**Voraussetzungen:** V2-027, V2-028.

**Konkrete Arbeit:** WaitSync, WaitKeyBytes, KeyAckWindow und KeyAckTx implementieren. `~KB2`-Fenster am Callback-Batch-Timestamp von KB2 ankern; Submit/Completion/Echo getrennt verfolgen. Identifikation erst nach aufgelöster tokenisierter Operation plus Echo starten.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_keybytes`: korrekt, falsches Sync, fehlendes/zusätzliches Keybyte, zu früh/zu spät, Completion vor/nach Echo, stale Token, Deadlinegleichheit.

**Akzeptanzkriterium:** Genau ein `~KB2` wird im offenen Submitfenster ausgegeben; kein altes oder unkorreliertes Ereignis öffnet Identify.

### TODO V2-030 – Host-TX-Byte, lokales Echo und ECU-Invers-ACK implementieren

**Ziel:** Ein Hostbyte erst nach Completion, korrektem Echo und gegebenenfalls inverser ECU-ACK abschließen.

**Voraussetzungen:** V2-024, V2-025, V2-029.

**Konkrete Arbeit:** `TxSubmitByte`, `TxAwaitLocalEcho`, `TxAwaitInverseEcuAck` und Evidenzset implementieren. Completion darf cross-channel vor/nach Echo sichtbar sein; im RX-Strom muss Echo vor ECU-ACK/-Datenbyte liegen. Mismatch/Timeout beendet Turn und sendet kein Folgebyte.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_tx_turn`: alle zulässigen Completion-/Echo-Reihenfolgen, fehlendes/doppeltes/falsches/spätes Echo, falsches/fehlendes ECU-ACK, ECU-ACK vor Echo, stale Completion.

**Akzeptanzkriterium:** Ein neuer semantischer TX beginnt nur nach vollständiger Evidenz; `max_semantic_in_flight` bleibt 1 und Counter bleibt bei Fehler unverändert.

### TODO V2-031 – `pending_rx_after_echo` implementieren

**Ziel:** Ein frühes ECU-Folgebyte bis zur noch fehlenden TX-Completion begrenzt und korrekt aufbewahren.

**Voraussetzungen:** V2-030.

**Konkrete Arbeit:** Genau einen Core-eigenen Slot mit Byte, Originaltimestamp, Generation, Ingress-Epoche und Vorgängertoken implementieren. Nach passender Completion nur weiterverarbeiten, wenn Korrelation stimmt und ACK-Submitfenster aktuell offen ist; zweites pending Byte ist Order-Fault.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_deferred_rx`: Completion knapp vor/nach ACK-Deadline, Reset/Disconnect/Sessionwechsel, falscher Vorgängertoken, zweites Byte, Originaltimestamp bleibt erhalten.

**Akzeptanzkriterium:** Kein zweiter TX wird vor Completion gestartet; das gespeicherte Byte wird weder überschrieben noch verspätet geACKt.

### TODO V2-032 – ECU-RX-Byteengine und inverse Byte-ACKs implementieren

**Ziel:** ECU-Bytes frameweise empfangen und jedes erforderliche Byte rechtzeitig invers quittieren.

**Voraussetzungen:** V2-025, V2-030, V2-031.

**Konkrete Arbeit:** `RxAwaitLength`, `RxAwaitByte`, `RxAckSubmit`, `RxAwaitLocalEcho` und `RxAwaitNextByte` implementieren. ACK-Fenster am originalen Batchtimestamp verankern; bei bereits geschlossenem Fenster keine verspätete Wire-Aktion. RX-Overflow/Generation-/Epochenwechsel invalidiert sofort.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_rx_ack`: einzelne/gebatchte Bytes, rechtzeitiges Event spät konsumiert, ACK-Submit bereits zu spät, Echo der inversen ACK, Overflow mitten im Frame, falsche Epoche.

**Akzeptanzkriterium:** Für jedes zulässige ECU-Byte entsteht exakt eine inverse ACK im offenen Fenster; kein Fehlerpfad sendet blind weiter.

### TODO V2-033 – KwpFrameBuilder und immutable KwpFrameEnvelope implementieren

**Ziel:** Frames vollständig im Core besitzen, minimal validieren und pointerfrei übergeben.

**Voraussetzungen:** V2-032.

**Konkrete Arbeit:** Fester Builder bis 65 Byte und immutable `KwpFrame`/`KwpFrameEnvelope` implementieren. Länge, exakte Gesamtgröße, Terminator, Titel, Counter und „kein Overflow seit Beginn“ prüfen. Envelope enthält Sequenz, Sessionepoch, Zeitstempel, Request-ID und Dialogkontext; keine Views/Heaptypen.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_frame_builder`: valide Grenzlängen, 0/2/65/255, fehlender/falscher Terminator, zu viele Bytes, Reset, `static_assert` für Size/Trivial-Copy.

**Akzeptanzkriterium:** Nur vollständig valide eigene Wertobjekte verlassen den Core; Builder kann danach sofort sicher wiederverwendet werden.

### TODO V2-034 – Ausgehende KWP-Blöcke und Counterregeln implementieren

**Ziel:** Requests byteweise mit gemeinsamem Blockcounter und korrekten inversen ACK-Turns senden.

**Voraussetzungen:** V2-030, V2-033.

**Konkrete Arbeit:** Blockkonstruktion für `0x12`, `0x29 <group>` und `0x09`, terminierte Länge und Counterübergänge implementieren. Counter nur nach nachweislich erfolgreichem Protokollschritt fortschreiben; Wrap, Duplicate, Skip und Reset typisieren.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_tx_blocks` und Referenzbytes aus Ledger: vollständiger Block, Fehler an jeder Byteposition, ACK-Timeout, Counter-Wrap/-Mismatch, kein Folgeturn nach Fehler.

**Akzeptanzkriterium:** Ausgabebytes entsprechen den belegten Referenzsequenzen; Counter bleibt über Fehler und Recovery deterministisch.

### TODO V2-035 – Post-Block-Turnaround als Zeitfenster implementieren

**Ziel:** Nächste Hostaktion weder zu früh noch zu spät nach ECU-Terminator beginnen.

**Voraussetzungen:** V2-025, V2-032, V2-034.

**Konkrete Arbeit:** `TurnaroundWindow` am Empfangstimestamp des Terminators verankern. Profil-Min/Max konservativ in Submitfenster übersetzen; kein `delay(30)`. Frühe Aktion bleibt pending, späte Aktion faultet.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_turnaround`: vor Minimum, am `not_before`, vor Deadline, an Deadline, verzögerter Callback, Egressjitter und leeres Fenster.

**Akzeptanzkriterium:** Erste Aktion des nächsten Blocks wird ausschließlich innerhalb des Core-Fensters freigegeben.

### TODO V2-036 – Identifikationssession implementieren

**Ziel:** Nach Keybyte-Handshake alle Identifikationsblöcke autonom verarbeiten und sauber nach Measuring wechseln.

**Voraussetzungen:** V2-029 bis V2-035.

**Konkrete Arbeit:** Identification-, ACK-/Terminatorsequenzen und Dialogkontext implementieren. Minimaler Critical-Path prüft nur Metadaten; Textinterpretation bleibt späterem Parser vorbehalten. Timeout, Counterfehler und Refusal führen zur typisierten Recovery.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_identification` anhand bytegenauer Referenzframes: mehrere ID-Blöcke, Abschluss, unbekannter Titel, beschädigter Counter, Disconnect an jeder Zustandsgrenze.

**Akzeptanzkriterium:** Core erreicht Measuring ohne Parser/UI-Rückmeldung; valide ID-Frames werden unverändert publiziert.

### TODO V2-037 – Autonomen MeasurementPlan für Gruppen 000–004 implementieren

**Ziel:** Die Messgruppenfolge vollständig im Session-Core betreiben.

**Voraussetzungen:** V2-034 bis V2-036.

**Konkrete Arbeit:** Immutable/versionierten Plan by value implementieren: Gruppe 000 über `0x12`, Gruppen 001–004 über `0x29`, notwendige `0x09`-Turns und Keep-Alive. Request-ID/Dialogkontext beim Senden festlegen. Nächste Aktion nur aus Sessionzustand, minimalen Frame-Metadaten und aktivem Plan ableiten.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_measurement_plan`: mehrere vollständige Zyklen, Gruppe verweigert, fehlender Consumer, Keep-Alive, Counter-Wrap; Testlinker stellt sicher, dass Parser/Domain/UI nicht gelinkt sind.

**Akzeptanzkriterium:** Der Dialog läuft unbegrenzt mit Fake-ECU weiter, auch wenn kein Parser, Snapshot oder UI existiert.

### TODO V2-038 – Sichere PlanChange-/ControlCommand-Übernahme implementieren

**Ziel:** UI-/Application-Kommandos optional halten und nur an sicheren Grenzen anwenden.

**Voraussetzungen:** V2-037.

**Konkrete Arbeit:** Wertbasierte Commands mit Request-ID und vollständiger Plankopie/vorhandener Plan-ID implementieren. Höchstens ein Command nach quieszentem Critical Pump und nur mit ausreichendem Slack übernehmen; laufender Turn/Keep-Alive nutzt bis dahin alten Plan. Full lehnt Command nonblocking ab.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_plan_change`: fehlend, verspätet, voll, Command-Flood, Planwechsel mitten im Turn, Bestätigung erst nach sicherer Übernahme.

**Akzeptanzkriterium:** Kein Commandpfad verändert ACK-/Turnaround-/Keep-Alive-Latenz; ohne Command läuft der alte Plan unverändert weiter.

### TODO V2-039 – Vollständige Protocol-/Transport-Recovery implementieren

**Ziel:** Alle Architekturfehler in deterministische Zustandsübergänge und neue Epochen überführen.

**Voraussetzungen:** V2-019, V2-026 bis V2-038.

**Konkrete Arbeit:** Faultmatrix aus Architekturabschnitt 10 umsetzen: Disconnect, RX-/Eventoverflow, Submitvarianten, Completion-/Echo-/ACK-/Sessiontimeout, Frame-/Counterfehler und Keep-Alive. Recovery mit begrenztem Backoff; Quiescing vor Reuse; neue Sessionepoch pro Versuch, neue Transportgeneration nur per neuem Handle.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit tabellengetriebenem `v2_protocol_recovery`: jeder Fault aus jedem relevanten Zustand, stale Events während/nach Recovery, keine alte Deadline/Builder-/Pending-Daten.

**Akzeptanzkriterium:** Jeder Fehler besitzt genau eine dokumentierte Reaktion; keine Recovery sendet vor Quiescence oder interpretiert alte Bytes weiter.

### TODO V2-040 – Zustandsbehafteten ECU-Emulator und Erfolgsintegration erstellen

**Ziel:** Den vollständigen realen Protocol Core ohne Fahrzeug byte- und zeitgenau testen.

**Voraussetzungen:** V2-029 bis V2-039.

**Konkrete Arbeit:** Emulator für BusIdle/Sync/Keybytes, lokales Echo, inverse ACKs, Counter, Identifikation und Gruppen 000–004 implementieren. Er treibt exakt den Production-Core über FakeTransport; keine zweite Protokolllogik im Testharness umgehen.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_ecu_emulator_happy_path`: Kaltstart bis mehrere Messzyklen, exakte Wiretranskription, Request-/Responsekontext und Keep-Alive.

**Akzeptanzkriterium:** Ein deterministischer Test erreicht und hält Measuring über mindestens 100 Planzyklen ohne Echtzeit-Sleeps oder unerwarteten Fault.

### TODO V2-041 – Fault-Injection-Matrix und Fuzzing des Protocol Core abschließen

**Ziel:** Hangs, Bufferfehler und unzulässige Zustandsübergänge vor Targetintegration finden.

**Voraussetzungen:** V2-040.

**Konkrete Arbeit:** FaultScript für Drop/Delay/Duplicate/Reorder/Corrupt/Disconnect an jeder Byteposition erweitern; strukturgeführtes Fuzzing beliebiger Event-/Bytefolgen und Counter/Längen aufsetzen. Invarianten: bounded actions, ein aktiver Turn, kein OOB, kein Endlosloop, keine Wireaktion nach fatalem Fault.

**Tests/Verifikation:** `v2_protocol_fault_matrix`, ASan/UBSan und Fuzz-Corpus aus Pflichtszenarien; mindestens definierter zeitbegrenzter Fuzzlauf lokal und längerer CI-Lauf. Regressionsinput jedes Fundes fest einchecken.

**Akzeptanzkriterium:** Gesamte Matrix grün; Fuzzlauf endet ohne Crash, Hang, Sanitizerbefund oder Invariantenbruch.

## Phase 3 – Entkopplung und Datenerfassung

### TODO V2-042 – ValidatedFrameQueue mit Drop-newest und sichtbaren Sequenzlücken implementieren

**Ziel:** Validierte Frames nonblocking vom Protocol Runner an Processing übergeben.

**Voraussetzungen:** V2-033, V2-041.

**Konkrete Arbeit:** Queue mit 32 Envelopes by value implementieren. `rx_sequence` vor `try_send` für jeden validierten Frame vergeben; bei Full neuestes Frame droppen, Zähler/High-Watermark aktualisieren, KWP-Turn normal fortsetzen.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_validated_frame_queue`: FIFO, genau 32, Full, Sequenzlücke nach Drop, Builderwiederverwendung ohne Datenänderung, langsamer/fehlender Consumer.

**Akzeptanzkriterium:** Queuefull blockiert nie und beeinflusst den Corezustand nicht; nächster empfangener Frame macht den Drop durch Sequenzlücke sichtbar.

### TODO V2-043 – Alle FreeRTOS-Kanäle mit fester Policy implementieren **[TARGET]**

**Ziel:** Architekturkapazitäten, Ownership und Full-Policies 1:1 auf dem Target abbilden.

**Voraussetzungen:** V2-013, V2-042.

**Konkrete Arbeit:** `FreeRtosChannels` für Framequeue 32, Commandqueue 8, Snapshot-/Statusmailbox je 1, Persistence-/Diagnosticqueue je 128 erstellen. Statische/allokationskontrollierte Erzeugung beim Startup; keine Multi-Consumer-Queue. Produceroperationen entsprechend `try_send`/overwrite.

**Tests/Verifikation:** HOST-Policytest `v2_channel_policies`; **[TARGET]** `freertos_channels` prüft Kapazität, Copy-by-value, Full-/Overwrite-Verhalten, High-Watermarks und dass Consumerarbeit keine Queue-Critical-Section hält.

**Akzeptanzkriterium:** Jeder Kanal verhält sich exakt wie Architekturabschnitt 8; alle Full-Fälle sind testbar und telemetrisch sichtbar.

### TODO V2-044 – ProtocolRunner mit geordnetem Pump und Cutoff integrieren

**Ziel:** PHY, Ringe, Core und Downstream-Kanäle in einem exklusiven, ereignisgetriebenen Owner verbinden.

**Voraussetzungen:** V2-012 bis V2-022, V2-041 bis V2-043.

**Konkrete Arbeit:** Runner implementieren: invalidierende Stickies zuerst, RX-/Critical-Head nach gemeinsamer Sequenz mergen, Event an Core, Aktionen samt synchronem Submitresultat bis Quieszenz, danach nächstes Event. Vor `advance` Publication-Cutoff; Commands erst nach Critical-Quieszenz und begrenzt. Downstreamsend immer Timeout 0/overwrite.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_protocol_runner`: Cross-ring-Reorder, Disconnectvorrang, Eventoverflow/Poison, Callback zwischen Empty und Deadline, stale Wake, Actionpump, Command-Flood, volle Framequeue.

**Akzeptanzkriterium:** Runner ist alleiniger PHY/Core-Owner; kein Downstreamzustand ist Voraussetzung für Pump, Deadline oder Sessionfortschritt.

### TODO V2-045 – Runner auf Target mit synthetischem Adapter verifizieren **[TARGET]**

**Ziel:** FreeRTOS-Scheduling, Notifications, Cutoff und Core unverändert auf dem realen ESP32-P4 testen, bevor die ECU angeschlossen wird.

**Voraussetzungen:** V2-021, V2-022, V2-044.

**Konkrete Arbeit:** Einen compile-time Testadapter verwenden, der echte Callback-/Taskkontexte und Fake-Wire-Ereignisse erzeugt, aber denselben Production-Runner/Core nutzt. Ereignisse cross-core, an Deadlinegrenzen und in Bursts injizieren.

**Tests/Verifikation:** **[TARGET]** `protocol_runner_concurrency`: lost/coalesced Wake, Cutoff-Race, Mid-callback-Reopen, Criticalringoverflow, Sub-Tick-Wake, Command-Flood und Prioritätsprüfung. Targettrace archivieren.

**Akzeptanzkriterium:** Alle Szenarien PASS; kein Busy-Spin, Deadlock, falscher Timeout oder nichtdeterministischer Zustand über mindestens 10.000 Wiederholungen.

### TODO V2-046 – Strukturierte Raw-Frame-Capture hinter der Übergabegrenze implementieren

**Ziel:** Empfangene valide Frames nachvollziehbar erfassen, ohne Byte-/ACK-Pfad zu belasten.

**Voraussetzungen:** V2-042 bis V2-045.

**Konkrete Arbeit:** `RawFrameRecord` als fester Werttyp aus Envelope plus Timing-/Dropkontext definieren. Processing publiziert nonblocking an PersistenceQueue; Logger schreibt versioniertes Format mit Profil-/Build-ID und Sequenzlücken. Keine per-Byte-Datei-/Textausgabe im Runner.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_raw_capture`: Byteidentität, Metadaten, Formatversion, Queuefull/Storagefehler, Roundtrip-Reader und explizite Lücke.

**Akzeptanzkriterium:** Capture kann exakt zurückgelesen werden; blockierter Sink erzeugt nur gezählte Recorddrops und keine KWP-Auswirkung.

### TODO V2-047 – 60-s-Downstream-Sättigung nachweisen

**Ziel:** Die zentrale Entkopplungsgarantie bereits vor Parser/UI belegen.

**Voraussetzungen:** V2-040, V2-042 bis V2-046.

**Konkrete Arbeit:** Emulator/Targetadapter lässt Protocol weiterlaufen, während Frameconsumer und Persistenceconsumer 60 s angehalten sind. Danach Consumer fortsetzen und Sequenz-/Droptelemetrie prüfen. ACK-/Sessionmetriken vor, während und nach Stall vergleichen.

**Tests/Verifikation:** HOST `v2_downstream_stall_60s` mit beschleunigter FakeClock; **[TARGET]** `downstream_stall_60s` in Echtzeit. Logger und UI-Kontext zusätzlich blockieren.

**Akzeptanzkriterium:** Sessionfortschritt und ACK-Latenzverteilung ändern sich innerhalb der festgelegten Messmarge nicht; nur `frame_drop_full`/optionale Drops steigen; kein RX-Overflow entsteht.

### TODO V2-048 – Hot-Path-Allokation und Callback-/Runner-WCET instrumentieren **[TARGET]**

**Ziel:** Verbotene Allokation und unerwartet lange Critical-Path-Arbeit automatisch erkennen.

**Voraussetzungen:** V2-044 bis V2-047.

**Konkrete Arbeit:** Startup-Grenze markieren; danach Allocation-Counter/Assert für Runner, RX/TX/Control-/Lifecycle-Callbacks aktivieren. Feste Latenzhistogramme und Callback-WCET ohne per-Event-Queue/Formatierung ergänzen.

**Tests/Verifikation:** RED-Fixture mit absichtlicher Testallokation muss anschlagen. GREEN: HOST-SAN und **[TARGET]** `hot_path_budget` unter RX-Bursts, Queuefull und Command-Flood.

**Akzeptanzkriterium:** Null Hot-Path-Allokationen nach Startup; kein formatierter Callbacklog; gemessene WCET liegt innerhalb des vorläufigen Timingbudgets.

## Phase 4 – Auswertung und Anwendungsdaten

### TODO V2-049 – Captures unveränderlich übernehmen und normalisieren

**Ziel:** Bestehende Fahrzeugdaten als reproduzierbare Golden-Testbasis nutzen, ohne Altcodeabhängigkeit.

**Voraussetzungen:** V2-006, V2-046.

**Konkrete Arbeit:** `captures/engine_running_corrected_replay.csv`, Referenztext und nötige belegte Frames nach `testdata/captures/` kopieren; Originalhash, Quelle und Transformationsskript dokumentieren. In ein kanonisches byte-/zeitgenaues Replayformat konvertieren; Originalkopien nie editieren.

**Tests/Verifikation:** `v2_capture_integrity` prüft Hashes, Parsebarkeit, Frameanzahl/-längen und deterministische erneute Konvertierung. Manipulierte Fixture muss rot sein.

**Akzeptanzkriterium:** Golden-Daten sind reproduzierbar, provenance-gebunden und ohne Include aus `ReplayData.h` nutzbar.

### TODO V2-050 – Generischen KWP-Anwendungsparser implementieren

**Ziel:** Valide Frames hinter der Queue in typisierte Nachrichten überführen.

**Voraussetzungen:** V2-033, V2-049.

**Konkrete Arbeit:** Werttypen `Identification`, `Ack`, `GroupHeader`, `GroupBody`, `GroupRefused`, `Unknown` und Parser implementieren. Payload-Mindestlängen erneut prüfen, Dialogkontext nutzen, keine View über Verarbeitungsschritt hinaus speichern.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_kwp_application_parser`: alle beobachteten Titel, Grenzlängen, unbekannt, falscher Kontext und Capture-Goldenframes; ASan/UBSan.

**Akzeptanzkriterium:** Jeder Input ergibt deterministisch einen typisierten Wert oder `ParserRejected`; keine Lifetime hängt am Queueelement.

### TODO V2-051 – Digifant-Gruppe 000 decodieren

**Ziel:** Rohfelder und belegte RPM-Umrechnung der Gruppe 000 korrekt abbilden.

**Voraussetzungen:** V2-050, Referenzeinträge im Ledger.

**Konkrete Arbeit:** Decoderpfad für Gruppe 000 mit Rawwert, Einheit, Quellgruppe und Validitätsstatus implementieren. Nur belegte Umrechnungen physikalisch benennen; unbekannte Felder Raw/Unknown lassen.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_digifant_group000`: Goldenframes, Grenzrohwerte, ungültige Länge, unbekannte Positionen und erwartete RPM-Werte.

**Akzeptanzkriterium:** Goldenwerte stimmen mit belegter Referenz überein; keine unbelegte Semantik wird erfunden.

### TODO V2-052 – Header-/Tabellenmodell für Gruppen 001–004 implementieren

**Ziel:** Headerdaten sicher an Sessionepoch, Gruppe und Sequenz binden.

**Voraussetzungen:** V2-050.

**Konkrete Arbeit:** Tabellen-/Zonendeskriptoren aus `GroupHeader` als Processing-eigenen Cache modellieren. Cache bei Sessionwechsel, Sequenzlücke, Refusal oder inkompatiblem Body invalidieren; keine Core-Rückkopplung.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_digifant_header_cache`: Header→Body, falsche Gruppe/Epoch, Drop-Lücke, doppelter/neuer Header, Body ohne Header.

**Akzeptanzkriterium:** Ein Body wird nur mit eindeutig passendem, aktuellem Header decodiert; unsichere Korrelation ergibt Invalid/Unknown.

### TODO V2-053 – Digifant-Formeln und Gruppen 001–004 decodieren

**Ziel:** Belegte Messwerte reproduzierbar aus Header/Body-Zonen berechnen.

**Voraussetzungen:** V2-051, V2-052.

**Konkrete Arbeit:** Formeln `0x8B`, `0x8C`, `0x85`, `0x88`, `0x89`, Batterie Gruppe 002/Zone 3 und G69 Raw Gruppe 003/Zone 3 implementieren. Tabellenarithmetik, Grenz-/Rundungsverhalten explizit festlegen. G69 nicht als kalibrierten Winkel ausgeben.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit je separaten Tests `v2_formula_8b`, `v2_formula_8c`, `v2_formula_85_88_89` und `v2_digifant_groups_001_004`; Golden- und Grenzwerte.

**Akzeptanzkriterium:** Alle belegten Capturewerte stimmen; unbekannte Formel/Zone bleibt Raw/Unknown, nicht still 0 oder physikalisch interpretiert.

### TODO V2-054 – MeasurementModel und Gültigkeits-/Staleness-Regeln implementieren

**Ziel:** Messwerte mit Quelle, Alter und Datenqualität konsistent halten.

**Voraussetzungen:** V2-051 bis V2-053.

**Konkrete Arbeit:** Single-Owner-Modell für Processing mit Wert/Rohwert, Einheit, Timestamp, Sequenz, Sessionepoch, Quellgruppe/-zone und `Valid/Stale/Unsupported/Invalid/Unknown`. Sessionwechsel/Disconnect macht alte Werte sichtbar stale/disconnected.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_measurement_model`: Updates verschiedener Gruppen, Zeitfortschritt, Drop-Lücke, Sessionwechsel, Disconnect, Unsupported und Unknown.

**Akzeptanzkriterium:** Kein alter Wert erscheint nach Sessionwechsel als aktuell; jedes Signal trägt nachvollziehbare Provenienz und Gültigkeit.

### TODO V2-055 – ProcessingService integrieren

**Ziel:** Framequeue, Parser, Decoder, Headercache und Domainmodell in genau einer Downstream-Task verbinden.

**Voraussetzungen:** V2-042, V2-050 bis V2-054.

**Konkrete Arbeit:** ProcessingService als alleinigen Owner von Parsercache/MeasurementModel implementieren. Auf leere Eingangsqueue darf er unbegrenzt warten; alle Ausgänge sind nonblocking/overwrite. Sequenzlücken invalidieren abhängige Decoderzustände vor Verarbeitung des nächsten Frames.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_processing_service`: Goldenframes, Sequenzlücke, ParserRejected, volle Persistencequeue und gestoppter Outputconsumer.

**Akzeptanzkriterium:** Processing erzeugt korrekte Modellupdates, hält keine Queue-/Runnerressource während Parsing und kann den Protocolpfad nicht blockieren.

### TODO V2-056 – Immutable MeasurementSnapshot und Latest-Mailbox implementieren

**Ziel:** UI und Telemetrie erhalten konsistente, vollständig eigene Zustandskopien.

**Voraussetzungen:** V2-043, V2-054, V2-055.

**Konkrete Arbeit:** Snapshot mit Messwerten, Gültigkeit/Alter, Session-/Motorstatus, Generation, letzter RX-Sequenz und Fault-/Dropindikatoren erstellen. Processing publiziert by value mit Overwrite in Länge-1-Mailbox; UI erhält Empfangskopie.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_measurement_snapshot`: atomarer Gesamtzustand, schnelle Overwrites/langsamer Leser, Sessionwechsel, keine Pointer/Views, Trivial-Copy-/Sizecheck sofern Queuevertrag dies verlangt.

**Akzeptanzkriterium:** Leser sieht ausschließlich vollständige alte oder neue Snapshots; langsamer Leser blockiert Processing/Protocol nicht.

### TODO V2-057 – ProtocolTelemetry und Statusmailboxes implementieren

**Ziel:** Alle Pflichtmetriken ohne synchrones Logging im Critical Path sichtbar machen.

**Voraussetzungen:** V2-043, V2-044, V2-048.

**Konkrete Arbeit:** Single-Writer-Counter, feste Histogramme, Ring-/Queue-High-Watermarks, letzte Fault-/Statewerte und Overwrite-Status je Consumer implementieren. Snapshot bei Zustandswechsel, sonst höchstens alle 250 ms. Heap/DMA/PSRAM/Stack nur außerhalb Critical Path höchstens 1 Hz samplen.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_protocol_telemetry`: jeder Fault-/Dropzähler, Maxima, Snapshotrate, verlorenes DiagnosticEvent bei erhaltenem sticky Status. **[TARGET]** `telemetry_health_sampling` prüft Heap-/Stackwerte.

**Akzeptanzkriterium:** Architektur-Pflichtmetriken sind numerisch verfügbar; kein Zähler hängt vom Erfolg einer Loggerqueue ab.

### TODO V2-058 – Logger/PersistenceService mit unabhängigen Sinks implementieren

**Ziel:** Formatierung und ungebundene Serial-/Storage-I/O vollständig aus Protocol und Processing fernhalten.

**Voraussetzungen:** V2-046, V2-057.

**Konkrete Arbeit:** Strukturierte Diagnostic-/Persistence-Records konsumieren; Debugserial best effort, persistente Raw-/Messdatei mit Sequenzlücken und UI-Log kompakt. Pro Sink begrenzter Timeout/Backoff; defekter Sink kann deaktiviert werden und hält keinen fremden Lock.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_logger_service`: blockierter/fehlerhafter Sink, volle Queue, Restart, Formatversion, Dropzähler. **[TARGET]** `logger_stall` blockiert Storage dauerhaft bei weiterlaufendem Protocoltest.

**Akzeptanzkriterium:** Storage-/Serialausfall betrifft nur den Sink und seine Drops; kein RX-Overflow, ACK-Timeout oder Runnerwait entsteht.

### TODO V2-059 – Golden-Replay durch die komplette Datenpipeline verifizieren

**Ziel:** Von validiertem Frame bis Snapshot/Capture einen stabilen fachlichen End-to-End-Nachweis schaffen.

**Voraussetzungen:** V2-049 bis V2-058.

**Konkrete Arbeit:** ByteReplay liest kanonische Captures und speist Production-Frame-/Processing-Komponenten. Golden-Ausgabe für Identifikation, Gruppen 000–004, Messwerte, Validität, Snapshots und Raw-Records festlegen. Kein Alt-`SimulatedLink` verwenden.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_golden_replay_pipeline`; zwei identische Läufe müssen byteidentische strukturierte Ergebnisse liefern. ASan/UBSan und vollständige Hostsuite ausführen.

**Akzeptanzkriterium:** Alle bekannten Captures werden ohne Hardware deterministisch ausgewertet; unbekannte Daten bleiben sichtbar Raw/Unknown.

## Phase 5 – UX/UI und Gesamtverifikation

### TODO V2-060 – ApplicationController und UiIntent-Grenze implementieren

**Ziel:** UI-Intents sicher in optionale Commands übersetzen, ohne Protocol-/Domaininternas zu exponieren.

**Voraussetzungen:** V2-038, V2-056, V2-057.

**Konkrete Arbeit:** Wertbasierte `UiIntent`, Request-ID, Command-Ablehnung `Busy/QueueFull` und Statusbestätigung implementieren. Controller besitzt keine PHY/Core-Referenz; Buttonzustand gilt nicht als Sessionbestätigung.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_application_controller`: volle Commandqueue, duplicate Request-ID, verspätete Bestätigung, Planwechsel, UI verschwindet während pending Command.

**Akzeptanzkriterium:** UI kann Protocol ausschließlich über die bounded Commandqueue beeinflussen; fehlende UI stoppt den Dialog nicht.

### TODO V2-061 – UI-Shell für Status, Fehler und Navigation implementieren **[TARGET]**

**Ziel:** Display/Touch erst jetzt anbinden und ausschließlich Snapshot/Status konsumieren.

**Voraussetzungen:** V2-056, V2-057, V2-060.

**Konkrete Arbeit:** `M5UiApp`/`UiState` mit Tabnavigation, Connected/Stale/Disconnected, letztem Fault und Dropindikatoren implementieren. M5GFX/Sprites bleiben UI-owned; kein Include aus Protocol/Platform-USB/Decoderinternas.

**Tests/Verifikation:** HOST-Viewmodeltest `v2_ui_state`; **[TARGET]** `ui_shell` mit Touchnavigation, Snapshotwechsel und simuliertem Disconnect. Architekturguard erneut ausführen.

**Akzeptanzkriterium:** UI zeigt Zustände korrekt aus Wertkopien und hält keine fremde Ressource; 500-ms-Renderstall beeinflusst synthetischen Protocoltest nicht.

### TODO V2-062 – Minimal-Dashboard implementieren **[TARGET]**

**Ziel:** Belegte Digifantwerte verständlich und ohne falsche Genauigkeit anzeigen.

**Voraussetzungen:** V2-061; `UI_UX_KONZEPT_MINIMAL.md` darf als Darstellungsreferenz gelesen werden, nicht als neue Architekturquelle.

**Konkrete Arbeit:** RPM, Temperaturen, Batterie, G69 Raw und relevante Last-/Lambda-/Einspritzwerte aus Snapshot rendern; Gültigkeit/Stale/Unknown sichtbar machen. Updatefrequenz begrenzen und keine Domainberechnung in Views duplizieren.

**Tests/Verifikation:** HOST-Snapshot/Viewmodel-Golden `v2_dashboard_viewmodel`; **[TARGET]** visuelle Testmatrix für Valid/Stale/Disconnected/Unknown und wiederholte langsame Renders.

**Akzeptanzkriterium:** Alle angezeigten Werte stammen ausschließlich aus Snapshot; G69 wird nicht als unbelegter Winkel ausgegeben; Protocolmetriken bleiben bei UI-Stall stabil.

### TODO V2-063 – Scope-Anzeige aus timestamped MeasurementEvents implementieren **[TARGET]**

**Ziel:** Zeitverläufe anzeigen, ohne UI-Sampling als Messquelle zu verwenden.

**Voraussetzungen:** V2-055, V2-062.

**Konkrete Arbeit:** Begrenzten UI-lokalen Scope-Ring und deterministisches Downsampling aus originalen Messtimestamps implementieren. Overwrite/Drop ist optional und gezählt; persistente Records bleiben unabhängig.

**Tests/Verifikation:** RED → GREEN → REFACTOR mit `v2_scope_downsampling`: unregelmäßige Zeitstempel, Burst, Ringwrap, UI-Stall. **[TARGET]** `scope_render_stall` mindestens 500 ms.

**Akzeptanzkriterium:** Plotzeit entspricht Messzeit; Scopeverlust beeinflusst weder Snapshotmodell noch Raw-Capture oder Protocol.

### TODO V2-064 – Vollständigen Targetbuild im Replay-/Emulatormodus verifizieren **[TARGET]**

**Ziel:** Production-Taskgraph, Processing, Logger und UI ohne Fahrzeug gemeinsam prüfen.

**Voraussetzungen:** V2-059 bis V2-063.

**Konkrete Arbeit:** Compile-time Replay-/ECU-Emulatormodus anbinden, der denselben Core/Runner/Processing/UI-Code verwendet und nur den Transportport ersetzt. Taskprioritäten, Mailboxes, Status und Faultanzeigen prüfen.

**Tests/Verifikation:** `./tools/build_target.sh`; **[TARGET]** `full_system_replay` mit mindestens zehn vollständigen Datensätzen, UI-Tabwechseln, Loggerausfall und Snapshot-Goldenhash.

**Akzeptanzkriterium:** Gesamtanwendung läuft ohne Altklassen; Replay und realer Build unterscheiden sich nur im Plattformadapter, nicht im Protocol Core.

### TODO V2-065 – Reale K409-/ECU-Funktion freigeben **[HW-K409, HW-ECU]**

**Ziel:** 5-Baud-Init, Keybytes, Identifikation und Gruppen 000–004 am echten Digifant nachweisen.

**Voraussetzungen:** V2-023, V2-028, V2-041, V2-048, V2-064; sicherer Fahrzeug-/Benchaufbau.

**Konkrete Arbeit:** Productionnahen Instrumentierungsbuild verwenden; Kaltstart, Reconnect, Identifikation, mehrere autonome Messzyklen, verweigerte Gruppe und kontrollierten Stop ausführen. Serialausgabe bleibt strukturierte Telemetrie außerhalb des Hot Paths.

**Tests/Verifikation:** **[HW-ECU]** `real_ecu_functional`: mindestens Gruppen 000–004, erwartete Titel/Counter, keine ungeklärten Resyncs. Captures mit bestehender Referenz plausibilisieren, aber nicht durch sie ersetzen.

**Akzeptanzkriterium:** Reale ECU bleibt über die definierte Testdauer verbunden; alle Pflichtgruppen liefern valide Frames und erwartete Domainwerte; jeder Fehler ist typisiert.

### TODO V2-066 – Endgültiges KwpTimingProfile mit Logic Analyzer qualifizieren **[HW-ECU, HW-LA]**

**Ziel:** Alle offenen ECU-/FTDI-Zeitannahmen durch physische End-to-End-Messungen schließen.

**Voraussetzungen:** V2-065.

**Konkrete Arbeit:** K-Line-Endflanke ECU-Byte → Startflanke inverse ACK, K-Line→Callback, callback→submit, submit→K-Line, `~KB2`, Echo/ECU-ACK, BusIdle, komplette 5-Baud-Zellen, Stop-Hold, Turnaround und Keep-Alive messen. Best/Worst-Last und Sicherheitsmarge dokumentieren; Profil versionieren.

**Tests/Verifikation:** Trace-Checker an Grenzverletzungsfixture rot; reale **[HW-LA]** Traces grün. Mindestens die in `docs/timing_budget.md` definierte Stichprobe pro Ereignistyp; End-to-End-Kriterium, nicht nur Softwarelatenz.

**Akzeptanzkriterium:** Jedes produktive Timingfenster hat physische Min/Max-Basis und Marge; kein transformiertes Submitfenster ist leer. Andernfalls keine Releasefreigabe.

### TODO V2-067 – Scheduling-, Queue- und Downstream-Stress auf Finalbuild ausführen **[TARGET, HW-ECU]**

**Ziel:** Die Entkopplung unter maximal vorgesehener Last auf dem echten System beweisen.

**Voraussetzungen:** V2-066.

**Konkrete Arbeit:** Processing 60 s anhalten, Logger dauerhaft blockieren, UI wiederholt ≥500 ms blockieren, Command-Flood, Snapshotoverwrite, volle Frame-/Persistence-/Diagnosticqueue und definierte USB-Bursts auslösen. Task-/Queue-/Latenztelemetrie erfassen.

**Tests/Verifikation:** **[HW-ECU]** `final_downstream_stress`; zusätzlich `final_queue_saturation` im kontrollierten Testmodus. Vorher/nachher ACK-End-to-End-Verteilung und Sessioncounter vergleichen.

**Akzeptanzkriterium:** Kein Downstream-Stall erzeugt RX-Overflow, ACK-Timeout oder Sessionabbruch; nur die jeweilige dokumentierte Drop-Policy greift; RX-Ingress bleibt im Lastprofil unter 50 %.

### TODO V2-068 – Hotplug-, Fault- und Soak-Abnahme ausführen **[HW-K409, HW-ECU, SOAK]**

**Ziel:** Langzeitstabilität, Ressourcenreserve und Recovery des finalen Systems nachweisen.

**Voraussetzungen:** V2-067.

**Konkrete Arbeit:** 100 Hot-Unplug/Reconnect-Zyklen in verschiedenen Byte-/Controlzuständen, 24-h beschleunigten Hostsoak und 8-h Hardware-/ECU-Soak durchführen. Faultinjection für Echo/ACK/Counter/Overflow/Completionverlust wiederholen; Heap-, DMA-, PSRAM-, Stack- und Telemetrietrends auswerten.

**Tests/Verifikation:** `v2_host_soak_24h`; **[HW-K409]** `hotplug_100`; **[HW-ECU]** `ecu_soak_8h`. Vollständige maschinenlesbare Zusammenfassungen und Artefakthashes archivieren.

**Akzeptanzkriterium:** Kein Heaptrend, sinkende Stackreserve, UAF, Hang oder unerklärter Resync; alle injizierten Fehler folgen der Faultmatrix und alle 100 Reconnects sind sauber generationgetrennt.

### TODO V2-069 – Definition of Done und Implementierungsfreeze auditieren

**Ziel:** Eindeutig feststellen, ob die Implementierung die festgeschriebene Architektur vollständig erfüllt.

**Voraussetzungen:** V2-001 bis V2-068 vollständig grün; keine `BLOCKED`-Hardwaregates.

**Konkrete Arbeit:** Jede Checkbox aus `ARCHITECTURE_V2.md` Abschnitt 17 einer Test-ID und Evidenz zuordnen. Vollständige Host-/Sanitizer-/Target-Suites, Architekturguard und frischen Productionbuild ausführen. Abhängigkeitsgraph, Queuekonfiguration, Timingprofil, Telemetriedictionary, Referenzledger und bekannte Restrisiken prüfen.

**Tests/Verifikation:** `./tools/check_architecture.sh`; alle CMake-Presets und CTest-Suites; `./tools/build_target.sh`; maschinenlesbarer DoD-Checker `./tools/check_dod.sh`. `git diff --check` und Suche nach Altklassen/Hot-Path-Verboten.

**Akzeptanzkriterium:** Jede DoD-Zeile besitzt grüne, tatsächlich ausgeführte Evidenz; kein Hardwaretest ist durch Mock ersetzt; keine offene kritische Annahme bleibt. Erst dann wird der Implementierungsstand für Releaseplanung eingefroren.

## 4. Kritische Traceability-Matrix

| Architekturvertrag | Primäre Implementierungs-TODOs | Zwingende Abschlussnachweise |
|---|---|---|
| genau eine semantische TX-/Control-Operation | 016–019, 024, 030 | `v2_transport_op_token`, reale korrelierte Completion, `max_semantic_in_flight == 1` |
| stale Completion ohne Zustandswirkung | 016, 019, 030, 039 | alte Turn-/Session-/Generationevents in Faultmatrix |
| physische Quiescence vor Reuse | 019, 020, 039 | Cancel/Drain, spätes physisches Wirken, Hot-Unplug |
| Core-owned Deadlines/`not_before` | 007, 012, 021, 024, 025, 035 | Randtests, Publication-Cutoff, Sub-Tick-Wake |
| korrekte 5-Baud-Controlsemantik | 018, 023, 026–029, 066 | reale K-Line-Flanken und vollständiger Stop-Hold |
| Echo-/ACK-Wire-Reihenfolge | 029–032, 040, 041 | Completion-Reorder, Echo vor ECU-Byte, Deferred-RX-Slot |
| SPSC und poisoned Overflow | 010–013, 015, 045 | TSan plus Target Mid-callback-Reopen/Overflow |
| autonome Messplanung | 037, 038, 047 | kein Parser/UI gelinkt; fehlende Commands und Command-Flood |
| kein Downstream-Backpressure | 042–048, 055–058, 067 | 60-s-Stall, Loggerausfall, UI-Stall, reale ACK-Latenzen |
| gleicher Core auf Host und Target | 008, 024, 040, 044, 064 | Link-/Dependencyguard und identische Corequellen |

## 5. Abschließende Selbstprüfung dieses Plans

- **Beginnt die Ausführung mit dem verlangten Unterordner?** Ja, V2-001 legt als ersten Schritt `M5Tab5_Digifant_Analyzer/` an.
- **Ist die Reihenfolge bottom-up?** Ja. UI beginnt erst nach realem Transport, vollständigem KWP-Core, Queueentkopplung und Domainpipeline.
- **Sind kritische Verträge klein genug geschnitten?** Ja. Token, TX-Lane, Composite-Control, Quiescence, Hot-Unplug, Wake, 5-Baud, Echo/ACK, Deferred-RX, SPSC-Poison, Cutoff und autonomer Plan besitzen eigene TODOs und Tests.
- **Kann nach jedem TODO objektiv entschieden werden?** Ja. Jeder Schritt nennt Voraussetzungen, konkrete Arbeit, ausführbare Testnamen/Kommandos und ein binäres Akzeptanzkriterium.
- **Können Hardwareannahmen versehentlich durch Mocks freigegeben werden?** Nein. Alle entsprechenden TODOs sind als Hardware-Gates markiert und verlangen reale Trace-/Targetartefakte.
- **Bleibt die V2-Architektur verbindlich?** Ja. Abweichungen führen zum Stop/Eskalation, nicht zu lokalem Redesign.
- **Kann ein anderer Agent die TODOs selbstständig nacheinander ausführen?** Ja, sofern die markierte Hardware verfügbar ist. Fehlt sie, ist der exakte Blocker über das nächste Hardware-Gate eindeutig bestimmt; alle Softwaretests davor und danach besitzen stabile Runner und benannte Akzeptanzkriterien.
