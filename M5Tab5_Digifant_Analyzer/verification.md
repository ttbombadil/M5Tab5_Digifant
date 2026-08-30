# Verification

## Aktueller Statusindex (2026-08-25)

| Bereich | Status | Primärer Nachweis |
|---|---|---|
| V2-001 bis V2-008 | PASS, historische optionale Timing-Evidenz separat offen | jeweilige Abschnitte unten |
| V2-009 bis V2-015 | PASS | reale ECU-/Entkopplungs-Gates, Pipelinechecks |
| V2-016 IMU-Port | PASS | 25-Hz-Targetnachweis im V2-017/019-Verlauf |
| V2-017 Logger-Merge | PASS | Host-/Target-/ECU-/IMU-Lauf |
| V2-018 DLOG V2 | PASS | reale SD-Datei und Host-Rücklesung |
| V2-019 gemeinsame ECU-/IMU-/SD-Abnahme | PASS | finaler Target-/Hostnachweis am Ende |
| optionale Logic-Analyzer-Evidenz | PENDING-OPTIONAL-HARDWARE-EVIDENCE | bewusst nicht durch Software ersetzt |

Die folgenden Einträge bleiben eine unveränderte chronologische Evidenzspur.
Bei älteren Zwischenständen sind frühere BLOCKED-/PARTIAL-Status korrekt als
Historie zu lesen; für den aktuellen Gesamtstatus gilt ausschließlich diese
Matrix und der jeweils spätere Abschlussnachweis.

## V2-001 – Tab5-Minimalprogramm

Datum: 2026-08-20

- RED ausgeführt: `arduino-cli compile --fqbn esp32:esp32:not_a_tab5 M5Tab5_Digifant_Analyzer`
  schlug erwartungsgemäß mit „Invalid FQBN“ fehl.
- GREEN ausgeführt: `arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer`
  erfolgreich; Sketch 521804 Bytes, globale Variablen 27184 Bytes.
- Board/FQBN: M5Stack Tab5 / `esp32:esp32:m5stack_tab5`, Arduino-ESP32 3.3.10.
- Upload auf `<serial-port>`: erfolgreich; ESP32-P4 erkannt und Flashdaten
  verifiziert.
- Serialmonitor via `screen` auf 115200 Baud: erfolgreich; empfangen wurden
  `M5Tab5_Digifant_Analyzer: smoke test started` und wiederholte
  `M5Tab5_Digifant_Analyzer: smoke test running`-Meldungen.
- Displaybeobachtung: durch Nutzer bestätigt; der Tab5 zeigt blauen Hintergrund
  und weiße Schrift wie vorgesehen.

Status: `PASS` – V2-001 ist vollständig abgeschlossen. Kein K409-/FTDI-TODO wurde
begonnen.

## V2-002 – K409-/FTDI-Erkennung

Datum: 2026-08-20

- HOST: `c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror
  tests/k409_device_filter_test.cpp -o /tmp/k409_device_filter_test` und
  anschließender Testlauf: PASS. VID/PID-Filter und getrennte Generationen
  für zwei K409-Verbindungen sind geprüft.
- TARGET: Sketch mit EspUsbHost 2.7.8 und Arduino-ESP32 3.3.10 kompiliert und
  auf `<serial-port>` geflasht: PASS.
- TARGET Serial: `M5Tab5_Digifant_Analyzer: smoke test started` und laufende
  Statusmeldungen empfangen: PASS.
- HW-K409: kein `K409_CONNECTED`-Ereignis; am Tab5-USB-Host wurde während der
  Prüfung kein K409 (`VID 0x0403`, `PID 0x6001`) angeschlossen/erkannt.

Status: `BLOCKED-HARDWARE` – V2-002 wartet auf einen angeschlossenen AutoDia
K409. V2-003 wurde nicht begonnen.

Nach erneutem Target-Reset mit angeschlossenem K409 wurde automatisch über
Serial empfangen:

```text
K409_CONNECTED vid=0x0403 pid=0x6001 address=1 generation=1
```

Der Disconnect-/Reconnect-Nachweis wurde anschließend über die dauerhaft
geöffnete Serial-Konsole erbracht:

```text
K409_DISCONNECTED address=2
K409_CONNECTED vid=0x0403 pid=0x6001 address=3 generation=3
```

Während des Abziehens meldete EspUsbHost zusätzlich einmal
`EP command error: ESP_ERR_INVALID_STATE`; der anschließende K409-Reconnect
war erfolgreich und erhielt eine neue Generation.

Status: `PASS` – VID/PID-Filter, Disconnect und generationgetrennter Reconnect
sind auf dem realen Tab5/K409 nachgewiesen. V2-003 kann beginnen.

## V2-003 – Untersuchung synchroner Adapter **[HW-K409]**

Testsketch: `M5Tab5_Digifant_Analyzer.ino`, EspUsbHost 2.7.8, Arduino-ESP32
3.3.10, echter Tab5 + K409/FT232R. Alle Zeiten sind `micros()`-Zeitstempel auf
dem Target.

Beobachtete Messwerte:

```text
CONTROL baud_1200       duration_us=207  accepted=1
CONTROL line_8n1        duration_us=306  accepted=1
TX_DIRECT               duration_us=104  accepted=1
TX_QUEUE submit         duration_us=63   accepted=1
TX_QUEUE flush(25 ms)   duration_us=110  result=1
TX_TIMEOUT_PROBE flush(1 ms) result=1
DISCONNECT TX submit    length=64 duration_us=22 accepted=1
Disconnect               beobachtet während des ungeklärten TX
post-disconnect flush   duration_us=2 result=0
Late-Effect-Fenster      3 s, kein Echo und keine verspätete RX-Wirkung
```

Ein lokales Echo wurde während der direkten und gequeueten TX-Operationen nicht
beobachtet. Der 1-ms-Flush lief erfolgreich zurück; ein natürlicher Timeout
konnte damit nicht erzwungen werden. Beim Disconnect während des 64-Byte-TX
lieferte der anschließende Flush sofort `false`; im dreisekündigen Fenster trat
keine verspätete RX-Wirkung auf.

Die Messung bestätigt damit begrenzte API-Rückkehr- und Disconnect-Reaktionen,
aber keinen physisch korrelierten Leitungsabschluss. `sendSerial()` und
`serialWriteAsync()` liefern nur `bool`; `serialWriteFlush()` und
`serialWriteStats()` liefern Pool-/aggregierte Zustände. Ein
anwendungsseitig korrelierbarer Terminal-Callback mit Operationstoken sowie
eine beweisbare Completion-vor-Wire-/Cancel-/Drain-Quiescence-Semantik fehlen.

Option 2 ist daher nicht tragfähig. Ein gepinnter EspUsbHost-Fork/Port ist für
V2-003 erforderlich. Status: `BLOCKED-ARCHITECTURE`. Kein KWP1281 wurde
begonnen.

## V2-003 – gepinnter EspUsbHost-Port/Fork

### Pin und Patchumfang

Die unveränderte 2.7.8-Basis ist unter
`third_party/EspUsbHost-2.7.8/` mit Lizenz, Pin und Vorpatch-Hashes
dokumentiert; die gebaute und gepatchte Forkquelle liegt ausschließlich unter
`src/esp_usb_host_fork/`. Der frühere byteidentische `third_party/.../src`-
Doppelbaum wurde in R4 entfernt.

Der Fork ergänzt ausschließlich:

- `EspUsbHostOperationToken` und `EspUsbHostOperationCompletion`;
- tokenisierte Serial-OUT-Submission mit genau einer aktiven tokenisierten
  Slotoperation;
- Completionstatus `Completed`, `Failed`, `Canceled` und `OutcomeUnknown`;
- bootlebende Retired-Transfer-Tabelle für Disconnect-Callbacks nach dem
  Zurücksetzen des DeviceState;
- tokenisierten FTDI-Control-Einstieg, dessen Abschluss erst nach dem
  synchronen Control-Transfer gemeldet wird.

Die Forkquelle umfasst ungefähr 0,75 MiB; ein allgemeines Bibliotheks-Redesign
wurde nicht vorgenommen.

### Tests

- `kwp_measurement_session_lifecycle_test`: PASS. Produktive Session-
  Tokenkorrelation, stale Completion und Disconnect-Quieszenz.
- `k409_device_filter_test`: PASS.
- Targetcompile mit lokalem Fork und Arduino-ESP32 3.3.10: PASS.
- Target-Seriallauf mit echtem K409: Control- und TX-Completions wurden
  korreliert beobachtet:

```text
COMPLETION generation=1 operation=1 kind=2 status=0 address=1
COMPLETION generation=1 operation=2 kind=1 status=0 address=1
```

- Der Disconnect-Testpfad wurde mit einem festen RAM-Ereignislog am Target
  erneut ausgeführt. Die Aufzeichnung wurde erst nach `Test beendet` über den
  seriellen Port gelesen; der Port war während der Messung nicht geöffnet.
  Der aktive TX-Test verwendete 60000 Bytes. Auszug des wiederholten Reports:

```text
LOG index=5 event=3 generation=1 operation=3 kind=1 status=0 address=1 at_us=10446603
LOG index=6 event=4 generation=1 operation=3 kind=1 status=3 address=1 at_us=19146463
LOG index=7 event=2 generation=1 operation=0 kind=0 status=0 address=1 at_us=19146633
LOG index=8 event=1 generation=2 operation=0 kind=0 status=0 address=2 at_us=43156016
LOG index=9 event=3 generation=2 operation=4 kind=1 status=0 address=2 at_us=43164381
LOG index=10 event=4 generation=2 operation=4 kind=1 status=0 address=2 at_us=43164506
REPORT generations=2 completions=4 echoes=0 state=1
```

`status=3` ist `OutcomeUnknown`. Die tokenisierte Operation 3 erhielt genau
einen Terminaleintrag, danach folgte der Disconnect; im anschließenden
Late-Completion-Fenster kam kein zweiter Terminaleintrag und keine RX-Wirkung.
Die Wiederverbindung erzeugte Generation 2 (neue USB-Adresse 2); Operation 4
der neuen Generation wurde erfolgreich abgeschlossen. Damit ist die alte
Generation vor der neuen Submission retired/quiescent isoliert. Der reale
Nachweis für Disconnect, Retirement, Late-Isolation und Reconnect ist damit
erbracht.

### Status und offene Risiken

Der Fork stellt die vorgesehene Token-/Completion-/Retirement-Struktur bereit;
alte Transfers werden bei Disconnect in eine bootlebende Retired-Tabelle
überführt und dürfen erst nach ihrem Terminalcallback freigegeben werden. Bei
voller Retired-Tabelle wird der Ausgang explizit `OutcomeUnknown` statt
Quiescence zu behaupten.

Der reale Targetnachweis für einen `OutcomeUnknown`-Terminalstatus nach
Disconnect, genau ein terminales Ergebnis, fehlende Late-Wirkung und eine
erfolgreiche neue Generation liegt im RAM-Report vor. Der Fork liefert für
Operation 3 genau einen Completion-Eintrag; Operation 4 der Generation 2 wird
erst danach akzeptiert und abgeschlossen. Eine natürliche verspätete zweite
Completion wurde nicht beobachtet; die Host-Lifecycle-Tests decken deren
stale/duplicate-Isolation ab.

Status: `READY-FOR-KWP`. Kein KWP1281 wurde begonnen.

## V2-003 – zuverlässige Byte-Transportbasis (RX-Ingress-Teil)

Implementiert wurden `src/rx_ingress_ring.h` und der gezielte Hosttest
`tests/rx_ingress_poison_test.cpp`. Der Ring besitzt 512 feste Slots (511
nutzbar), Release-/Acquire-Veröffentlichung, sticky Overflow, drop-only nach
Poison und einen expliziten Reset erst nach Producer-Quieszenz. Epochen- und
Transportsequenzwerte werden als Wertrecords erhalten.

Ausgeführte Tests:

```text
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ... rx_ingress_poison_test.cpp: PASS
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ... kwp_measurement_session_lifecycle_test.cpp: PASS
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ... k409_device_filter_test.cpp: PASS
c++ -std=c++20 -fsanitize=address,undefined ... rx_ingress_poison_test.cpp: PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer: PASS
```

Der reale Targettest wurde auf Tab5 + K409 ausgeführt. Der Sketch führte den
angeschlossenen EspUsbHost-Serial-Callback aus, erzeugte dabei 64 reale
Echo-Records und führte zusätzlich einen festen Callback-Batch mit
Ringfüllung/Poison aus:

```text
RX_TARGET_PROBE generation=1 address=1 poisoned=1 dropped=1 reset=1 old_epoch=1 new_epoch=2 isolated=1 real_batches=2
RX_TARGET_REOPEN batch_started=1 reset_active_before=0 reset_active_after=0 reset_after_callback=1
```

Damit sind Ringfüllung, sticky Poison, Reset erst nach Producer-Quieszenz,
Epochen-/Generationsisolation und Mid-callback-Reopen auf dem Target belegt.
Der nachfolgende reale Disconnect-/Retirement-Lauf blieb ebenfalls grün:

```text
LOG index=5 event=3 generation=1 operation=3 kind=1 status=0 address=1 at_us=4263519
LOG index=6 event=4 generation=1 operation=3 kind=1 status=3 address=1 at_us=7496283
LOG index=7 event=2 generation=1 operation=0 kind=0 status=0 address=1 at_us=7496469
LOG index=8 event=1 generation=2 operation=0 kind=0 status=0 address=2 at_us=17053015
LOG index=10 event=4 generation=2 operation=4 kind=1 status=0 address=2 at_us=17062474
REPORT generations=2 completions=4 echoes=64 state=1
```

Die erforderliche physische Logic-Analyzer-Messung wird erst für V2-004
(5-Baud/K-Line) benötigt. V2-003 ist damit vollständig abgeschlossen.

Status: `PASS` – V2-003 abgeschlossen; nächster Schritt ist V2-004.

## V2-004 – KWP1281-Core und 5-Baud-Init (Softwareteil)

Die Known-Good-Referenz `M5Tab5_Digifant_Proto/EcuInitTester.cpp` wurde für
KWP-Werte und Verhalten ausgewertet. Übernommen wurden als normative
Referenzdaten 2,6 s Busruhe, 5 Baud mit 200 ms je Zelle, Adresse `0x01`,
Sync `0x55`, 1200 Baud für die Datenphase, 8N1, 1-ms-Latency-Timer,
`~KB2`-Antwortfenster 25–40 ms, lokale Echo-/inverse-ACK-Reihenfolge und die
KWP-Blockgrenzen. Die blockierenden `delay()`-/Polling-/Loggingstrukturen des
Prototyps wurden nicht übernommen.

Neu erstellt wurden der plattformfreie `src/kwp1281_core.h` und der gezielte
Hosttest `tests/kwp1281_core_test.cpp`. Der Core modelliert deterministisch:

- absolute Bus-Idle- und 5-Baud-Zeitpunkte mit Start-, Daten- und Stopbit;
- tokenisierte Baud-/Break-/`~KB2`-Aktionen;
- Completion-Korrelation und Fehlerstatus;
- Sync-/Keybyte-Erkennung und Echoabschluss des `~KB2`-Turns;
- feste Aktionsspeicher ohne Heap oder Plattformheader.

Ausgeführte Tests:

```text
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ... kwp1281_core_test.cpp: PASS
c++ -std=c++20 -fsanitize=address,undefined ... kwp1281_core_test.cpp: PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer: PASS
```

Die endgültige 5-Baud-/K-Line-Freigabe bleibt `PENDING-HARDWARE-EVIDENCE`,
weil die physische Logic-Analyzer-Messung fehlt. Ein realer ECU-End-to-End-
Test kann erst mit angeschlossener Digifant-ECU ausgeführt werden; bis dahin
werden keine realen ECU-Ergebnisse behauptet. V2-005 bleibt deshalb von diesem
V2-004-Hardware-/Integrationsnachweis abhängig.

## V2-003 – Known-Good-Referenzprüfung

Die bestehende Anwendung wurde ausschließlich gelesen. Geprüfte Referenzdateien
(SHA-256):

- `M5Tab5_Digifant_Proto/UsbCdcLink.cpp` – `aaa9b1f429ae1ac68aebdee1b65d56c293720ce58d17861a19eb37d8db2627dd`
- `M5Tab5_Digifant_Proto/EcuInitTester.cpp` – `95b2bbd3363d857861c865b158688189872993fe7292ae06cca996db713e27bc`
- `M5Tab5_Digifant_Proto/ReplayData.h` – `f7f1e6011b63729581765f8af5d0ecd176defde432798c4da0af6348e3fac2aa`
- `M5Tab5_Digifant_Proto/SerialLink.h` – `06c32de9c211e0e32e3c9acbfb6865eab6ebd5f4b6492b4cbb5dd148cec02f03`

Wiederverwendbare, fachlich belegte Fakten sind K409 VID/PID `0x0403/0x6001`,
FTDI-Latency-Timer 1 ms, 8N1, Break-Werte `0x4000`/`0x0008`, 2,6 s Busruhe,
200 ms pro 5-Baud-Zelle, KWP-Blockformat mit Terminator `0x03`, inverse
Byte-ACKs sowie die vorhandenen Replay-Frames. Diese Fakten ändern keine
V2-Ownership-, Deadline- oder Queueverträge.

Nicht übernehmbar für V2-003 sind der blockierende Pollingpfad
`available()/read()`, direkte `write()`-Aufrufe ohne Token, `delay()` im
Protokollpfad, synchrone FTDI-Control-Aufrufe ohne korrelierte Completion sowie
Logging/UI-Arbeit im selben Ablauf. Der Prototyp liefert keinen Nachweis für
SPSC-Acquire/Release, Poison-/Reset-Handshake oder Callback-Reopen; diese
Nachweise bleiben V2-eigene Targettests.

Ein Logic Analyzer ist für den V2-003-SPSC-/Poison-/Completion-Lebensdauertest
nicht erforderlich, weil dort noch kein ECU-Leitungstiming freigegeben wird.
Die physische Timingmessung wird erst für V2-004 (5-Baud/K-Line) zwingend. Für
V2-003 verbleibt als unmittelbar fehlender Nachweis ausschließlich der reale
Targettest des RX-Callbacks mit voller/poisonender Epoche, Mid-callback-Reopen,
Generationstrennung und Retirement. Unabhängige Hosttests und die
Referenz-/Capture-Aufbereitung können danach fortgesetzt werden; KWP1281 bleibt
bis zum erfolgreichen V2-003-Targettest gesperrt.

## V2-006 – Pufferung, Capture und Logging (Hostteil)

V2-005 ist weiterhin ECU-abhängig. Der davon unabhängige V2-006-Teil wurde
implementiert:

- `src/validated_frame_queue.h`: feste by-value-Queue mit 32 nutzbaren
  Envelopes, `trySend`/`tryReceive`, drop-newest, verbrauchter `rxSequence`
  auch bei Full und High-Watermark;
- `src/raw_capture_queue.h`: feste Persistence-Record-Queue mit 128 nutzbaren
  Records, Dropzähler und Capture-Metadaten für Generation, Ingress-Epoche,
  Dropstand und Formatversion;
- `tests/frame_capture_queue_test.cpp`: FIFO, Full-Policy, sichtbare
  Sequenzlücke, Wertkopie und Capture-Metadaten.

Ausgeführte Tests:

```text
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ... frame_capture_queue_test.cpp: PASS
c++ -std=c++20 -fsanitize=address,undefined ... frame_capture_queue_test.cpp: PASS
kwp1281_core_test, rx_ingress_poison_test: PASS (Regression)
```

V2-006 benötigt weder ECU noch Logic Analyzer. Die Target-Kapazitäts- und
High-Watermark-Messung bleibt für die spätere Targetintegration offen und wird
nicht als Hardwarefreigabe ausgegeben. Status: `PASS` für den Hostteil.

## V2-007 – Digifant-Parser und -Decodierung (Hostteil)

Die Referenzframes aus `M5Tab5_Digifant_Proto/ReplayData.h` und die Formeln aus
`EcuInitTester.cpp` wurden als Known-Good-Verhaltensquelle verwendet. Neu
implementiert wurden:

- `src/kwp_application_parser.h`: pointerfreier KWP-Frameparser mit exakter
  Längen-/Terminatorprüfung und typisierten Titeln;
- `src/digifant_decoder.h`: Gruppe-000-Rohmodell/RPM-Schätzung und belegte
  Formeln `0x8B`, `0x8C`, `0x85`, `0x88`, `0x89` mit Tabelleninterpolation;
- `tests/parser_decoder_test.cpp`: Goldenframe, ungültiger Terminator,
  Gruppe-000-RPM und Formelgrenzen.

Ausgeführte Tests:

```text
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ... parser_decoder_test.cpp: PASS
c++ -std=c++20 -fsanitize=address,undefined ... parser_decoder_test.cpp: PASS
frame_capture_queue_test, kwp1281_core_test: PASS (Regression)
```

Der Hostteil ist hardwareunabhängig und grün. ECU-End-to-End-Daten bleiben
`PENDING-HARDWARE-EVIDENCE` aus V2-004; unbekannte Titel/Formeln werden nicht
als physikalische Werte ausgegeben.

## V2-004 – Empfangspfad und Byte-Engine (Softwareteil)

Die Referenz empfing Bytes nicht per eigener Callback-Queue, sondern über
`EspUsbHostCdcSerial::available()/read()` im 2-ms-Hauptloop. Während eines
KWP-Frames wurden Display-Updates bewusst ausgesetzt; dadurch blieb das
20–35-ms-ACK-Fenster erreichbar. Der neue Pfad übernimmt nur diese bewährte
Wire-Reihenfolge: eigenes Echo, danach inverse ECU-Quittung, bei ECU-Frames
Längen-/Datenbyte-ACKs und ungequittetes Terminatorbyte. Die Bytes kommen in V2
jedoch ausschließlich aus dem festen RX-Ingress; UI und USB werden nicht von
der Engine aufgerufen.

Neu erstellt wurden `src/kwp_byte_engine.h` und
`tests/kwp_byte_engine_test.cpp`. Der Test prüft die produktiv verwendete
Byte-Engine direkt: Host-Echo/ACK, Completion vor Echo, ECU-Frameaufbau mit
inversen ACKs, Terminatorprüfung und Echo-Mismatch-Fault. Der frühere
`KwpReceiveService`-Wrapper war kein Runtimepfad und wurde in R9 entfernt.
Warnings-as-errors und ASan/UBSan liefen grün.

Der konkrete Target-ECU-Runner ist inzwischen Bestandteil des geflashten
Low-Level-Builds. Die endgültige V2-004-Freigabe (insbesondere Logic-Analyzer-
Timing und vollständiger ECU-End-to-End-Nachweis) bleibt davon getrennt und
wird nicht vorweggenommen.

Der frühere `KwpRunnerModel` war ausschließlich ein Test-Schatten und wurde in
R9 entfernt. Die entsprechenden Generations- und Completionverträge prüfen
`kwp_measurement_session_lifecycle_test.cpp` und
`kwp_protocol_core_token_test.cpp` direkt gegen die produktiven Komponenten.

## V2-004 – erster integrierter Target-KWP-Probe

Der KWP-Core wurde in den Target-Sketch eingebunden und mit dem lokalen
EspUsbHost-Fork geflasht (FQBN `esp32:esp32:m5stack_tab5`, Upload auf
`<serial-port>`: PASS). Der Probe führte Busruhe, tokenisierte
Baud-/Break-Aktionen und die absolute 5-Baud-Sequenz aus. Der reale Targetlauf
mit angeschlossenem K409/FTDI ergab:

```text
KWP_INIT_PROBE state=3 generation=1 rx_bytes=0 key_events=0
KWP_TARGET_RESULT=NO_HANDSHAKE
```

`state=3` entspricht `WaitSync`: Es wurde kein ECU-Syncbyte `0x55` empfangen.
Damit ist kein ECU-End-to-End-Erfolg behauptet. V2-004 bleibt wegen fehlender
ECU-Antwort und der offenen Logic-Analyzer-Messung `SOFTWARE-PARTIAL` /
`PENDING-HARDWARE-EVIDENCE`.

Ein zweiter Lauf mit der Known-Good-FTDI-Vorinitialisierung (Latency 1 ms,
DTR/RTS aus) ergab identisch:

```text
KWP_INIT_PROBE state=3 generation=1 rx_bytes=0 key_events=0
KWP_TARGET_RESULT=NO_HANDSHAKE
```

Die tokenisierten Control-/Break-Aktionen wurden angenommen und korreliert;
ein ECU-Sync wurde dennoch nicht empfangen. Ursache bleibt ohne
Logic-Analyzer nicht zwischen K-Line-/ECU-Antwortlosigkeit und physischer
Break-/Timingabweichung unterscheidbar.

Nach Korrektur der FTDI-Modem-Control-Werte auf die Referenzwerte (`0x01`,
`wValue=0x0100/0x0200`) und Einführung eines kleinen Deadline-Scheduling-
Margins wurde der reale Targetlauf wiederholt. Der Tab5 empfing dabei die
ECU-Synchronisation und beide Keybytes:

```text
KWP_STAGE after_advance state=2
KWP_ACTION kind=0 value=1200 op=1 submit=accepted
KWP_ACTION kind=1 value=1 op=2 submit=accepted
KWP_ACTION kind=1 value=0 op=3 submit=accepted
KWP_ACTION kind=1 value=1 op=4 submit=accepted
KWP_ACTION kind=1 value=0 op=5 submit=accepted
KWP_ACTION kind=2 value=117 op=6 submit=accepted
KWP_ACTION op=6 completion=0
KWP_INIT_PROBE state=6 generation=1 rx_bytes=6 key_events=3
KWP_TARGET_RESULT=PASS
REPORT generations=1 completions=9 echoes=64 state=1
```

`state=6` ist `Active`. Die Completion-/Echo-Evidenz wurde im Core für beide
Reihenfolgen (Echo vor Completion und Completion vor Echo) ergänzt; nach der
Send-Completion wird keine zweite semantische Send-Operation erzeugt. Host-
Warnings-as-errors, ASan/UBSan und Targetcompile/Upload blieben grün. Der
Target-KWP-Probe ist damit als Software-/Targetnachweis grün; die verbleibende
V2-004-Hardwareevidenz bleibt `PENDING-HARDWARE-EVIDENCE` für die physische
Timingqualifikation und die vollständige Mess-/ECU-Freigabe.

## V2-008 – Snapshot-UI

Implementiert wurden `src/measurement_snapshot.h` und die produktiven
Snapshot-Mailboxen/Fanout-Komponenten. Snapshots werden als vollständige
Wertkopien in einer Latest-Mailbox veröffentlicht; ein langsamer Leser erhält
den neuesten konsistenten Zustand, während Zwischenstände als Overwrite gezählt
werden. Gültigkeit `Valid`, `Stale` und `Disconnected` bleibt sichtbar. Der
frühere `UiState`-Wrapper und `ui_snapshot_test.cpp` waren keine Runtimegrenze;
R9 prüft denselben Vertrag in `snapshot_runtime_boundary_test.cpp` gegen den
realen Fanout.

Hosttests einschließlich ASan/UBSan: PASS. Der Target-Smoke-Test wurde auf dem
realen Tab5 geflasht und über Serial verifiziert:

```text
UI_SNAPSHOT_TARGET valid=1 disconnected=1 rpm=1000 overwrites=1
```

V2-008 benötigt weder ECU noch Logic Analyzer und ist damit abgeschlossen.
Status: `PASS`.

## V2-005 – autonome Messgruppenplanung (Hostteil)

Die Sequenz wurde aus der Known-Good-Referenz abgeleitet und als feste,
parserfreie Planstufe umgesetzt: Identifikationsblock mit `0x09` quittieren,
Gruppe 000 mit `0x12` anfordern, Gruppen 001–004 über `0x09`-Umschaltung und
`0x29 <group>` anfordern. Eine verweigerte Gruppe wird gezählt übersprungen;
der Plan besitzt nur einen ausstehenden Wertbefehl und faultet bei nicht
entkoppeltem Command-Consumer. UI und Decoder sind nicht beteiligt.

Ausgeführte Tests:

```text
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror ... measurement_plan_test.cpp: PASS
c++ -std=c++20 -fsanitize=address,undefined ... measurement_plan_test.cpp: PASS
```

Der reale ECU-Nachweis für Identifikation, Gruppenframes, Counter und
Recovery ist noch nicht ausgeführt; V2-005 bleibt deshalb insgesamt
`PENDING-HARDWARE-EVIDENCE`.

## V2-005 – entkoppelter Byte-/Mess-Runner (Targetprobe)

`src/kwp_measurement_session.h` verbindet den festen Byte-Engine-Pfad, den
parserfreien MeasurementPlan und tokenisierte Einzelbyte-Aktionen. Der Runner
besitzt genau einen ausstehenden Transportbefehl; Completionen aus einer
anderen Generation werden gezählt und verworfen. Der Hosttest
`kwp_measurement_session_test.cpp` deckt Frame-ACK, stale Completion und
Disconnect ab; Warnings-as-errors und ASan/UBSan: PASS.
Der Test replayt zusätzlich die aufgezeichnete 64-Byte-Spur bis in den zweiten
ECU-Block; auch dieser Replay läuft unter Warnings-as-errors und ASan/UBSan
grün.

Der reale Tab5/K409-Probe bestätigte erneut den Handshake (`KWP_TARGET_RESULT=PASS`)
und empfing den vollständigen Identifikationsblock sowie den Beginn des zweiten
ECU-Blocks. Dabei wurden frühe Folgebytes vor ACK-Echos durch den festen
`pending_rx_after_echo`-Slot überbrückt. Die aufgezeichnete Spur lässt sich
hostseitig bis in den zweiten Block ohne Fault abspielen. Der Targetlauf stoppt
danach weiterhin kontrolliert in `Fault`, weil die vollständige Folge außerhalb
des begrenzten Diagnoseauszugs noch nicht korreliert ist; es wurden keine
Messwerte als valide ausgegeben. Zusätzlich wurde der feste
Target-Completionpuffer von 16 auf 64 Einträge erweitert, weil Handshake plus
Mess-ACKs mehr als 16 Terminalereignisse erzeugen.

Targetartefakte:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=2 stage=0 group=0 rx_bytes=114 stale=0
KWP_MEASUREMENT byte_fault=5 wire_tail=<114-byte target capture; first 64 bytes emitted>
KWP_MEASUREMENT_RESULT=NO_FRAME
```

Status V2-005: `SOFTWARE-PARTIAL` / `PENDING-HARDWARE-EVIDENCE`. Für den
nächsten sauberen Targetlauf ist ein ECU-Neustart (Zündung aus/ein) nötig;
dieser Nachweis wird nicht durch Replay oder Hosttests ersetzt.

### V2-005 – Regression vor dem nächsten Targetlauf

Der Hosttest `kwp_byte_engine_test` deckt nun zusätzlich den Fall ab, dass
nach einem Host-ACK-Byte bereits das Längenbyte des nächsten ECU-Frames im
poison-/pending-Slot liegt. Der Zustandsübergang führt dieses Byte nach
geschlossenem ACK-Echo in `RxLength` weiter; eine Verarbeitung in `Idle` würde
den beobachteten `UnexpectedByte`-Fault erzeugen. Der Test war zunächst rot
und ist nach der gezielten Korrektur grün.

Zusätzlich wurde der ausschließlich für den Targetprobe verwendete feste
Completion-Ledger auf 256 Einträge mit 16-Bit-Zähler begrenzt. Nach vollständig
bestätigtem Handshake wird der Ledger an einer quieszenten Grenze geleert,
damit Startup-Completions den Messlauf nicht vorzeitig überlaufen lassen.
Das erzeugt keine Hot-Path-Allokation und ändert weder die tokenisierte
Transportsemantik noch den KWP-Core-Vertrag.

Ausgeführt:

```text
kwp_byte_engine_test (Warnings-as-errors): PASS
kwp_measurement_session_test (Warnings-as-errors): PASS
alle Hosttests (Warnings-as-errors): PASS
alle Hosttests (ASan/UBSan): PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5: PASS
```

Der reale Lauf mit Zündung/ECU stand für diesen Zwischenstand noch aus; die
nachfolgende Targetregression dokumentiert die anschließende Ausführung. V2-005
bleibt wegen der noch fehlenden vollständigen Gruppenabnahme
`SOFTWARE-PARTIAL` / `PENDING-HARDWARE-EVIDENCE`.

### V2-005 – Targetregression nach Completion-/Pending-Korrektur

Mit angeschlossenem K409 und eingeschalteter Zündung wurde der Build auf dem
realen M5Stack Tab5 (`esp32:esp32:m5stack_tab5`, `<serial-port>`)
neu gestartet. Die serielle Aufzeichnung ist vollständig bis zum Ende des
5-Sekunden-Messprobes erfolgt:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=3 group=1 rx_bytes=290 stale=0
KWP_MEASUREMENT byte_fault=0 wire_tail=0FF000FFF60930CF33CC37C839C630CF36C930CF32CD34CB41BE47B820DF0303FC00FF09F6031BE401FEF60944BB49B647B849B646B941BE4E...
KWP_MEASUREMENT_RESULT=RUNNING
REPORT generations=1 completions=145 echoes=64 state=1
```

Damit ist der zuvor beobachtete `UnexpectedByte`-Fault im realen Pfad nicht
mehr reproduziert. Der Runner verarbeitet Identifikation und den Übergang zur
Gruppe 1 ohne Fault und mit korrelierten tokenisierten Completions. Der
Targetprobe endet absichtlich vor vollständiger Gruppen-000--004-Auswertung;
Decoderwerte, Counter-/Recovery-Abnahme und ein längerer autonomer Messlauf
sind daher weiterhin offen. V2-005 bleibt insgesamt
`SOFTWARE-PARTIAL` / `PENDING-HARDWARE-EVIDENCE`.

### V2-005 – 60-s autonomer Target-Messlauf

Mit demselben realen Tab5/K409/ECU-Aufbau wurde der Probe auf 60 Sekunden
erweitert. Die statischen Diagnosegrenzen (4.096 Completion-Einträge und
8.192 RX-Bytes) wurden nicht überschritten. Der gefilterte serielle Abschluss
lautet:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3470 stale=0
KWP_MEASUREMENT byte_fault=0
KWP_MEASUREMENT_RESULT=RUNNING
REPORT generations=1 completions=1735 echoes=64 state=1
```

`stage=2 group=1` bedeutet, dass die autonome Planfolge nach mehreren
Gruppenzyklen wieder in der Gruppe-1-Umschaltphase angekommen ist. Der
Transport-/Bytepfad blieb über den gesamten Lauf fehlerfrei; keine Completion
lief stale und es trat kein `UnexpectedByte` auf. Dieser Lauf belegt noch nicht
die fachliche Decoder-/Goldenwert-Abnahme und ersetzt keinen Logic-Analyzer-
Timingnachweis. V2-005 bleibt deshalb für die fachliche Endabnahme offen,
der reale Transport- und Messrunner-Nachweis ist jedoch `PASS`.

### V2-005 – Framezählung im realen 60-s-Lauf

Für die fachliche Abgrenzung wurden im Session-Core feste Framezähler ergänzt
und im Targetlauf ausgegeben. Der reale Tab5/K409/ECU-Lauf lieferte:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=1 group=0 rx_bytes=3468 stale=0
KWP_MEASUREMENT frames=82 ident=3 ack=25 headers=24 bodies=30 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0
KWP_MEASUREMENT_RESULT=RUNNING
REPORT generations=1 completions=1734 echoes=64 state=1
```

Damit sind im entkoppelten Runner reale Identifikations-, ACK-, Header- und
Body-Frames über mehrere Gruppenzyklen nachgewiesen. Die Generation blieb
konstant, stale Completions und Parser-Rejects blieben null, und der Runner
kehrte nach dem Zyklus wieder zur Gruppe-000-Anforderung zurück. Die Zähler
sind reine Werttelemetrie; sie verändern keine Deadline-, Ownership- oder
Transportentscheidung. V2-005 ist für den autonomen Messrunner damit
`PASS`; die fachliche Decoder-/Goldenwert-Abnahme aus V2-007 bleibt separat.

### V2-006 – Target-Queue-Vertrag

Die bereits hostgetesteten bounded Queues wurden zusätzlich auf dem realen
Tab5 ausgeführt. Der Test verwendet keine ECU-Daten und erzeugt keine
Protocol-Rückkopplung:

```text
QUEUE_TARGET validated_full=1 validated_gap=1 frame_drops=1 frame_high=32 capture_full=1 capture_drops=1 capture_high=128
```

Damit sind auf dem Target 32 nutzbare Validated-Frame-Slots mit
Drop-newest/High-Watermark und 128 nutzbare Persistence-Slots mit derselben
nonblocking Full-Policy nachgewiesen. Die sichtbare Sequenzlücke nach dem
Frame-Drop bleibt erhalten. V2-006 ist für den Host- und Target-Queuevertrag
`PASS`; ein realer Storage-Sink-Stall bleibt als späterer Logger-/Soaktest
separat.

### Regression nach V2-005/V2-006

```text
alle tests/*_test.cpp mit -std=c++20 -Wall -Wextra -Wpedantic -Werror: PASS
alle tests/*_test.cpp mit ASan/UBSan: PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer: PASS
git diff --check: PASS
```

Offen bleibt ausschließlich die bereits bekannte physische
Logic-Analyzer-/Timing-Evidenz für V2-004; sie wurde nicht durch diese
Target-/Hostläufe ersetzt.

## V2-007 – Digifant-Parser und -Decodierung (Golden-Abnahme)

Die Known-Good-Capture `captures/engine_running_corrected_replay.csv` wurde
unverändert aus der Referenz übernommen. Beide Dateien sind byteidentisch:

```text
Zeilen: 109 (Header plus 108 Datenframes)
SHA-256: 5421eda8655b648e7f706f80d63e07cb774b1ba6d2baeb058dddb44984a16cb0
Die Golden-Capture liegt zentral unter `captures/engine_running_corrected_replay.csv`.
Der Hosttest verwendet diese eine Quelle direkt: PASS.
```

Der gezielte RED-Test `digifant_groups_golden_test.cpp` schlug vor der
Gruppenimplementierung mit fehlendem `DecodedNumberedGroup` fehl. Nach der
minimalen Ergänzung in `src/digifant_decoder.h` prüft er die vier Zonen der
aufgezeichneten Gruppe 002 sowie den unbekannten G69-Datensatz aus Gruppe 003.
Die zusätzlich eingeführte `capture_golden_test.cpp` liest die kopierte CSV
direkt ein, validiert alle 108 Frames, findet mindestens vier Header und vier
Bodies und leitet daraus die Goldenfälle ab.

Die belegten Ergebnisse sind:

```text
Gruppe 002, Zone 3: Formel 0x85, nwb=0x18, raw=0x92 -> 13.6875 V
Gruppe 003, Zone 3 (G69): raw=0x00, nicht als Winkel interpretiert
```

Die Batterieformel und die Tabellen-/Bitmaskenformeln wurden entsprechend der
Known-Good-Referenz übernommen. Nicht belegte Formeln bleiben als
`supported=false` mit erhaltenem Rohwert sichtbar; sie werden nicht still als
Null oder physikalische Einheit ausgegeben.

Ausgeführt:

```text
alle tests/*_test.cpp mit -std=c++20 -Wall -Wextra -Wpedantic -Werror: PASS
alle tests/*_test.cpp mit ASan/UBSan: PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer: PASS
git diff --check: PASS
```

Die reale ECU-Framezählung aus dem vorangegangenen Targetlauf bleibt die
Hardware-Provenienz (`ident=3`, `headers=24`, `bodies=30`, `rejected=0`). Eine
Logic-Analyzer-Messung der Leitungstimings wurde nicht behauptet und bleibt für
V2-004 `PENDING-HARDWARE-EVIDENCE`.

Status V2-007: `PASS` (Host-/Golden-Abnahme). Der nächste planmäßige Schritt ist
V2-008; das Timing-Gate V2-004 bleibt unabhängig offen.

## Gesamtabschluss – Funktionaler Stand

Die Planpunkte V2-001 bis V2-008 sind für die verfügbare Plattform funktional
umgesetzt. Der reale End-to-End-Lauf mit M5Stack Tab5, K409/FT232R und Digifant-
ECU ist als Targetartefakt dokumentiert: K409-Generation, tokenisierte
Completions, Identifikation, Gruppen-Header und Gruppen-Bodies liefen über den
autonomen Messrunner; im 60-s-Lauf wurden `ident=3`, `headers=24`, `bodies=30`
und `rejected=0` gemeldet. Queuefüllung/Poison, Snapshot-Mailbox und Reconnect-
Retirement-Isolation sind ebenfalls auf dem realen Target nachgewiesen.

Der abschließende lokale Regressionlauf ergab:

```text
13 Hosttests mit -Wall -Wextra -Wpedantic -Werror: PASS
13 Hosttests mit ASan/UBSan: PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer: PASS
git diff --check: PASS
```

Die physische Logic-Analyzer-Aufzeichnung der K-Line-Flanken, Zellbreiten und
Control-Completion-zu-Leitung-Latenzen konnte mangels Messgerät nicht erstellt
werden. Entsprechend der aktuellen Freigabeentscheidung wird diese Lücke als
`PENDING-OPTIONAL-HARDWARE-EVIDENCE` geführt. Sie wurde weder durch Simulation
noch durch den ECU-Lauf ersetzt; die im Architekturtext dafür vorgesehenen
Messwerte bleiben als offene Verifikationsdaten gekennzeichnet.

Finaler Funktionsstatus: `FUNCTIONALLY-COMPLETE` für Tab5 + K409 + Digifant auf
Basis der vorhandenen Host-, Target- und ECU-Nachweise. Aus technischer Sicht
besteht kein weiterer Implementierungsblocker. Offen bleibt ausschließlich die
optionale physische Timingmessung aus V2-004. Frühere Zwischenstandszeilen mit
`PENDING-HARDWARE-EVIDENCE` sind historische Einträge und werden für den
aktuellen Gesamtstatus durch `PENDING-OPTIONAL-HARDWARE-EVIDENCE` ersetzt.

## Serieller Motordaten-Diagnosepfad – Targetabnahme

Für die serielle Diagnose wurde kein zweiter RX-SPSC-Consumer eingeführt. Der
einzige KWP-Consumer (`KwpMeasurementSession`) erzeugt nach erfolgreicher
Framevalidierung einen vollständigen `RawFrameRecord` und veröffentlicht ihn
nonblocking in der vorhandenen, festen `PersistenceQueue` (128 Nutzslots).
Ein separater Logger-Task konsumiert diese Queue, hält einen festen Headercache
für Gruppen 001–004 und ruft die vorhandene Digifant-Decodierung auf. Seine
Serialausgabe kann bei voller/gestörter Konsole nur Queue-Drops erzeugen; sie
wartet nicht im RX-, ACK- oder MeasurementRunner-Pfad.

Targettest: realer M5Stack Tab5 + K409/FT232R + Digifant-ECU, FQBN
`esp32:esp32:m5stack_tab5`, Port `<serial-port>`. Upload und serieller
Lauf wurden nach Schließen des vorherigen Monitors erfolgreich ausgeführt.
Repräsentative Ausgabe:

```text
KWP_FRAME seq=6 generation=1 session=1 group=001 title=0x02 counter=0x06 size=50 raw=3106028B1A11...
KWP_DECODE group=001 zone=1 formula=0x8B raw=255 value=29.2500 valid=1
KWP_DECODE group=001 zone=2 formula=0x8C raw=24 value=50.0000 valid=1
KWP_FRAME seq=9 generation=1 session=1 group=002 title=0x02 counter=0x09 size=50 raw=3109028B1A11...
KWP_DECODE group=002 zone=3 formula=0x85 raw=133 value=12.4688 valid=1
KWP_FRAME seq=13 generation=1 session=1 group=003 title=0xF4 counter=0x0D size=8 raw=070DF4FF00007A03
KWP_DECODE group=003 zone=3 formula=0x84 raw=0 value=RAW valid=0
KWP_FRAME seq=16 generation=1 session=1 group=004 title=0xF4 counter=0x10 size=8 raw=0710F4FF00FF2003
KWP_DECODE group=004 zone=4 formula=0x88 raw=32 value=32.0000 valid=1
KWP_DIAG_STATS frames=83 parser_rejects=0 capture_drops=0 capture_high=1 queue_next=83
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3472 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_DIAG_QUEUE drops=0 high=1 next=83
```

Damit sind reale Rohframes und dekodierte Werte aller Gruppen 000–004 sichtbar.
Nicht belegte Formeln (beispielsweise G69 `0x84`) bleiben als `RAW valid=0`
sichtbar. Während der Ausgabe blieben KWP-Fortschritt und Completionpfad
stabil: keine Parser-Rejects, keine Queue-Drops, kein Byte-Fault, keine stale
Completions und weiterhin `RUNNING` nach dem 60-s-Messfenster.

Host-/Sanitizer-Regression nach dem Diagnosepfad:

```text
alle tests/*_test.cpp mit Warnings-as-errors: PASS
alle tests/*_test.cpp mit ASan/UBSan: PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer: PASS
```

Status: `PASS` – serielle Roh-/Decode-Diagnose auf realem Target nachgewiesen;
keine Runtime-UI geändert.

### Aktueller Build – Wiederholung ohne Runner-Logging

Der aktuelle Sketch wurde anschließend erneut mit `arduino-cli compile` gebaut,
auf `<serial-port>` geflasht und nach einem frischen ECU-Start 60 s
beobachtet. Die per-Byte-/per-Aktionsausgabe des MeasurementRunner bleibt dabei
deaktiviert; serielle Diagnose stammt ausschließlich aus dem entkoppelten
Logger-Task.

Der Lauf meldete:

```text
KWP_TARGET_RESULT=PASS
KWP_DECODE group=000 rpm=0 source=group000
KWP_FRAME seq=71 generation=1 session=1 group=001 title=0x02 counter=0x47 size=50 raw=3147028B1A11FAE1E0BCAD96847561534839281F1912008C2811A06450443A322C26211B16100B0400000085020088FF0003
KWP_DECODE group=001 zone=1 formula=0x8B raw=255 value=29.2500 valid=1
KWP_FRAME seq=75 generation=1 session=1 group=002 title=0xF4 counter=0x4B size=8 raw=074BF4FF0C842D03
KWP_DECODE group=002 zone=3 formula=0x85 raw=132 value=12.3750 valid=1
KWP_FRAME seq=78 generation=1 session=1 group=003 title=0xF4 counter=0x4E size=8 raw=074EF4FF00007C03
KWP_DECODE group=003 zone=3 formula=0x84 raw=0 value=RAW valid=0
KWP_FRAME seq=81 generation=1 session=1 group=004 title=0xF4 counter=0x51 size=8 raw=0751F4FF00FF2003
KWP_DECODE group=004 zone=4 formula=0x88 raw=32 value=32.0000 valid=1
KWP_DIAG_STATS frames=83 parser_rejects=0 capture_drops=0 capture_high=1 queue_next=83
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3474 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_DIAG_QUEUE drops=0 high=1 next=83
```

Damit ist der aktuelle Build mit echten ECU-Frames und Gruppen 000–004 erneut
verifiziert. Während der Ausgabe wurden keine Parser-Rejects, Capture-Drops,
Byte-/Action-Fehler oder Stale-Ereignisse beobachtet; der KWP-Messrunner blieb
`RUNNING`. Die Diagnoseausgabe selbst erzeugt keine Wire-Aktion und kann bei
langsamer Ausgabe ausschließlich die begrenzte Capture-Queue füllen.

Nach dem Targetlauf wurden die 14 Hosttests erneut mit
`-Wall -Wextra -Wpedantic -Werror` sowie alle 14 Tests mit ASan/UBSan
ausgeführt; beide Suiten waren grün. Der abschließende Targetcompile mit
`esp32:esp32:m5stack_tab5` war ebenfalls grün. Der LeakSanitizer ist auf dieser
macOS-Hostplattform nicht verfügbar; ASan/UBSan liefen mit deaktivierter
Leak-Erkennung.

## V2-009 – einzelner RxIngressRing-Pfad

Änderungen: `src/kwp_measurement_session.h`, `src/rx_ingress_ring.h`,
`src/critical_transport_event_ring.h`, `M5Tab5_Digifant_Analyzer.ino`,
`tests/kwp_measurement_session_test.cpp`,
`tests/critical_transport_event_ring_test.cpp` und
`tools/check_pipeline_v2_009.sh`.

Der alte produktive `wire_rx`-/`wire_rx_count`-Pfad wurde entfernt. Der USB-
Callback veröffentlicht ausschließlich `RxIngressItem` mit Batchzeit,
Generation, Epoche und gemeinsamer Transportsequenz. Der KWP-Owner konsumiert
die Items ausschließlich über `RxIngressRing::tryPop()`; Completion- und
Disconnect-Ereignisse werden zusätzlich über den bounded
`CriticalTransportEventRing` korreliert. Die neue Session-Grenze verwirft
stale Generationen und faultet bei einem Epochenwechsel.

RED: Der neue Ring→Session-Test schlug vor der Implementierung wegen der
fehlenden `onRxItem`-Grenze fehl; der statische Guard schlug wegen der alten
`wire_rx`-Zugriffe fehl.

Host-/Sanitizer-Nachweise:

```text
alle tests/*_test.cpp, C++20 -Wall -Wextra -Wpedantic -Werror: PASS
alle tests/*_test.cpp, ASan/UBSan: PASS
tools/check_pipeline_v2_009.sh: PASS
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer: PASS
arduino-cli upload -p <serial-port> --fqbn esp32:esp32:m5stack_tab5: PASS
```

Targetlauf mit Tab5 + K409/FT232R + Digifant-ECU nach der Ring-Umverdrahtung:

```text
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3470 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_DIAG_QUEUE drops=0 high=20 next=83
```

Gruppen 000–004 wurden kontinuierlich dekodiert; `parser_rejects=0`,
`capture_drops=0` und `stale_completions=0` wurden beobachtet. Dieser Lauf
belegt die reale Umverdrahtung auf den Ring.

Nach dem anschließenden Metrik-Reset (damit der absichtliche Poison-Test nicht
die normale High-Watermark verfälscht) wurde ein zweiter frischer Reflash-/
Targetlauf gestartet. K409-Enumeration, Generation 1 und alle drei Preinit-
Control-Completions waren grün, die ECU lieferte jedoch reproduzierbar keine
KWP-RX-Antwort:

```text
KWP_INIT_PROBE state=7 generation=1 rx_bytes=2 key_events=0
KWP_TARGET_RESULT=NO_HANDSHAKE
```

Damit ist das verpflichtende ECU-Gate für den finalen V2-009-Nachweis aktuell
`BLOCKED-HARDWARE-ECU-NO-RX`; die Implementierung und Host-/Targetcompile-
Nachweise sind grün, V2-010 darf erst nach einem erneuten 60-s-Lauf mit
`KWP_TARGET_RESULT=PASS`, `KWP_MEASUREMENT_RESULT=RUNNING` und
`rx_ingress_drops=0` beginnen.

Ein weiterer frischer Tab5-Reset nach erneutem Zündungsstart wurde ebenfalls
ausgeführt (`<serial-port>`, gleicher FQBN). Der K409 wurde erneut als
Generation 1 erkannt und die Preinit-Control-Operationen completeten; die ECU-
Antwort blieb jedoch aus:

```text
KWP_INIT_PROBE state=7 generation=1 rx_bytes=2 key_events=0
KWP_TARGET_RESULT=NO_HANDSHAKE
```

Der Gate-Status bleibt daher reproduzierbar `BLOCKED-HARDWARE-ECU-NO-RX`.

Auch der Lauf bei laufendem Motor (frischer Reflash/Reset, identischer FQBN und
Port) blieb ohne ECU-Antwort. Die gemeldeten 1024 RX-Records stammen aus dem
absichtlichen Target-Ring-Poison-/Reopen-Test vor dem KWP-Handshake; ein
`0x55`-Sync und keine Keybytes wurden von der ECU empfangen:

```text
KWP_INIT_PROBE state=7 generation=1 rx_bytes=1024 key_events=0
KWP_TARGET_RESULT=NO_HANDSHAKE
```

Damit ist der fehlende reale ECU-RX-Nachweis auch bei laufendem Motor bestätigt;
V2-010 bleibt blockiert.

## Known-Good-Referenztest zur Hardwareabgrenzung

Zur Abgrenzung zwischen V2-Ringpfad und Hardware wurde der unveränderte
`M5Tab5_Digifant_Proto` mit demselben Board/FQBN auf `<serial-port>`
kompiliert und geflasht. Auch der Prototyp erreichte die 5-Baud-Phase, erhielt
aber danach keinen ECU-Sync:

```text
[ECU] state=SEND_5BAUD_BITBANG
[ECU] 5-baud address transmission finished.
[ECU] Timeout at 1200 baud waiting for Sync
```

Der Analyzer wurde anschließend wieder geflasht. Der identische Timeout im
Known-Good-Prototyp bestätigt, dass der aktuelle `NO_HANDSHAKE`-Gatefehler
außerhalb der V2-RX-Umverdrahtung liegt (ECU/K-Line-Versorgung, Anschluss oder
Fahrzeugzustand). V2-009 bleibt bis zur ECU-Antwort blockiert.

## V2-009 Wiederherstellung des Analyzer-Runtimepfads

Der offene Analyzer-Tab hatte den Sketch zwischenzeitlich auf den alten
Low-Level-Scaffold zurückgeschrieben. Der Runtimepfad wurde aus den vorhandenen
V2-Komponenten wiederhergestellt: `on_serial_data()` publiziert ausschließlich
in `RxIngressRing`; der KWP-Owner liest ausschließlich `tryPop()`; Completion
und Disconnect werden über `CriticalTransportEventRing` korreliert; die
`KwpMeasurementSession` publiziert valide Frames in die bounded
`PersistenceQueue`, deren Logger-Task die bestehende Decodierung verwendet.
Die drei bewährten FTDI-Preinit-Controls aus der Known-Good-Referenz wurden vor
dem Core-Handshake beibehalten.

Nach Reflash mit FQBN `esp32:esp32:m5stack_tab5` auf `<serial-port>`
meldete der reale Tab5/K409/ECU-Lauf:

```text
KWP_INIT_PROBE state=6 generation=1 rx_bytes=6
KWP_TARGET_RESULT=PASS
KWP_FRAME seq=6 generation=1 group=001 title=0x02 size=50 raw=3106028B1A11...
KWP_FRAME seq=9 generation=1 group=002 title=0x02 size=50 raw=3109028B1A11...
KWP_FRAME seq=12 generation=1 group=003 title=0x02 size=33 raw=200C028B1A11...
KWP_FRAME seq=15 generation=1 group=004 title=0x02 size=33 raw=200F028B1A11...
```

Damit sind Handshake und reale Ring→KWP→Decoder-Daten für Gruppen 000–004
nachgewiesen. Der Aufzeichnungslauf wurde vor dem abschließenden
`KWP_MEASUREMENT_RESULT` am Ende des Fensters beendet; ein vollständiger
60-s-Gateabschluss mit allen Schlusszählern ist deshalb noch offen.

Der anschließende vollständige Gate-Lauf nach frischem Reflash wurde mit
`<serial-port>` durchgeführt und ist für V2-009 grün:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3470 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=1 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_DIAG_QUEUE drops=0 high=1 next=83
```

V2-009 Status: `PASS`; V2-010 ist der nächste Schritt.

## V2-010 – genau eine ValidatedFrameQueue

Die produktive Framegrenze wurde auf `ValidatedFrameQueue` (32 Nutzslots,
Drop-newest) umverdrahtet. `KwpMeasurementSession` publiziert valide Frames
direkt als `KwpFrameEnvelope` by value; `DiagnosticDecoder` konsumiert diese
Queue als einziger Downstream-Consumer. Die frühere produktive
`PersistenceQueue`/`RawFrameRecord`-Instanz wurde aus dem Sketch entfernt. Die
bestehenden Raw-Capture-Typen bleiben ausschließlich für ihre Hosttests und
spätere Persistenzarbeit erhalten.

RED: `validated_frame_pipeline_test` und `check_pipeline_v2_010.sh` schlugen
vor der Umverdrahtung wegen des fehlenden Validated-Frame-Sinks bzw. der noch
vorhandenen zweiten Queue fehl.

Nachweise:

```text
validated_frame_pipeline_test: PASS
alle Hosttests C++20/Werror: PASS
alle Hosttests ASan/UBSan: PASS
check_pipeline_v2_009.sh: PASS
check_pipeline_v2_010.sh: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
```

Realer Regressionslauf nach Reflash auf `<serial-port>`:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3472 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=1 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=83
```

V2-010 Status: `PASS`; nächster Schritt ist V2-011 (entkoppelter Decoder und
MeasurementModel).

## V2-011 – Processing-Decoder und MeasurementModel

Der einzige `ValidatedFrameQueue`-Consumer ist jetzt der Processing-Task. Er
ruft den bestehenden `DiagnosticDecoder` auf, aktualisiert das neue by-value
`MeasurementModel` und publiziert daraus immutable `MeasurementSnapshot`-Werte.
Das Modell trägt Sessionepoch, Transportgeneration, RX-Sequenz, Zeitstempel,
Quellgruppe/-zone, Rohwerte, Validität, Fault- und Dropzähler. Sequenzlücken
invalidieren den Decoder-Headercache und markieren das Modell stale;
Disconnect markiert es disconnected. Der KWP-Runner erhält keinerlei
Decoder-/Modellrückmeldung.

RED/GREEN: `measurement_model_test` schlug zunächst wegen des fehlenden Modells
fehl und ist nach Implementierung grün. Alle bestehenden Tests wurden nicht
abgeschwächt.

Nachweise:

```text
measurement_model_test: PASS
alle Hosttests C++20/Werror: PASS
alle Hosttests ASan/UBSan: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
```

Realer ECU-Lauf auf `<serial-port>`:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3476 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=1 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=83
```

Gruppen 000–004 wurden real verarbeitet; keine Parser-Rejects, RX-Drops,
Poison- oder Action-Fehler. V2-011 Status: `PASS`; nächster Schritt ist V2-012
(immutable Snapshots und unabhängige Mailboxen).

## V2-012 – unabhängige Snapshot-Mailboxen

Der Processing-Task publiziert jeden vollständigen `MeasurementSnapshot` by
value in zwei unabhängige Länge-eins-Mailboxen: `serial_snapshot_mailbox` und
`display_snapshot_mailbox`. Kein Consumer teilt sich eine Mailbox oder besitzt
einen Rückkanal zum Processing-/KWP-Pfad. Overwrite-latest bleibt gezählt und
liefert stets einen vollständigen alten oder neuen Snapshot.

Nachweise:

```text
snapshot_mailboxes_test: PASS
alle Hosttests C++20/Werror: PASS
alle Hosttests ASan/UBSan: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
```

Realer ECU-Lauf nach Reflash auf `<serial-port>`:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3471 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=1 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=83
```

V2-012 Status: `PASS`; nächster Schritt ist V2-013 (Serial als reiner
Snapshot-Consumer).

## V2-013 – Serial als unabhängiger Snapshot-Consumer

Der Serialpfad liest ausschließlich vollständige `MeasurementSnapshot`-Werte
aus `serial_snapshot_mailbox`. Decoder, Framequeue und KWP-Runner werden vom
Serial-Task weder aufgerufen noch synchronisiert. Die Ausgabe ist damit ein
eigener, niedrig priorisierter Consumer; ein langsamer Monitor kann höchstens
Snapshots überschreiben, nicht den ECU-/KWP-Pfad blockieren.

Nachweise:

```text
serial_snapshot_consumer_test: PASS
alle Hosttests C++20/Werror: PASS
alle Hosttests ASan/UBSan: PASS
check_pipeline_v2_013.sh: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
```

Realer ECU-Lauf nach Reflash auf `<serial-port>`:

```text
KWP_TARGET_RESULT=PASS
KWP_SNAPSHOT generation=1 session=1 seq=82 rpm=0 coolant_raw=255 iat_raw=36 battery_raw=131 g69_raw=0 validity=0
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3476 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=1 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=83
```

Es wurden kontinuierlich Snapshot-Zeilen mit Generation, Session, Sequenz,
Rohwerten und Validitätsstatus ausgegeben. Währenddessen blieb der reale
KWP-Dialog in `RUNNING`, ohne Parser-Rejects, RX-Drops, Poison oder
Action-Fehler. V2-013 Status: `PASS`; nächster Schritt ist V2-014
(Display als unabhängiger Snapshot-Consumer).

## V2-014 – Display als unabhängiger Snapshot-Consumer

Der Display-Task liest ausschließlich `display_snapshot_mailbox` über den
vorhandenen Snapshotpfad und rendert RPM, Rohwerte und Validität. Der frühere
`UiState`-Testwrapper ist in R9 entfernt; die Laufzeit verwendet
`DisplayUiModel`.
Die bisherige M5Unified-Initialisierung bleibt erhalten; der KWP-Startpfad
führt keine Displayaufrufe mehr aus. `check_pipeline_v2_014.sh` verhindert
direkte USB-/KWP-/Decoderabhängigkeiten in den UI-Headern.

Nachweise:

```text
ui_snapshot_test: PASS
alle Hosttests C++20/Werror: PASS
alle Hosttests ASan/UBSan: PASS
check_pipeline_v2_014.sh: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
Upload <serial-port>: PASS
```

Realer ECU-Lauf nach Reflash auf `<serial-port>`:

```text
KWP_TARGET_RESULT=PASS
KWP_SNAPSHOT generation=1 session=1 seq=82 rpm=0 coolant_raw=255 iat_raw=37 battery_raw=131 g69_raw=0 validity=0
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3474 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=1 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=83
```

Die serielle Snapshotausgabe und der KWP-/Decoderpfad blieben stabil. Die
physische Sichtprüfung der aktuellen Displaydarstellung sowie ein gezielter
500-ms-Renderstall sind noch als `PENDING-TARGET-VISUAL-EVIDENCE` offen;
Serial/KWP liefern dafür bereits unabhängige Mailboxdaten. V2-014 bleibt bis
dieser kurzen Sichtprüfung `SOFTWARE-PASS / TARGET-VISUAL-PENDING`.

### V2-014 Nachbesserung – flackerfreies Rendering

Die Sichtprüfung zeigte zunächst Flackern. Ursache war ein vollständiges
`fillScreen`/Redraw ohne neuen Snapshot. Der Display-Task rendert nun nur noch
bei erfolgreicher Mailbox-Entnahme; Wartezyklen ändern den Framebuffer nicht.

```text
Targetcompile esp32:esp32:m5stack_tab5: PASS
check_pipeline_v2_014.sh: PASS
Upload <serial-port>: PASS
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=4 group=4 rx_bytes=3428 stale=0
KWP_MEASUREMENT frames=81 ident=3 ack=25 headers=24 bodies=29 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=2 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=81
```

KWP und Serial blieben während des Reflash-Laufs stabil. Die abschließende
Sichtbestätigung der flackerfreien Fassung ist noch offen.

## Erweiterte ECU-Datenrepräsentation – 26 bounded Snapshot-Felder

Der Decoder-/Modelpfad bewahrt nun jedes beobachtete Feld der unterstützten
Gruppen 000–004 by value: zehn Group-000-Bytes sowie vier Zonen je Gruppe 001,
002, 003 und 004. Jedes Feld enthält Gruppe, Zone, Rawwert, Formel-ID, NWB,
Wert, Einheit-/Semantik-ID, Status und die vollständige Zeit-/Sequenz-/
Session-/Generationsprovenienz. Das feste Array hat 26 Einträge; unbekannte
Formeln bleiben `RawOnly` und werden nicht verworfen.

Nachweise:

```text
RED: full_ecu_snapshot_test schlug vor der Modell-/Decodererweiterung wegen fehlender Felder fehl
GREEN: full_ecu_snapshot_test: PASS
alle Hosttests C++20/Werror: PASS
alle Hosttests ASan/UBSan: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
Upload <serial-port>: PASS
```

Realer ECU-Lauf:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=2 group=1 rx_bytes=3471 stale=0
KWP_MEASUREMENT frames=83 ident=3 ack=25 headers=24 bodies=31 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=1 stale_rx=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=83
```

Der Serial-Consumer gab pro Snapshot 26 strukturierte `KWP_FIELD`-Zeilen aus;
bei 83 Frames wurden 2158 Feldzeilen beobachtet. Beispiel:

```text
KWP_FIELD group=2 zone=3 raw=131 formula=0x85 nwb=24 value=12.281 unit=3 semantic=11 status=2 ts=61202152 seq=75 session=1 generation=1
KWP_FIELD group=3 zone=3 raw=0 formula=0x84 nwb=2 value=0.000 unit=0 semantic=12 status=1 ts=63144080 seq=78 session=1 generation=1
KWP_FIELD group=4 zone=3 raw=255 formula=0x87 nwb=1 value=0.000 unit=0 semantic=0 status=1 ts=65104889 seq=81 session=1 generation=1
```

Semantisch belegt sind RPM, Kühlmitteltemperatur, Ansauglufttemperatur,
Batteriespannung, G69-Raw sowie die Group-000-Felder anhand der Known-Good-
Referenz. Dekodierte Formeln sind `0x8B`, `0x8C`, `0x85`, `0x88` und `0x89`.
Die Zonen mit `0x81`, `0x84` und `0x87` bleiben Raw-only; ebenso unbekannte
nummerierte Zonen. Zwischen Decoder und Snapshot gehen keine beobachteten
Felder mehr verloren.

## Semantik-Korrektur gemäß VW-Prüftabelle

Die fachlichen Feldidentitäten wurden anhand der VW/VAG-1551-Prüftabelle für
Digifant 07.93 und der realen Header-/Capture-Struktur korrigiert. Besonders
wichtig: Gruppe 000 Feld 4 (V2-Zone 3) ist Motorlast; Gruppe 000 Feld 10
(V2-Zone 9) ist Motordrehzahl. Die bisherigen Prototyp-Zuordnungen werden
nicht als normative Semantik verwendet.

Semantik- und Formel-Evidenz sind getrennt: offizielle Bedeutungen tragen
`semanticEvidence=Official`; bekannte Headerformeln tragen
`formulaEvidence=Reference`; hergeleitete Group-000-Umrechnungen tragen
`Inferred`; unbekannte Formeln bleiben `Unknown` und Raw-only.

Geänderte Convenience-Quellen:

```text
rpm         <- group000/z9 oder group001..004/z1 (0x8B)
coolantRaw  <- group000/z2 oder group001/z2
iatRaw      <- group000/z0 oder group002/z4
batteryRaw  <- group002/z3
g69Raw      <- group003/z3 (Raw-only, keine Winkelumrechnung)
```

Nachweise:

```text
RED: full_ecu_snapshot_test schlug mit fehlender MotorLoad-Semantik fehl
GREEN: alle Hosttests C++20/Werror: PASS
alle Hosttests ASan/UBSan: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
```

Gezielte Regressionen verhindern ausdrücklich, dass Group-000-Zone 3 wieder
als RPM oder Zone 9 als ein anderes Signal bezeichnet wird. Ein neuer realer
ECU-Lauf ist für diese reine Downstream-Semantikänderung noch ausstehend; die
zuvor nachgewiesenen Transport-/KWP-Regressionen bleiben unverändert.

## Reale ECU-Semantikregression nach der Korrektur

Der korrigierte Build wurde mit angeschlossenem M5Stack Tab5, K409/FT232R und
laufender Digifant-ECU getestet. Der Targetlog wurde 110 s lang in einem
RAM-/Host-Capture aufgezeichnet; die Ausgabe erfolgte ausschließlich über den
bereits entkoppelten Snapshot-Consumer.

Targetnachweis:

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT state=1 stage=4 group=4 rx_bytes=3420 stale=0
KWP_MEASUREMENT frames=81 ident=3 ack=25 headers=24 bodies=29 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=2 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=81
```

Die 26-Feld-Invariante wurde direkt aus dem seriellen Log geprüft: 2106
`KWP_FIELD`-Zeilen = 81 Snapshots × 26 Felder. Jede Gruppe-000-Zone 0–9 und
jede Zone 1–4 der Gruppen 001–004 trat 81-mal auf. Es gab keine fehlenden
Felder, und die Regressionen `000/Z3 -> semantic=MotorLoad` sowie
`000/Z9 -> semantic=Rpm` blieben über alle Beobachtungen erfüllt.

Beispiele aus demselben Lauf (Evidenzwerte: Official=1, Reference=2,
Inferred=4, Unknown=0):

```text
KWP_FIELD group=0 zone=3 raw=32 ... unit=5 semantic=4 semantic_evidence=1 formula_evidence=0 status=1
KWP_FIELD group=0 zone=9 raw=240 ... value=682.000 unit=1 semantic=5 semantic_evidence=1 formula_evidence=4 status=2
KWP_FIELD group=1 zone=2 raw=44 formula=0x8C ... value=31.000 unit=2 semantic=3 semantic_evidence=1 formula_evidence=2 status=2
KWP_FIELD group=2 zone=3 raw=131 formula=0x85 ... value=12.281 unit=3 semantic=2 semantic_evidence=1 formula_evidence=2 status=2
KWP_FIELD group=3 zone=3 raw=0 formula=0x84 ... semantic=11 semantic_evidence=1 formula_evidence=0 status=1
KWP_FIELD group=4 zone=3 raw=255 formula=0x87 ... semantic=13 semantic_evidence=1 formula_evidence=0 status=1
```

Die Convenience-Quellen sind damit targetseitig nachvollziehbar: `rpm` kommt
aus 000/Z9 bzw. 001–004/Z1, `coolantRaw` aus 000/Z2 bzw. 001/Z2,
`iatRaw` aus 000/Z0 bzw. 002/Z4, `batteryRaw` aus 002/Z3 und `g69Raw` aus
003/Z3. Die beispielhaft sichtbaren Rohwerte waren IAT 53, Kühlmittel 44,
Batterie 131 und G69-Raw 0; 000/Z9 lieferte Raw 240 mit der weiterhin als
`INFERRED` markierten Group-000-Umrechnung. Die offiziell belegte Semantik ist
damit bestätigt, ohne eine neue Formel zu behaupten.

Hosttests (C++20/Werror), ASan/UBSan, Targetcompile
(`esp32:esp32:m5stack_tab5`) und Upload waren erfolgreich. Der reale Lauf ist
für KWP-, Queue-, Vollständigkeits- und Semantikpfad `PASS`. Die numerische
Freigabe der Group-000-RPM-Formel bleibt wegen ihrer ausdrücklich `INFERRED`
geführten Evidenz separat offen; es wurde keine neue Formel abgeleitet.

## RPM-Korrelation – ECU-Capture

Ein nachgereichter serieller ECU-Capture (SHA-256
`a05f025615eb11838c3a059d7864061e13f02172fa166a97102c5d73e44df075`)
enthält 37 Snapshots und 962 Feldzeilen (= 37 × 26). KWP blieb `RUNNING`;
die Abschlusszeilen melden 84 Frames, `rejected=0`, `rx_ingress_drops=0`,
`byte_fault=0`, `action_failures=0` und `KWP_FRAME_QUEUE drops=0`.

Beobachtete Group-000-Z9-Punkte:

```text
timestamp_us  raw  163840/raw
37168340      191  857.277  -> 857
46505467      188  871.489  -> 871
56056308      151  1085.033 -> 1085
65539571      143  1145.035 -> 1145
```

Zeitlich benachbarte RPM-Felder der nummerierten Gruppen lagen unter anderem
bei 1025–1068 U/min (frühe Sequenzen), später bei 1454, 1775–1800, 1872,
1961, 2158 und 2477 U/min. Die Group-000-Werte 857/871 liegen in den frühen,
ungefähr 1000-U/min-Sequenzen um etwa 150–200 U/min darunter. Die späteren
Punkte sind wegen der mehrere Sekunden versetzten Feldzeitstempel eine
Drehzahlrampe und kein synchroner Vergleichspunkt.

Der Capture enthält keine externe Kombiinstrument-/Drehzahlreferenz. Daher ist
`163840/raw` mit diesem Material noch nicht als tragfähig freigegeben und
auch nicht als widerlegt markiert: Die zeitliche Asynchronität verhindert eine
saubere End-to-End-Abweichungsrechnung. Es wurde keine alternative Formel
eingeführt.

## RPM-Korrelation – Leerlauf/Low-Ramp-Capture

Ein weiterer Nutzer-Capture (SHA-256
`a17b51654b466629c197912fd9204a12340588eca4ea1986e1969b928570e609`)
enthält 32 Snapshots und 832 Feldzeilen (= 32 × 26). Der Lauf blieb stabil:

```text
KWP_MEASUREMENT state=1 stage=1 group=0 rx_bytes=3437 stale=0
KWP_MEASUREMENT frames=82 ident=3 ack=25 headers=24 bodies=30 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=2 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=82
```

Die Aufzeichnung enthält nur Leerlauf bis ungefähr 1000 U/min; 1500, 2000
und 2500 U/min sind nicht vorhanden. Group-000/Z9 lieferte Raw 207/206,
also 791 bzw. 795 U/min. Die zeitnahen nummerierten RPM-Felder lagen im
Leerlaufbereich bei etwa 820–865 U/min. Beim anschließenden Anstieg wurden
864, 952, 981 und 996 U/min sichtbar, während der letzte Group-000-Wert
weiterhin 795 U/min mit älterem Zeitstempel war.

Damit ist die Formel im Leerlaufbereich plausibel, aber noch nicht vollständig
korreliert: Die höheren Zielbereiche fehlen und die zeitliche Entkopplung der
Feldupdates verhindert eine punktgenaue Gleichzeitigkeit. `163840/raw` bleibt
deshalb `INFERRED`; es wurde keine Formel geändert.

## RPM-Korrelation – 180-s-Test bei laufendem Motor

Für die gezielte Fahrzeugkorrelation wurde ausschließlich das Target-
Testfenster auf 180000 ms verlängert und mit
`KWP_MEASUREMENT_WINDOW_MS=180000` gekennzeichnet. Transport-, KWP- und
Decoderlogik blieben unverändert. Targetcompile und Upload mit
`esp32:esp32:m5stack_tab5` waren erfolgreich.

Der Motor lief bereits vor dem neuen KWP-Handshake. Der Handshake bestand,
die Messsession brach jedoch nach etwa 30,6 s und vor den höheren
Drehzahlphasen ab. Capture-SHA-256:
`e3d50baa08d644b9dda683ebc6f356e24ea77d0ebaaf6cd683b2fb1c94fef184`.

```text
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT_WINDOW_MS=180000
KWP_MEASUREMENT state=2 stage=3 group=2 rx_bytes=1439 stale=0
KWP_MEASUREMENT frames=35 ident=3 ack=11 headers=9 bodies=12 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=3 rx_ingress_drops=0 poison=0 high=2 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_MEASUREMENT_RESULT=NO_FRAME
KWP_FRAME_QUEUE drops=0 high=1 next=35
```

`byte_fault=3` entspricht `ByteEngineFault::InvalidLength`. Bis zum Fault
waren nur Leerlaufwerte enthalten: Group-000/Z9 Raw 207/208 ergab 791/787
U/min; die zeitnahen Felder 001–004/Z1 lagen überwiegend bei 796–879 U/min.
Es gab weder RX-/Framequeue-Drops noch Parser-Rejects oder Actionfehler.

Die RPM-Formel kann aus diesem Lauf nicht weiter qualifiziert werden. Der
nächste technische Schritt ist ein begrenzter Byte-/Zustandstrace unmittelbar
vor `InvalidLength`, um zu klären, ob ein Byte verloren ging oder die Engine
nach einem Turn im falschen Zustand auf die nächste Länge wartete. Die Formel
`163840/raw` bleibt unverändert `INFERRED`.

## Byteengine-Trace für `InvalidLength`

Zur ausschließlichen Diagnose von `byte_fault=3` wurde ein fester
32-Einträge-Ring in die Byteengine aufgenommen. Jeder Eintrag enthält
Timestamp, RX-/TX-/Completion-Richtung, Byte, State vor/nach dem Ereignis,
erwartete Framegröße und Byteindex, RX-/Session-Blockcounter, Turn- und
Operation-ID, Echo-/ACK-/Completion-/Pending-Status sowie Session-, Plan-,
Generations- und Epochenkontext. Der Ring alloziert nicht, überschreibt nur
seinen ältesten Eintrag und formatiert während des KWP-Pfads nichts. Die
serielle Ausgabe erfolgt ausschließlich nach Fault oder Testende.

RED/GREEN-Nachweise:

```text
kwp_invalid_length_trace_test vor Trace-Implementierung: Compile-RED
alle Hosttests C++20/Werror: PASS
kwp_invalid_length_trace_test ASan/UBSan: PASS
kwp_byte_engine_test ASan/UBSan: PASS
kwp_measurement_session_test ASan/UBSan: PASS
Targetcompile esp32:esp32:m5stack_tab5: PASS
Upload <serial-port>: PASS
```

Der reale Lauf mit Tab5, K409 und laufender Digifant-ECU dauerte das volle
180-s-Testfenster. Capture:
`/tmp/kwp_invalid_length_target_trace.log`, SHA-256
`74be82a31b30e0aa934c21a41cbc8b797c364fb5177e3a0a22169030a33b1f2d`.

```text
KWP_MEASUREMENT state=1 stage=3 group=2 rx_bytes=10287 stale=0
KWP_MEASUREMENT frames=243 ident=3 ack=75 headers=73 bodies=92 refused=0 rejected=0
KWP_MEASUREMENT byte_fault=0 rx_ingress_drops=0 poison=0 high=2 stale_rx=0
KWP_MEASUREMENT action_failures=0
KWP_BYTE_TRACE_BEGIN count=32
KWP_BYTE_TRACE_END
KWP_MEASUREMENT_RESULT=RUNNING
KWP_FRAME_QUEUE drops=0 high=1 next=243
```

Der Abschluss-Trace zeigt für den laufenden Group-2-Header die wiederholte
ordnungsgemäße Folge `RxByte -> TX inverse ACK -> korrelierte Completion ->
RX lokales Echo -> RxByte`; alle beobachteten Echo-Bytes entsprachen dem
gesendeten inversen ACK. Der Lauf überschritt den früheren Abbruchzeitpunkt
um mehr als den Faktor fünf, ohne `InvalidLength` zu reproduzieren.

Damit liegt keine reale letzte Bytefolge vor einem neuen `InvalidLength` vor.
Die konkrete Ursache des früheren Einzelereignisses ist mit der verfügbaren
Evidenz nicht bewiesen. Sicher ist nur: `InvalidLength` kann ausschließlich
entstehen, wenn die Engine im Zustand `RxLength` ein Byte außerhalb 3..64
erhält; der erfolgreiche Lauf belegt weder einen RX-Verlust noch eine
Echo-/ACK-Fehleinsortierung. Die Known-Good-Referenz behandelt das unmittelbar
auf eine eigene RX-ACK-Sendung folgende Byte positionsbasiert als lokales
Echo, während V2 zusätzlich Completion und wertbasierte Echo-Korrelation
führt. Dieser Unterschied bleibt eine zu prüfende Hypothese, keine bestätigte
Fehlerursache.

Deshalb wurde keine Byteengine-Verhaltensänderung auf Verdacht vorgenommen.
Die minimale derzeit gerechtfertigte Änderung ist ausschließlich der bounded
Trace. Bei einem natürlichen Wiederholungsfall liefert er die für eine
gezielte Host-Reproduktion nötige Sequenz; erst dann darf eine minimale
Echo-/Pending-/Reset-Korrektur abgeleitet und in einem weiteren ECU-Lauf
verifiziert werden.
## Direkte VW/VAG-Quellenprüfung der Digifant-Semantik

Am 2026-08-24 wurde eine lokale, nicht versionierte Primärquelle
(14 Seiten, VW/VAG-Prüfunterlage) direkt per PDF-Textauswertung geprüft.

Ergebnis: Die offizielle Feldzuordnung bestätigt die vollständige 26-Feld-
Repräsentation. Besonders die zuvor korrigierten Zuordnungen sind explizit
belegt: `000/Z3 = Motorlast`, `000/Z9 = Motordrehzahl`, `000/Z1 =
Spannungsversorgung Motorsteuergerät`, `000/Z5 = Zeitzähler der
Lambdaregelung` und `000/Z6 = Zähler für nicht verwertbares/fehlendes
Sondensignal`. Die Gruppen 001–004 sowie die Zustandsbitbedeutungen von 004/Z4
sind ebenfalls beschrieben.

Die PDF liefert Prüfbereiche und Feldsemantik, aber keine vollständige
Bestätigung aller V2-Rawwertformeln. Formelstatus bleibt deshalb unabhängig
und unverändert (`REFERENCE`, `INFERRED` oder `UNKNOWN`).

Status: `SOURCE-SEMANTICS-CONFIRMED`; keine Produktivcodeänderung.

## Spannungstest – erster aufgezeichneter Messpunkt

Vom Nutzer bereitgestellter serieller Mitschnitt, Multimeterwert `12,43 V`,
Begleittemperatur `16,3 °C`.

| Zustand | 000/Z1 raw | 000/Z1 `24*raw/256` | 002/Z3 raw | 002/Z3 `24*raw/256` | Multimeter | Abweichung ECU | Bewertung |
|---|---:|---:|---:|---:|---:|---:|---|
| Zündung an, Motor aus (vom Nutzer bestätigt) | 130 | 12,1875 V | 130 | 12,1875 V | 12,43 V | −0,2425 V (−1,95 %) | technisch plausibel |

Der Mitschnitt enthält zwar `KWP_SNAPSHOT ... rpm=682`; der Nutzer bestätigte
jedoch, dass der Motor bei der Messung aus war. Der RPM-Wert wird deshalb als
stale bzw. nicht für die Zustandsklassifikation verwendbar behandelt. `000/Z1` wird im aktuellen
Runtimepfad noch als Raw-Feld geführt; die Berechnung erfolgt hier bewusst nur
für die Messauswertung und ändert keine Produktivformel.

Status: `PARTIAL-VOLTAGE-MEASUREMENT`; weitere Punkte (Motor aus, Licht,
Zusatzlast) stehen aus.

### Vorhandene Temperaturdaten aus Nutzer-Captures

Die bereits übermittelten Mitschnitte enthalten folgende ECU-Rohwerte. Die
extern genannten Temperaturen `17,3 °C` und `16,3 °C` sind dabei nicht mit
einem eindeutigen Capture-Zeitstempel oder einer eindeutigen Messstelle im
seriellen Text versehen und werden deshalb nicht künstlich einem einzelnen
Frame zugeordnet.

| Capturezustand | Drehzahlbereich | 000/Z0 IAT raw | 000/Z2 Kühlmittel raw | 002/Z4 IAT raw |
|---|---:|---:|---:|---:|
| vorhandener Lauf A | ca. 835–1067 U/min | 57–58 | 16 bzw. 44 | — |
| vorhandener Lauf B | ca. 827–981 U/min | 68 | 37–50 | 68 |
| vorhandener Lauf C | ca. 879–893 U/min | 68 | 25 | 68 |

Damit ist die vollständige ECU-Rohdatenübernahme und die Wiederholung von
`000/Z0`/`002/Z4` sichtbar. Eine belastbare Raw→°C-Formel oder eine direkte
Abweichung zur externen Motortemperatur lässt sich aus diesen Captures allein
nicht ableiten, weil Referenztemperatur, Messstelle und Zeitpunkt nicht
maschinenlesbar gekoppelt sind.

### Spannungstest – Leerlauf

Der nachgereichte ECU-Mitschnitt zeigt einen laufenden Motor mit etwa
827–981 U/min. Zusammen mit dem zuvor genannten Multimeterwert `14,53 V`:

| Zustand | 000/Z1 raw | 000/Z1 `24*raw/256` | 002/Z3 raw | 002/Z3 `24*raw/256` | Multimeter | Abweichung |
|---|---:|---:|---:|---:|---:|---:|
| Leerlauf | 151 | 14,15625 V | 151 | 14,15625 V | 14,53 V | −0,37375 V (−2,57 %) |

Beide ECU-Pfade liefern wieder identische Rohwerte und Werte. Der Punkt ist
plausibel, stützt die Skalierung jedoch weiterhin nur vorläufig.

### Spannungstest – Licht eingeschaltet

Der Mitschnitt zeigt weiterhin laufenden Leerlauf (`rpm` etwa 823–923) und
`000/Z1 raw=151` sowie `002/Z3 raw=151`. Der zugehörige Multimeterwert beträgt
`14,37 V`.

| Zustand | 000/Z1 raw | 000/Z1 `24*raw/256` | 002/Z3 raw | 002/Z3 `24*raw/256` | Multimeter | Abweichung |
|---|---:|---:|---:|---:|---:|---:|
| Leerlauf + Licht | 151 | 14,15625 V | 151 | 14,15625 V | 14,37 V | −0,21375 V (−1,49 %) |

Auch unter dieser Last bleiben beide ECU-Pfade identisch. Der ECU-Rohwert ist
in diesem Mitschnitt gegenüber dem vorherigen Leerlaufpunkt unverändert.

### Spannungstest – zusätzliche elektrische Last

Der Mitschnitt zeigt laufenden Leerlauf (`rpm` etwa 879–893),
`000/Z1 raw=149` und `002/Z3 raw=149`. Der Multimeterwert beträgt `14,29 V`.

| Zustand | 000/Z1 raw | 000/Z1 `24*raw/256` | 002/Z3 raw | 002/Z3 `24*raw/256` | Multimeter | Abweichung |
|---|---:|---:|---:|---:|---:|---:|
| Zusatzlast | 149 | 13,96875 V | 149 | 13,96875 V | 14,29 V | −0,32125 V (−2,25 %) |

Damit liegen vier Messpunkte vor. Beide ECU-Felder bleiben über alle Punkte
identisch; die ECU-Skalierung liegt jeweils rund 1,5–2,3 % unter dem
Multimeterwert.

## Display-UI – Snapshot-Consumer

Datum: 2026-08-24

- `src/display_ui_model.h` enthält die portable Tab-/Scope-Modellschicht.
- `src/display_ui.h` enthält den Target-Renderer.
- Der Display-Task konsumiert ausschließlich `display_snapshot_mailbox`.
- Vier Tabs: Kompakt, Liste, System und Mitschriebe.
- Die Darstellung entsteht vollständig in einer vorallokierten PSRAM-
  `LGFX_Sprite` (Querformat) und wird geschlossen mit `pushSprite()` übertragen.
  Es gibt kein periodisches `fillScreen()` auf dem sichtbaren Display.
- Mitschriebe verwenden einen festen Ring mit 240 Samples und 100-ms-
  Abtastintervall.
- `tests/display_ui_model_test.cpp`: HOST PASS.
- Alle vorhandenen Hosttests mit Warnings-as-errors: PASS.
- UI-Test mit ASan/UBSan: PASS.
- Pipelineguards `v2_009`, `v2_010`, `v2_013`, `v2_014`: PASS.
- Targetcompile mit `esp32:esp32:m5stack_tab5`: PASS; 782014 Bytes Programm,
  81020 Bytes globale Variablen.
- Targetupload auf `<serial-port>`: PASS; Flash-Verifikation PASS.

Offen ist ausschließlich die manuelle Sichtprüfung am Gerät: Querformat,
flackerfreie Darstellung, Touch-Tabs, Listenpaging und Scope während eines
ECU-Laufs. Transport-/Decoderpfad wurde für die UI nicht verändert.

### UI-Flicker-Korrektur

Nach Targetbeobachtung eines sichtbaren Links-nach-rechts-Aufbaus wurde die
Darstellung an die Known-Good-Referenz angeglichen: Sprite-Übertragung läuft
jetzt innerhalb einer gemeinsamen `M5.Display.startWrite()`/
`endWrite()`-Transaktion. Wiederholte Snapshots mit unveränderter
Sequenznummer lösen keine unnötige Vollbildübertragung mehr aus.

Host-/Sanitizer-Test und Targetcompile/upload nach dieser Korrektur: PASS.
Die erneute reale Sichtprüfung steht noch aus.

### UI-Tabwechsel – Displaytask-Stack

Datum: 2026-08-24

Die Targetbeobachtung „erster Tab stabil; beim Tabwechsel kurz blau, danach
schwarz“ wurde als Reset/Stacküberlauf des Displaytasks eingegrenzt. Der erste
Frame entsteht noch im Setup-Kontext, während ein Touch-Wechsel erstmals die
vollständigen Listen-/System-/Scope-Renderer im zuvor nur 4096 Byte großen
Displaytask-Stack ausführt.

- Displaytask-Stack kontrolliert auf 12288 Byte erhöht.
- Periodische, nicht zeitkritische Telemetrie
  `DISPLAY_TASK_STACK_HIGH_WATER=<words>` ergänzt.
- `display_ui_model_test`: HOST mit Warnings-as-errors PASS.
- `display_ui_model_test`: ASan/UBSan PASS.
- Targetcompile `esp32:esp32:m5stack_tab5`: PASS; 783460 Bytes Programm,
  82036 Bytes globale Variablen.
- Targetupload `<serial-port>`: PASS; Flash-Hash verifiziert.

Die reale Touchprüfung aller vier Tabs nach dieser Korrektur ist noch offen.

### UI-Evidenz und RAW-zu-Wert-Darstellung

Datum: 2026-08-24

- Die Darstellungsentscheidung ist im portablen UI-Modell zentralisiert.
- `REFERENCE` wird normal dargestellt.
- `INFERRED` und `EXPERIMENTAL` werden grau dargestellt.
- `UNKNOWN` bleibt grau und wird ausdrücklich als `RAW` angezeigt.
- Bei `STALE`, `INVALID`, `DISCONNECTED` oder nicht verfügbarem Feld erscheint
  `---`; alte dekodierte Werte werden nicht als aktuell dargestellt.
- Die Listenansicht zeigt Semantik- und Formel-Evidenz getrennt als `S:` und
  `F:` sowie bei belastbarer Dekodierung `RAW <n> -> <Wert> <Einheit>`.
- Die Kompaktkacheln zeigen zusätzlich zum Hauptwert den Rawwert und beide
  Evidenzkürzel.

Verifikation:

- `display_ui_model_test`: HOST mit Warnings-as-errors PASS.
- `display_ui_model_test`: ASan/UBSan PASS.
- Targetcompile `esp32:esp32:m5stack_tab5`: PASS; 783778 Bytes Programm,
  82036 Bytes globale Variablen.
- Targetupload `<serial-port>`: PASS; Flash-Hash verifiziert.

Die fachliche Darstellung verwendet ausschließlich vorhandene Snapshotdaten;
es wurden keine Formeln, Einheiten oder Decoderzuordnungen ergänzt.

### Motor-aus-RPM-Regression

Datum: 2026-08-24

Reale Beobachtung: Bei eingeschalteter Zündung und stehendem Motor zeigte die
Kompaktansicht `682 rpm`. Die Ursache ist reproduzierbar und bereits durch den
früheren Targetmitschnitt belegt: `000/Z9 raw=240` wurde mit der nur
`INFERRED` bewerteten Formel `163840/raw` zu `682` umgerechnet. Dieser Wert ist
für den Motor-aus-Zustand empirisch falsch.

Korrektur ohne neue Formel:

- Das generische Feld `000/Z9` bleibt vollständig erhalten; Rawwert und
  inferierte Rechnung bleiben in der Liste grau sichtbar.
- Der produktive RPM-Convenience-Wert wird nicht mehr aus der inferierten
  Group-000-Formel gesetzt.
- Kompaktansicht und Scope wählen ausschließlich das neueste aktuelle,
  `REFERENCE`-dekodierte RPM-Feld aus `001–004/Z1`.
- Liegt noch kein Referenzwert derselben Session und Transportgeneration vor,
  erscheint `---`.
- Felder einer alten Session oder Transportgeneration werden unabhängig vom
  globalen Snapshotstatus als ungültig dargestellt.

Verifikation:

- Alle vorhandenen Hosttests mit C++20/Werror: PASS.
- `display_ui_model_test`, `measurement_model_test` und
  `full_ecu_snapshot_test` mit ASan/UBSan: PASS.
- Targetcompile `esp32:esp32:m5stack_tab5`: PASS; 784008 Bytes Programm,
  82036 Bytes globale Variablen.
- Targetupload `<serial-port>`: PASS; Flash-Hash verifiziert.
- Displaytask-Stackreserve nach Start: 10156, später 9372 Words.
- Der unmittelbar anschließende ECU-Handshake antwortete nach dem Flashreset
  nicht (`KWP_TARGET_RESULT=NO_HANDSHAKE`); die reale Motor-aus-Anzeige benötigt
  daher noch einen frischen ECU-Handshake nach Zündungszyklus.

### Nummerierte RPM-Endpunktkorrektur

Datum: 2026-08-24

Nach Auswahl der `REFERENCE`-RPM-Felder zeigte der reale Motor-aus-Zustand
`29,2 rpm`. Vorhandene Targetlogs belegen die Ursache unmittelbar:
`001/Z1 formula=0x8B raw=255 value=29.2500`. Der Maximal-Rawwert ist beim
realen Digifant der Stillstands-Endpunkt; die allgemeine 15/16-
Tabelleninterpolation erreicht den letzten Tabellenwert ohne Sonderbehandlung
nicht exakt.

- `0x8B raw=255` wird jetzt explizit als Endpunkt `0 rpm` dekodiert.
- Alle übrigen Tabellenwerte und Formeln bleiben unverändert.
- Der Rawwert `255` bleibt im generischen Feld erhalten.
- RPM-Werte werden auf Displaykacheln und in der Liste ohne Nachkommastelle
  formatiert.

Verifikation:

- Alle vorhandenen Hosttests mit C++20/Werror: PASS.
- Parser-, Diagnostic- und UI-Modelltests mit ASan/UBSan: PASS.
- Gezielte Regression `0x8B / raw=255 -> 0.0`: PASS.
- Targetcompile `esp32:esp32:m5stack_tab5`: PASS; 784152 Bytes Programm,
  82036 Bytes globale Variablen.
- Targetupload `<serial-port>`: PASS; Flash-Hash verifiziert.

Die reale Motor-aus-Verifikation benötigt nach dem Upload erneut einen
Zündungszyklus und ist noch offen.

### Automatischer ECU-Reconnect ohne Tab5-Reset

Datum: 2026-08-24

Die einmalige Boot-Sperre `static ran` wurde entfernt. Die Recovery übernimmt
die in der Known-Good-Referenz bewährten Grenzen: 6000 ms zwischen vollständigen
Initversuchen und 4000 ms Sessiontimeout ohne RX-Fortschritt.

- Solange der K409 verbunden ist und keine Session läuft, folgen automatisch
  weitere vollständige 5-Baud-/KWP-Initversuche.
- Die Messsession läuft kontinuierlich statt nach 180 Sekunden zu enden.
- Jeder Versuch erhält eine neue `session_epoch`.
- `transport_op_id` bleibt über Preinit, KWP-Core, MeasurementSession und
  spätere Versuche monoton und wird nicht wiederverwendet.
- `OutcomeUnknown` oder Critical-Event-Overflow sperrt weitere Wireaktionen bis
  zu einer neuen Transportgeneration; normale Sync-/Sessionfehler dürfen nach
  dem Backoff erneut initialisieren.
- Sessionverlust wird dem alleinigen Processing-/Model-Owner als Stale-Anfrage
  übergeben; kein zweiter Model-Writer wurde eingeführt.

Verifikation:

- Neuer `kwp_reconnect_policy_test`: PASS, einschließlich Zeitwrap,
  6000-ms-Retrygrenze und 4000-ms-Stallgrenze.
- Alle Hosttests mit C++20/Werror: PASS.
- Reconnect-, Core-, MeasurementSession- und Modeltests mit ASan/UBSan: PASS.
- Pipelineguards V2-009, V2-010, V2-013 und V2-014: PASS.
- Targetcompile `esp32:esp32:m5stack_tab5`: PASS; 785184 Bytes Programm,
  82084 Bytes globale Variablen.
- Targetupload `<serial-port>`: PASS; Flash-Hash verifiziert.
- Nutzerprüfung: Recovery nach Zündungswechsel mehrfach ohne Tab5-Reset
  erfolgreich.
- Zusätzlich automatisch beobachtet:

```text
KWP_TARGET_BEGIN generation=1 session=1 address=1
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT_MODE=CONTINUOUS
KWP_SESSION_TIMEOUT_RETRY
KWP_TARGET_BEGIN generation=1 session=2 address=1
KWP_TARGET_RESULT=PASS
KWP_MEASUREMENT_MODE=CONTINUOUS
```

Die zweite Session blieb im anschließenden Beobachtungsfenster aktiv; es gab
keinen Boardreset und keine neue Transportgeneration.

### Bounded Display-Startup-Recovery

Nach einem Upload trat einmal `DISPLAY_UI_SPRITES ... ready=0` auf. Da
`DisplayUi::update()` bei `ready=false` dauerhaft zurückkehrte, konnte sich die
UI ohne Boardreset nicht erholen.

- 150 ms zusätzliche Display-Settling-Zeit nach `M5.begin()`.
- Bei fehlgeschlagener Sprite-Erzeugung werden alle Teil-Sprites freigegeben
  und höchstens 20 Initialisierungsversuche im Abstand von 500 ms ausgeführt.
- Jeder Versuch protokolliert Displaymaße, Spritemaße, Ready-Status und freien
  PSRAM; nach Erfolg endet die Retrylogik dauerhaft.
- Renderingstrategie und Snapshot-only-Consumervertrag bleiben unverändert.

Realer Startnachweis nach Upload und anschließendem seriellen Reset:

```text
DISPLAY_UI_SPRITES attempt=1 display=1280x720 tile=305x293
status=1280x58 row=1256x42 scope=1256x430 ready=1 psram=30187316
DISPLAY_TASK_STACK_HIGH_WATER=7932
DISPLAY_TASK_STACK_HIGH_WATER=7800
```

Status: `DISPLAY-STARTUP-PASS`, `AUTO-ECU-RECOVERY-PASS`.

### Explizite Statusreihenfolge K409 -> KWP -> ECU

Datum: 2026-08-24

Die drei Statusanzeigen wurden zuvor indirekt aus der allgemeinen
Snapshot-Gültigkeit abgeleitet. Dadurch konnte ein noch datenloser bzw. stale
Snapshot KWP grün anzeigen, bevor der physische K409-Status sichtbar war.

- `MeasurementSnapshot` enthält jetzt getrennte, by-value Flags
  `k409Connected`, `kwpConnected` und `ecuDataValid`.
- Der alleinige Processing-/Model-Owner übernimmt den physischen
  Gerätestatus und den KWP-Sessionstatus; der Displaytask bleibt ein reiner
  Snapshot-Consumer.
- `kwpConnected` wird nur bei gleichzeitigem `k409Connected` gesetzt.
- `ecuDataValid` wird erst nach einem gültig dekodierten Frame der aktuellen
  Session gesetzt und bei Sessionverlust/Disconnect gelöscht.
- Serial gibt dieselben drei Snapshotflags aus wie das Display.

Targetseitig wurde folgende Progression beobachtet:

```text
KWP_SNAPSHOT ... k409=0 kwp=0 ecu=0
KWP_SNAPSHOT ... k409=1 kwp=0 ecu=0
KWP_SNAPSHOT ... k409=1 kwp=1 ecu=0
KWP_SNAPSHOT ... k409=1 kwp=1 ecu=1
```

Verifikation:

- Alle 23 Hosttests mit C++20 und Warnings-as-errors: PASS.
- `measurement_model_test`, `display_ui_model_test` und
  `kwp_reconnect_policy_test` mit ASan/UBSan: PASS.
- Pipelineguards V2-009, V2-010, V2-013 und V2-014: PASS.
- Targetcompile `esp32:esp32:m5stack_tab5`: PASS; 785628 Bytes Programm,
  82092 Bytes globale Variablen.
- Targetupload `<serial-port>`: PASS; Flash-Hash verifiziert.
- Reale ECU-Daten nach Upload: `generation=1 session=2`, Gruppenverarbeitung
  aktiv, `k409=1 kwp=1 ecu=1`.

Status: `STATUS-ORDER-TARGET-PASS`.

### Tab5-Panel-Init nach Upload

Datum: 2026-08-24

Ein Diagnoseboot zeigte `M5.Display.width()==0` und
`M5.Display.height()==0`; reine Sprite-Retries können diesen Zustand nicht
beheben. Die lokale M5Unified-Version führt den ersten DSI-Panel-Probe vor
ihrem Power-Setup aus und lässt `M5.begin()` danach absichtlich kein zweites
Mal laufen. Das erklärt die Beobachtung „nach Upload schwarz, nach manuellem
Reset funktionsfähig“.

- Direkt nach `M5.begin()` wird die Displaybereitschaft geprüft.
- Nur bei `0x0` wird der Panel-Init vor USB/KWP und vor allen Tasks höchstens
  fünfmal im Abstand von 250 ms wiederholt.
- Nach erfolgreichem späten Panel-Init wird das Display einmal bei M5Unified
  registriert, damit Touch denselben Displaypfad verwendet.
- Es gibt keine Display-Reinitialisierung im Runtime-/KWP-Pfad.

Der anschließende reale Upload und serielle Neustart initialisierten bereits
beim ersten Probe korrekt:

```text
DISPLAY_UI_SPRITES attempt=1 display=1280x720 tile=305x293
status=1280x58 row=1256x42 scope=1256x430 ready=1 psram=30187316
```

Der Schutzpfad ist Target-kompiliert und geflasht; ein erneuter realer
`0x0`-Erstprobe trat im abschließenden Lauf nicht auf.

### Betriebszustandsabhängige Kompaktwerte

Datum: 2026-08-24

Die reale ECU liefert bei stehendem Motor weiterhin einen gültigen internen
Einspritzzeit-Rechenwert. Im Targetlauf wurde gleichzeitig beobachtet:

```text
KWP_SNAPSHOT ... rpm=0 ... k409=1 kwp=1 ecu=1 validity=0
KWP_FIELD group=2 zone=2 raw=16 formula=0x89 nwb=50 value=8.000
```

`8,0 ms` ist damit korrekt aus dem ECU-Feld dekodiert, aber bei `RPM=0` kein
Nachweis eines tatsächlich ausgegebenen Einspritzimpulses. Die generische
26-Feld-Repräsentation, Liste und Serialausgabe bleiben deshalb unverändert.
Nur die Kompaktansicht berücksichtigt zusätzlich den aktuellen Motorzustand.

Audit der acht Kompaktfelder:

| Feld | Motor aus | Darstellung |
|---|---|---|
| RPM | echter Stillstandswert | `0 rpm` |
| Kühlmitteltemperatur | Sensorsignal weiterhin sinnvoll | unverändert |
| Ansauglufttemperatur | Sensorsignal weiterhin sinnvoll | unverändert |
| Versorgungsspannung | weiterhin sinnvoll | unverändert |
| Motorlast | Betriebswert ohne drehenden Motor irreführend | `MOTOR AUS` |
| G69/Drossel | Stellung ist auch bei Motor aus sinnvoll | Rawwert unverändert |
| Einspritzzeit | ECU-Rechenwert, kein Impulsnachweis | `MOTOR AUS` |
| Lambda-Sondenspannung | elektrische Spannung weiterhin messbar | sichtbar, präziser als `LAMBDA-SPANNUNG` bezeichnet |

Die Betriebsentscheidung verwendet ausschließlich das neueste aktuelle
`REFERENCE`-RPM-Feld derselben Session und Transportgeneration:

- RPM > 0: Motorlast und Einspritzzeit werden normal dargestellt.
- RPM = 0: Hauptwert `MOTOR AUS`; darunter bleibt `ECU RAW <n>` sichtbar.
- RPM noch unbekannt: `STATUS ---`; es wird kein Betriebswert behauptet.
- Stale/Disconnect/Invalid: bestehendes `---` bleibt vorrangig.

Verifikation:

- Gezielter `display_ui_model_test`: PASS, einschließlich Running, Stopped,
  Unknown und sessionfremdem RPM-Feld.
- Alle 23 Hosttests mit C++20 und Warnings-as-errors: PASS.
- `display_ui_model_test` mit ASan/UBSan: PASS.
- Pipelineguards V2-009, V2-010, V2-013 und V2-014: PASS.
- Targetcompile `esp32:esp32:m5stack_tab5`: PASS; 786016 Bytes Programm,
  82092 Bytes globale Variablen.
- Targetupload `<serial-port>`: PASS; Flash-Hash verifiziert.
- Reale Motor-aus-Session lief von mindestens Sequenz 0 bis 18 weiter;
  K409, KWP und ECU blieben aktiv.
- Display nach Upload: `1280x720`, alle Teil-Sprites `ready=1`.

Zusätzlich ersetzt ein einmaliger, durch RTC-Zustand begrenzter automatischer
Boot-Recovery-Neustart den zuvor gelegentlich erforderlichen manuellen Reset
nach fehlgeschlagenem Tab5-Panel-Probe. Er läuft ausschließlich vor USB/KWP;
ein persistenter Paneldefekt kann keine Neustartschleife auslösen.

### Reale G69-Pedalweg-Korrelation

Datum: 2026-08-24

Aufbau: Tab5 + K409 + reale Digifant-ECU, Zündung an, Motor aus. Die
Drosselklappe wurde über das Fahrpedal in neun gehaltenen Stufen betätigt:
geschlossen, ungefähr 1/4, 1/2, 3/4, voll und anschließend in umgekehrter
Reihenfolge zurück. Jede Stellung wurde ungefähr 15 s gehalten. Ausgewertet
wurden ausschließlich die bestehenden Snapshot-/Serialdaten; der
zeitkritische KWP-Pfad wurde nicht instrumentiert oder verändert.

Repräsentative stabile Messpunkte (die Gruppen werden zeitversetzt abgefragt;
Zwischenwerte sind daher nur näherungsweise derselben Pedalstellung
zuzuordnen):

| Bewegungsphase | 000/Z7 Raw | 000/Z7 mit inferierter `raw*0,02 V`-Formel | 003/Z3 Raw |
|---|---:|---:|---:|
| geschlossen, Beginn | 37 | 0,74 V | 0 |
| Öffnen, Zwischenpunkt | 151–152 | 3,02–3,04 V | 31 |
| Öffnen, Zwischenpunkt | 192 | 3,84 V | 59 |
| oberer Endpunkt / Vollpedal | 224 | 4,48 V | 80 |
| Schließen, Zwischenpunkt | 178 | 3,56 V | 53 |
| Schließen, Zwischenpunkt | 176 | 3,52 V | 48 |
| Schließen, Zwischenpunkt | 145 | 2,90 V | 27 |
| geschlossen, Ende | 37 | 0,74 V | 0 |

Ergebnis:

- Beide Signale stiegen beim Öffnen monoton und fielen beim Schließen wieder
  ab. In den erfassten Haltephasen wurden keine Aussetzer oder unplausiblen
  Sprünge beobachtet.
- Die Endpunkte waren reproduzierbar: `000/Z7=37`, `003/Z3=0` geschlossen
  sowie `000/Z7=224`, `003/Z3=80` bei Vollpedal.
- Damit ist die offiziell geforderte gleichmäßige Änderung von G69 im realen
  Fahrzeug experimentell gestützt.
- Für dieses Fahrzeug und die aktuelle Betätigung ist der beobachtete
  `003/Z3`-Rawbereich `0..80` belegt. Daraus folgt jedoch ohne externe
  Winkelreferenz keine belastbare Gradskala.
- Die generische KWP-Formel `0x84` aus Referenzimplementierungen und die
  Feldsemantik besitzen weiterhin getrennte Evidenz. `003/Z3` bleibt in der
  Produktdarstellung Raw-only; weder `0..80 Grad` noch eine `0..90 Grad`-
  Umrechnung wird aus diesem Test abgeleitet.
- Eine fahrzeugspezifische normierte Öffnung könnte künftig ausdrücklich als
  experimentelle Convenience-View aus den gemessenen Endpunkten berechnet
  werden. Sie wäre keine absolute Winkelmessung.

Status: **G69-MONOTONIC-TARGET-PASS**. Kein weiterer identischer Pedaltest ist
erforderlich. Für eine Freigabe in Grad wäre eine unabhängige mechanische
Winkelreferenz oder eine belastbare feldspezifische technische Quelle nötig.

## V2-015 – Entkopplungs- und Erweiterungsabnahme (Softwarevorbereitung)

Datum: 2026-08-24

Status: **PASS**. Software-, Target- und reales ECU-Gate sind abgeschlossen.

### RED

Der neue Test `tests/v2_015_decoupling_test.cpp` wurde zuerst gegen den
vorherigen Stand kompiliert. Die Kompilierung schlug erwartungsgemäß fehl,
weil `SnapshotConsumerFanout` und die prüfbare Publikationszählung noch nicht
existierten. Der Test deckt folgende Verträge ab:

- 60 s simulierter Decoderstillstand mit ausschließlich dokumentierten
  Drop-newest-Frameverlusten und sichtbarer Sequenzlücke;
- unabhängige Serial-/Display-/Bluetooth-/Web-Mailboxen;
- dauerhaft blockiertes Serial und Web;
- 500 Snapshotperioden blockiertes Display;
- langsamer Bluetoothconsumer;
- Concurrent-Overwrite ohne Torn Snapshot und ohne Publikationsdrop.

### Umverdrahtung und Bereinigung

- `LatestSnapshotMailbox` verwendet nun drei feste Slots mit explizitem
  SPSC-Slotlifecycle. Writer und Reader greifen nie gleichzeitig auf denselben
  Snapshotwert zu. Es gibt keine dynamische Allokation und keinen blockierenden
  Downstream-Wait.
- `SnapshotConsumerFanout` besitzt genau vier feste Latest-Mailboxen für
  Serial, Display sowie die Dummy-Bluetooth-/Webconsumer. Jeder Snapshot wird
  by value und unabhängig publiziert.
- Bluetooth und Web laufen auf dem Target als langsame, verwerfende
  Dummy-Blattconsumer ohne Funk-, Netzwerk- oder Protokollcode.
- Runtime-Telemetrie (Frames, Frame-/RX-/Snapshotoverwrites,
  Parser-Rejects, Bytefaults und Actionfehler) fließt einwegig in den
  `MeasurementSnapshot`. Serial und Display lesen diese Werte ausschließlich
  aus ihrer jeweiligen Snapshotkopie.
- Alle formatierte Runtime-Serialausgabe wurde aus Callback-, Transport-,
  KWP- und Displaypfaden entfernt. Sie befindet sich ausschließlich im
  niedrig priorisierten Serial-Snapshotconsumer. `Serial.begin/flush` bleiben
  begrenzte Startupoperationen vor dem KWP-Betrieb.
- Der ungenutzte parallele `RawFrameRecord`-/`PersistenceQueue`-/
  `setCaptureSink`-Pfad wurde erst nach grünem ValidatedFrameQueue-Ersatz
  entfernt. Decoder und Tests verwenden jetzt ausschließlich
  `KwpFrameEnvelope` aus der einen `ValidatedFrameQueue`.
- `ValidatedFrameQueue::nextSequence` ist für die einwegige Telemetrie nun
  atomar lesbar; Full- und Drop-newest-Vertrag bleiben unverändert.
- Für den ECU-Abnahmelauf existiert der compile-time Testmodus
  `V2_015_TARGET_STRESS=1`. Er pausiert ausschließlich den Displayconsumer
  wiederholt für 500 ms; Upstreamcode und Produktionsbuild bleiben
  unverändert.

Geänderte bzw. entfernte Dateien:

- `M5Tab5_Digifant_Analyzer.ino`
- `src/measurement_snapshot.h`
- `src/measurement_model.h`
- `src/validated_frame_queue.h`
- `src/kwp_measurement_session.h`
- `src/diagnostic_decoder.h`
- `src/display_ui.h`
- `src/raw_capture_queue.h` (entfernt)
- `tests/v2_015_decoupling_test.cpp` (neu)
- `tests/measurement_model_test.cpp`
- `tests/diagnostic_decoder_test.cpp`
- `tests/kwp_measurement_session_test.cpp`
- `tests/frame_capture_queue_test.cpp`
- `tools/check_pipeline_v2_013.sh`
- `tools/check_pipeline_v2_014.sh`
- `tools/check_pipeline_v2_015.sh` (neu)

### Tatsächlich ausgeführte Tests

- 24/24 Hosttests, C++20, `-Wall -Wextra -Wpedantic -Werror`: **PASS**.
- 24/24 Hosttests mit ASan/UBSan: **PASS**. LeakSanitizer wird vom lokalen
  macOS-Toolchainbuild nicht unterstützt; ASan/UBSan liefen deshalb mit
  `detect_leaks=0` vollständig durch.
- `v2_015_decoupling_test` zusätzlich mit TSan: **PASS**, keine Data Race.
- Gezielter finaler Stalltest mit Host, ASan/UBSan und TSan nach Ergänzung des
  dauerhaft blockierten Serialconsumers: **PASS**.
- `tools/check_pipeline_v2_015.sh`: **PASS**. Der Check bestätigt genau einen
  `RxIngressRing`, eine `ValidatedFrameQueue`, einen Decoderowner, keine
  Persistence-/Raw-Parallelroute und vier unabhängige Snapshotconsumer.
- Produktions-Targetcompile mit `esp32:esp32:m5stack_tab5`: **PASS**;
  781806 Bytes Flash, 95572 Bytes globale Variablen.
- Target-Stresscompile mit
  `compiler.cpp.extra_flags=-DV2_015_TARGET_STRESS=1`: **PASS**;
  781846 Bytes Flash, 95572 Bytes globale Variablen.
- `git diff --check -- M5Tab5_Digifant_Analyzer`: **PASS**.

### Noch offenes Hardwaregate

Erforderlich ist ein realer Lauf mit Tab5, K409 und Digifant-ECU:

1. mindestens 60 s kontinuierlicher Betrieb und vollständige Gruppen 000–004;
2. wiederholte automatische 500-ms-Displaypausen im Stressbuild;
3. manuelle Tabwechsel einschließlich Mitschreibansicht;
4. weiterhin laufende Serial-Snapshotausgabe;
5. abschließend `rx_drops=0`, `frame_drops=0`, `parser_rejects=0`,
   `byte_fault=0`, `action_failures=0` und keine KWP-Unterbrechung durch einen
   Consumer.

Die fehlende optionale Logic-Analyzer-Evidenz bleibt davon getrennt als
`PENDING-OPTIONAL-HARDWARE-EVIDENCE` dokumentiert.

### Reales ECU-Gate – PASS

Datum: 2026-08-24

Aufbau: realer M5Stack Tab5, K409/FT232R und Digifant-ECU. Geflasht wurde
zunächst der mit `V2_015_TARGET_STRESS=1` gebaute Teststand. Die serielle
Aufzeichnung wurde durch den Agenten direkt auf `<serial-port>` geführt;
es bestand kein konkurrierender serieller Monitor.

Testablauf:

- Verbindungsaufbau aus dem Zustand Zündung aus bis zur laufenden ECU;
- mehr als 60 s kontinuierlicher Messbetrieb, tatsächlich rund 200 s;
- automatische 500-ms-Pause des Displayconsumers alle vier Sekunden;
- manuelle Wechsel Kompakt → Liste Seite 1 → Liste Seite 2 → System →
  Mitschriebe;
- Mitschreibansicht pausiert und wieder fortgesetzt;
- Rückkehr zur Kompaktansicht;
- Serial formatierte währenddessen kontinuierlich vollständige
  Snapshotkopien;
- langsame Dummy-Bluetooth-/Webconsumer blieben gleichzeitig aktiv.

Realer Abschlussstand aus einer vollständig aufgezeichneten Snapshotzeile:

```text
KWP_SNAPSHOT generation=1 session=8 seq=161 rpm=879 ...
k409=1 kwp=1 ecu=1 validity=0 frames=162
frame_drops=0 rx_drops=0 snapshot_overwrites=157
parser_rejects=0 byte_fault=0 action_failures=0 faults=0
```

Unmittelbar danach wurden weitere Frames derselben Session beobachtet
(`seq>=163`); es gab keinen Reconnect und keinen Sessionwechsel. Alle 26
Felder waren im Snapshot befüllt. Gruppen 000 sowie 001–004 wurden während
des Laufs wiederholt aktualisiert.

Bewertung der `snapshot_overwrites=157`: Dies sind erwartete, gezählte
Overwrite-latest-Ereignisse ausschließlich an den absichtlich pausierten oder
langsamen Blattconsumer-Mailboxen. Sie erzeugten weder Frame-/RX-Drops noch
Parser-, Byte-, Action- oder Decoderfehler. Damit ist gerade die geforderte
Entkopplung nachgewiesen; die Zahl ist kein Verlust im ECU-/KWP-/Decoderpfad.

Nach dem erfolgreichen Gate wurde der normale Produktionsbuild ohne
`V2_015_TARGET_STRESS` erneut kompiliert und auf denselben Tab5 geflasht.
Upload und Flash-Hashprüfung: **PASS**. Produktionsgröße: 781806 Bytes Flash,
95572 Bytes globale Variablen.

V2-015-Abschluss: Die einzige produktive Datenrichtung ist nachweislich

```text
ECU/K409 → RxIngressRing → KWP-Runner → ValidatedFrameQueue
→ Processing/Decoder → MeasurementModel/Snapshot
→ unabhängige Serial-/Display-/Bluetooth-/Web-Consumer
```

Serial, Display und Mitschreibansicht beeinflussten RX, KWP und Decoder nicht.
Status: **V2-015 PASS**.

## Langzeit-ECU-Datenlogger „Sprotz" – Software- und Targetvorbereitung

Datum: 2026-08-24

Status: **SOFTWARE-PASS / TARGET-SD-WRITE-AND-READBACK-PASS /
PENDING-ECU-LOGGER-EVIDENCE**.

Implementiert wurde ausschließlich ein neuer Downstream-Blattconsumer:

```text
MeasurementSnapshot → feste SPSC-Loggerqueue (32 nutzbare Plätze)
→ Logger-Task Priorität 1 → versionierte Binärdatei auf microSD
```

UI-Kommandos laufen separat über eine feste 8-Record-SPSC-Queue. Loggerstatus
wird by value an Display und den bestehenden Serialconsumer publiziert. Die
Logger-Task ist der einzige Besitzer von `SD_MMC` und `File`; Transport, KWP,
ValidatedFrameQueue, Decoder und MeasurementModel kennen weder Loggerstatus
noch Dateisystem. START/STOP/MARKER tragen `esp_timer_get_time()` als monotonen
64-Bit-Zeitstempel. Ein Snapshotrecord ist fest 1112 Byte groß und enthält alle
26 Felder samt Rawwert, Formel/NWB, Decodewert, Einheit/Semantik, beiden
Evidenzgraden, Status und Feldprovenienz sowie Snapshot-Telemetrie.

Tatsächlich ausgeführte Nachweise:

- `sprotz_logger_test`, C++20 mit Warnings-as-errors: **PASS**;
- derselbe Test mit ASan/UBSan: **PASS**;
- SPSC-Snapshotqueue und Statusmailbox unter TSan: **PASS**, keine Race;
- Fehlerfälle Queuefull, Openfehler, Writefehler, Flushfehler und Storage-full:
  **PASS**; sie verändern keinen Upstreamzustand;
- START → zwei Snapshots → MARKER → STOP inklusive Timestamp-/Metadatenprüfung:
  **PASS**;
- Start vor erstem ECU-Snapshot behält die kanonischen 26 Feldpositionen:
  **PASS**;
- Binärfixture → `tools/decode_sprotz_log.py` → Wide-CSV mit allen 26 Feldern:
  **PASS**;
- vollständige Hostsuite: 25/25 **PASS**;
- vollständige Hostsuite mit ASan/UBSan: 25/25 **PASS**;
- `tools/check_sprotz_logger.sh`: **PASS**;
- `tools/check_pipeline_v2_015.sh`: **PASS**;
- `git diff --check -- M5Tab5_Digifant_Analyzer`: **PASS**;
- Targetcompile `esp32:esp32:m5stack_tab5`: **PASS**, 904430 Byte Flash,
  142316 Byte globale Variablen, 185364 Byte verbleibend;
- Upload auf realen ESP32-P4 `<serial-port>` inklusive Hashprüfung:
  **PASS**;
- realer Boot ohne eingelegte/lesbare microSD: UI/Serial melden eindeutig
  `NoStorage`, `records=0`, `queue_drops=0`; K409 und der übrige Runtimepfad
  starten weiter: **TARGET-NO-STORAGE-PASS**.

### Reales microSD-Schreibgate

Datum: 2026-08-25

Nach Einlegen einer FAT32-microSD und anschließendem Neustart meldete der reale
Tab5 `state=Ready`, `error=None`. Über den Touchscreen wurden START, mehrere
MARKER, Tabwechsel und STOP ausgeführt. Die Logger-Task blieb dabei unabhängig
von Display und Serial aktiv.

Realer Abschlussstatus:

```text
SPROTZ_LOGGER state=1 error=0 records=0 events=10 queue_drops=0
bytes=11152 file=/sprotz/g0_s0_131013204_0.dlog
```

Die Dateigröße ist exakt konsistent mit dem festen Format:
`32 Byte Header + 10 × 1112 Byte Eventrecord = 11152 Byte`. Enthalten sind ein
START-, acht MARKER- und ein STOP-Record. STOP schloss und flushte die Datei;
danach wechselte der Status von `Recording` zurück zu `Ready`. Es traten weder
Queueverlust noch Open-/Write-/Full-Fehler auf. Ein fehlendes Medium war zuvor
ebenfalls korrekt als `NoStorage` sichtbar und beeinflusste K409/KWP nicht.

Status des Speichergates: **TARGET-SD-WRITE-PASS**.

### Reales microSD-Rücklesegate

Die Karte wurde anschließend physisch am Entwicklungsrechner eingelesen. Die
reale Datei `/Volumes/SD/sprotz/g0_s0_131013204_0.dlog` hatte exakt 11152 Byte
und den SHA-256-Hash
`af0fe90085fee8c3447537072956ef44d3b360dabb364602341d251d94c3b73e`.
`tools/decode_sprotz_log.py` konvertierte sie ohne Schema-, Längen- oder
Feldordnungsfehler in Wide-CSV:

- 10/10 Records lesbar;
- genau ein START-, acht MARKER- und ein STOP-Record;
- monotone Eventtimestamps von `131013204` bis `197600200` us;
- alle 26 festen Feldpositionen je Record vorhanden;
- keine abgeschnittenen oder zusätzlichen Bytes.

Status des Rücklesegates: **TARGET-SD-READBACK-PASS**.

Noch offen ist ausschließlich ein Mehrminutenlauf mit laufender Digifant-ECU,
der echte Snapshotrecords, vollständige 26 Felder sowie unveränderte
KWP-/Drop-/Faultzähler während SD-Schreiben, MARKER und Tabwechsel belegt.

## V2-017 – Runtime-Integration und reale Abnahme

Datum: 2026-08-25

Runtime-Datenfluss:

```text
Tab5 IMU → ImuSampleRing (257 Slots / 256 nutzbar)
MeasurementSnapshot → LoggerSnapshotQueue
UI START/STOP/MARKER → LoggerCommandQueue
             └→ Logger-Task: je ein Pending-Element → LoggerTimeMerge → V1-Logger-Core
```

Der Merge läuft ausschließlich in der Logger-Task. Die ECU-/KWP-/Decoder- und
Snapshot-Pipeline wurde nicht verändert. Vor START und nach STOP verworfene
IMU-Samples werden bounded behandelt; Queue-Produzenten warten nicht.

Tatsächlich ausgeführte Nachweise:

- 28/28 Hosttests mit C++20 und Warnings-as-errors: **PASS**;
- 28/28 Hosttests mit ASan/UBSan: **PASS**;
- `check_pipeline_v2_015.sh`, `check_sprotz_logger.sh` und `git diff --check`:
  **PASS**;
- Targetcompile `esp32:esp32:m5stack_tab5`: **PASS**, 907534 Bytes Flash,
  154236 Bytes globale Variablen;
- Upload auf realen Tab5 `<serial-port>` mit Flash-Hashprüfung: **PASS**.

Reales ECU-/IMU-Gate:

- reale K409-/Digifant-Session: `generation=1`, `session=14`;
- Abschluss: 575 ECU-Frames, `frame_drops=0`, `rx_drops=0`,
  `parser_rejects=0`, `byte_fault=0`, `action_failures=0`;
- IMU-Sampling stabil mit 25 Hz; beim STOP `imu_samples=5515`,
  `imu_queue_drops=0`;
- Loggerabschluss: `state=1`, `records=330`, `events=5`,
  `snapshot_queue_drops=0`, `imu_queue_drops=0`,
  `command_queue_drops=0`;
- `events=5` entspricht START + 3 MARKER + STOP;
- Datei wurde durch STOP geflusht und geschlossen; Logger kehrte nach
  `Ready` zurück;
- während Logger-/SD-Pfad: keine ECU- oder IMU-Drops und keine KWP-Faults.

Die zeitliche Reihenfolge und der Tie-Break `START → MARKER → ECU_SNAPSHOT →
IMU_SAMPLE → STOP` sind durch `logger_time_merge_test` sowie die reale
gemeinsame `esp_timer_get_time()`-Basis abgedeckt. Die Marker sind manuelle
Zeitereignisse; automatische Sprotz-Erkennung ist nicht Bestandteil von V2-017.

Status: **V2-017 PASS**. V2-018 ist nachstehend vollständig abgenommen.

## V2-018 – DLOG V2 mit IMU-Records und Host-Rücklesung

Datum: 2026-08-25

Die aktuelle Firmware wurde mit Arduino-ESP32 `3.3.11` (ESP-IDF 5.5.5) auf
den realen Tab5 `<serial-port>` kompiliert, hochgeladen und per
Flash-Hash verifiziert. Die periodische SD-Prüfung erkannte die zunächst
fehlende Karte selbstständig als `NoStorage` und wechselte nach dem Einlegen
ohne START und ohne Reboot auf `Ready`.

Reales kombiniertes ECU-/IMU-Schreibgate:

```text
SPROTZ_LOGGER state=1 error=0 records=845 events=5
queue_drops=0 snapshot_queue_drops=0 imu_queue_drops=0 command_queue_drops=0
imu_samples=15406 bytes=1978642
file=/sprotz/g0_s0_80146537_0.dlog
```

Der Lauf dauerte 616,260 s. Währenddessen blieben KWP-/ECU- und IMU-Pfad
aktiv; es gab keinen Reboot, keinen USB-DWC-Assert und keine Logger-,
Snapshot-, IMU- oder Command-Drops. STOP schloss und flushte die Datei und
führte den Logger zurück nach `Ready`.

Die Karte wurde anschließend physisch am Host eingelesen. Die Datei hatte
1.978.642 Byte und SHA-256
`eeac1ad49d61d2c1f11deffd11cb6ac3aca90a4bcbf8ceb5004298a25a8adb22`.
`tools/decode_sprotz_log.py` validierte Header, Recordlängen, Payload-FNV-
Prüfsummen, Schema und Feldordnung ohne Fehler.

Rückleseergebnis:

- 16.257 Records: 1 IMU_ORIENTATION, 1 START, 3 MARKER, 15.406 IMU_SAMPLE,
  845 ECU_SNAPSHOT und 1 STOP;
- monotone gemeinsame Zeitbasis ohne Inversion;
- IMU-Intervalle 38.985–41.337 us, Mittelwert 40.000,021 us = 24,999987 Hz;
- IMU-Sequenzen 1961–17366 ohne Lücke;
- alle 845 ECU-Snapshots mit 26/26 ECU-Feldern;
- START/Marker/STOP zeitlich eindeutig im Recordstrom.

Host-Abnahme nach dem finalen Merge-/SD-Probe-Stand:

- vollständige C++20-Tests mit Warnings-as-errors: **PASS**;
- vollständige ASan-/UBSan-Suite: **PASS**;
- V2-Decoder-Test und reale Datei-Rücklesung: **PASS**.

Status: **V2-018 PASS**. V2-019 ist nachstehend vollständig abgenommen.

## Kartenpräsenz- und serielle Diagnosekorrektur

Datum: 2026-08-25

Die reine `cardType()`-/`totalBytes()`-Prüfung konnte nach physischem Entfernen
einer Karte einen stale Mount als `Ready` melden. Der Idle-Probeweg führt
deshalb alle zwei Sekunden einen kontrollierten `SD_MMC.end()`-/`begin()`-
Zyklus aus; während Recording bleibt der Logger alleiniger SD-Eigentümer und
wird nicht ungemountet. Die erneute Initialisierung bestätigt Karte und
`/sprotz`-Dateisystem tatsächlich.

Die serielle Diagnose meldet `storage_present=0/1` im Loggerstatus. Der Befehl
`SD_STATUS` liefert den zuletzt durch die Logger-Task geprüften Hardwarestatus.
Real am Tab5 geprüft:

- Karte draußen: `SD_STATUS present=0 logger_state=3 error=3`;
- Karte eingesteckt: automatischer Wechsel zu
  `state=1 error=0 storage_present=1` ohne START und ohne Reboot;
- KWP/ECU blieb während der wiederholten SD-Initialisierungsfehler aktiv.

Für die V2-019-Abnahme wurden zusätzlich `TAB 0` bis `TAB 3` als serielle
Testbefehle ergänzt. Die Tab-Anforderung wird im Display-Task ausgeführt und
greift nicht auf Logger-, KWP- oder IMU-Datenpfade zu.

## V2-019 – Gemeinsame ECU-/IMU-Logger-Abnahme

Datum: 2026-08-25

Reales finales Targetgate auf `<serial-port>` mit Arduino-ESP32
`3.3.11`/ESP-IDF `5.5.5`, anschließend physisches Host-Rücklesen:

```text
SPROTZ_LOGGER state=1 error=0 records=857 events=5
queue_drops=0 snapshot_queue_drops=0 imu_queue_drops=0 command_queue_drops=0
imu_samples=15648 storage_present=1 bytes=2008270
```

Während des 625,942-s-Laufs blieben KWP und ECU aktiv. Der Serialpfad meldete
durchgehend `ecu=1`, `frame_drops=0`, `rx_drops=0`, `parser_rejects=0`,
`byte_fault=0` und `action_failures=0`. `TAB 0`, `TAB 1`, `TAB 2` und `TAB 3`
wurden während Recording seriell angefordert und ausgeführt. Es gab keinen
Reboot, keinen USB-DWC-Assert und keine Sessionunterbrechung.

Die physisch am Host gelesene Datei
`/Volumes/SD/sprotz/g0_s0_43587025_0.dlog` hatte 2.008.270 Byte und SHA-256
`86c0cb6aaa82ba4994ab55aada38febf2559a1da005e979ab90a03d6952b108`.
Der V2-Konverter validierte alle Record-Header, Längen, FNV-Payload-Prüfsummen,
Schema- und Feldordnungen.

Rückleseergebnis:

- 16.511 Records: 1 IMU_ORIENTATION, 1 START, 15.648 IMU_SAMPLE,
  857 ECU_SNAPSHOT, 3 MARKER und 1 STOP;
- monotone gemeinsame Zeitbasis ohne Inversion;
- IMU-Intervalle 38.759–40.799 us, Mittelwert 40.000,015 us = 24,999990 Hz;
- IMU-Sequenzen ohne Lücke;
- 857/857 ECU-Snapshots mit 26/26 ECU-Feldern;
- zeitliche Ereignisfolge: START → MARKER → MARKER → MARKER → STOP;
- alle Logger-Dropzähler real `0`.

Status: **V2-019 PASS**. V2-016 bis V2-019 sind vollständig abgeschlossen.

Nach dem Hardwaregate wurden die reine serielle Reset-Debugausgabe entfernt
und die finale Firmware nochmals geprüft: `check_sprotz_logger.sh`,
`check_pipeline_v2_015.sh`, `git diff --check`, Targetcompile und Upload mit
Flash-Hashprüfung: **PASS**. Diese letzte Änderung verändert keinen Runtime-
oder Datenpfad.

## Refactoring-Gates R0–R4/R2 – 2026-08-25

Die bis einschließlich R4 sowie R2 ausgeführten Strukturänderungen sind
zusätzlich abgesichert durch:

- 28/28 C++-Hosttests mit C++20, `-Wall -Wextra -Wpedantic -Werror` und
  `-pthread` sowie den Python-DLOG-V2-Test: **PASS**;
- TSan für `sprotz_logger_test` (MPSC-Produzenten) und
  `imu_sample_ring_test` (Diagnosemailbox): **PASS**;
- `check_pipeline_v2_009.sh`, `_010.sh`, `_013.sh`, `_014.sh`, `_015.sh` und
  `check_sprotz_logger.sh`: **PASS**;
- finaler Arduino-Targetcompile für `esp32:esp32:m5stack_tab5`: **PASS**
  (917178 Bytes Flash, 156804 Bytes globale Daten).

R1/R2 wurden zuvor zusätzlich auf dem realen Target `<serial-port>`
über mehr als 60 Sekunden mit angeschlossener ECU beobachtet. KWP-Frames und
Gruppen 000–004 liefen mit `frame_drops=0`, `rx_drops=0`, `parser_rejects=0`,
`byte_fault=0` und `action_failures=0`; die IMU-Diagnose meldete gültige
Samples. R4 ändert den gebauten Fork nicht, daher ist hierfür kein neuer
ECU-Lauf erforderlich.

## R6-Processing-Gate – 2026-08-25

Der erste R6-Teil extrahiert ausschließlich Decoder-, `MeasurementModel`- und
Snapshot-Publikationsverantwortung in `src/processing_service.h`. Es wurde
keine neue Task angelegt; `processing_task_entry()` lädt nur den bestehenden
Runtime-Status, ruft einen bounded Pollschritt auf und verwendet weiterhin die
bisherigen Verzögerungen von 1/10 ms.

Die `.ino` wurde von 621 auf 580 Zeilen reduziert. Entfernt wurden nur die
lokalen Processing-Zustände, die Decoder-/Modellinstanzen, der Record-Wrapper
und der bisherige Processing-Pollkörper; Serial-, KWP-/Transport- und
Taskregistrierungsanteile blieben in der `.ino`.

Host-/Sanitizer- und Guard-Ergebnis:

- 29/29 C++-Hosttests einschließlich `processing_service_test`: **PASS**;
- ASan/UBSan für alle 29 Hosttests: **PASS**;
- relevante TSan-Tests für Processing, Logger-MPSC und IMU-Mailbox: **PASS**;
- DLOG-Test und alle Pipeline-/Logger-Guards: **PASS**;
- Arduino-Targetcompile: **PASS** (915822 Bytes Flash, 156812 Bytes globale
  Daten).

Reales ECU-Gate auf `<serial-port>`, 65 Sekunden nach Upload mit
verifiziertem Flash-Hash:

```text
KWP_SNAPSHOT count=157
last: generation=1 session=1 seq=95 k409=1 kwp=1 ecu=1
      frames=96 frame_drops=0 rx_drops=0 parser_rejects=0
      byte_fault=0 action_failures=0 faults=0
KWP_GROUPS 0,1,2,3,4
IMU_STATUS count=12, last samples=1757 seq=1756
SPROTZ_LOGGER last state=3 error=3 queue_drops=0
                         snapshot_queue_drops=0 imu_queue_drops=0
                         command_queue_drops=0 storage_present=0
RUNTIME_FAULT_LINES count=0
```

`snapshot_overwrites=94` im letzten Snapshot sind erwartete, gezählte
Overwrite-latest-Ereignisse der absichtlich langsamen bzw. nicht belegten
Snapshot-Blattconsumer. Sie sind kein Frame-/RX-/Logger-/IMU-Queue-Drop und
entsprechen dem bestehenden V2-015-Vertrag. Die SD-Karte war bei diesem Gate
nicht eingelegt (`storage_present=0`); der Loggerpfad lief ohne Queueverluste,
die IMU-Diagnose blieb aktiv.

Status: **R6-Processing PASS**. Der separate Serial-Teil ist im folgenden
Abschnitt dokumentiert; KWP-Runtime sowie R7–R9 wurden nicht begonnen.

## R6-Serial-Gate – 2026-08-25

Der zweite R6-Teil extrahiert die bisherige Serial-Tasklogik in
`src/serial_consumer.h`. Die Komponente besitzt ausschließlich Parser- und
Formatierungszustand sowie Referenzen auf die bestehenden sicheren Mailboxen
und Commandqueue. `serial_snapshot_task_entry()` bleibt die einzige Task und
enthält nur den Poll-Aufruf und das unveränderte 10-ms-Delay. Die `.ino` sank
von 580 auf 456 Zeilen.

Hosttests und Golden-Abdeckung:

- 30/30 C++-Hosttests einschließlich `serial_consumer_test`: **PASS**;
- Kommandos `START`, `STOP`, `MARKER`, `TAB 0..3`, `STATUS`, `SD_STATUS`,
  unbekanntes und überlanges Kommando geprüft;
- Golden-/Regression-Prüfungen für `SERIAL_CMD`, `SD_STATUS`,
  `SPROTZ_LOGGER`, `KWP_SNAPSHOT`, `KWP_FIELD` und `IMU_STATUS`: **PASS**;
- ASan/UBSan für alle 30 Hosttests: **PASS**;
- relevante TSan-Tests (SerialConsumer, Processing, Logger, IMU): **PASS**;
- DLOG-Test und alle Architekturguards: **PASS**.

Target-/ECU-Gate auf `<serial-port>`, 65 Sekunden nach verifiziertem
Upload; dabei wurden `STATUS`, `SD_STATUS`, `TAB 1` und `TAB 3` real gesendet:

```text
KWP_SNAPSHOT count=145
last: generation=1 session=1 seq=96 k409=1 kwp=1 ecu=1
      frames=97 frame_drops=0 rx_drops=0 parser_rejects=0
      byte_fault=0 action_failures=0 faults=0
KWP_GROUPS 0,1,2,3,4
IMU_STATUS count=13, last samples=1877 seq=1876
SPROTZ_LOGGER state=1 error=0 queue_drops=0
              snapshot_queue_drops=0 imu_queue_drops=0 command_queue_drops=0
              storage_present=1
SERIAL: SD_STATUS present=1 logger_state=1 error=0
SERIAL: SERIAL_CMD TAB 1 QUEUED
SERIAL: SERIAL_CMD TAB 3 QUEUED
RUNTIME_FAULT_LINES count=0
```

`snapshot_overwrites=83` waren erwartete Latest-Mailbox-Overwrites der
langsamen Blattconsumer, keine Frame-/RX-/Logger-/IMU-Drops. Ausgabeformate,
Kommandosemantik, Queuearten, Mailboxen, Taskpriorität 1 und das 10-ms-Delay
blieben unverändert. Der Targetcompile lag bei 916304 Bytes Flash und 157012
Bytes globalen Daten.

Ein separater mutierender `START`/`MARKER`/`STOP`-Smoke mit eingelegter SD-Karte
reproduzierte zusätzlich einen `xTaskPriorityDisinherit`-Assert im Target-
Loggerpfad. Die Serial-Extraktion enthält keine Loggeränderung und der
Fehler tritt im nicht-mutierenden Gate nicht auf; er bleibt als bestehendes
Target-/Loggerproblem außerhalb R6 offen und wurde nicht durch eine unzulässige
Loggeränderung kaschiert.

Status zum Abschluss von R6: **R6 vollständig abgeschlossen**. Die späteren
Schritte R8 und R9 sind in den nachfolgenden Abschnitten separat dokumentiert;
R7 bleibt wegen des offenen Target-Assert-Gates BLOCKED.

## Untersuchung `xTaskPriorityDisinherit` – 2026-08-25

Die anschließende isolierte Targetanalyse ist in
`TARGET_ASSERT_XTASKPRIORITYDISINHERIT.md` dokumentiert. Der zuvor beobachtete
Assert ließ sich in kontrollierten Wiederholungen einschließlich acht
instrumentierter `START -> 3 x MARKER -> STOP`-Zyklen nicht erneut auslösen;
auch die vorgeschaltete Mutex-Owner-Diagnose meldete keinen Mismatch. Es wurde
kein Produktivfix vorgenommen. Entscheidung: **NOT-REPRODUCED**, **R7 BLOCKED**
bis ein Panic-Backtrace den konkreten Mutex und Owner-/Lifecycle-Pfad belegt.

## R8 – mechanischer Header-/Implementierungsschnitt – 2026-08-25

Vor der Änderung wurden Includes, öffentliche Typen und Ausgangsgrößen
erfasst. `measurement_snapshot.h` hatte 174 Zeilen, `sprotz_logger.h` 582 und
`display_ui.h` 584. Das DLOG-Goldenfixture hatte 1.144 Byte und SHA-256
`204e9ec95688c5bb76018d42d54f46516f0269f8d3f4c5a4d75e2a187f588b60`.

Der mechanische Schnitt erzeugt folgende flache Verantwortlichkeiten:

- `measurement_snapshot_types.h`: Enums, `MeasurementField` und
  `MeasurementSnapshot` (66 Zeilen);
- `measurement_snapshot_mailbox.h`: `LatestSnapshotMailbox` und
  `SnapshotConsumerFanout` (118 Zeilen);
- `measurement_snapshot.h`: kompatibler Umbrella (6 Zeilen);
- `logger_types.h`: Logger-Enums, Command und Status (49 Zeilen);
- `logger_channels.h`: SPSC-/MPSC-Queues, Aliase, Statusmailbox und Fanout
  (214 Zeilen);
- `sprotz_log_format.h`: `LogSink` sowie DLOG-V1/V2-Encoder (176 Zeilen);
- `sprotz_logger_core.h`: unveränderter `SprotzLoggerCore` (175 Zeilen);
- `sprotz_logger.h`: kompatibler Umbrella (7 Zeilen);
- `display_ui.h`: unveränderte Klasse/API und Objektlayout (106 Zeilen);
- `display_ui.cpp`: mechanisch verschobene Methodenkörper (529 Zeilen).

Direkte Includes wurden auf den jeweils benötigten DTO-, Mailbox- oder
Logger-Channel-Header eingegrenzt. Die Architekturguards wurden an die seit R6
reale `ProcessingService`-Ownership und an die neuen Loggerdateien angepasst.
Sie prüfen weiterhin quellbaumweit und zusätzlich explizit den einzigen
Decoderowner sowie die Snapshot-/Loggerpublikation; kein Guard wurde
abgeschwächt.

Verifikation:

- vollständige Hostsuite: **30/30 PASS** mit C++20 und Warnings-as-errors;
- vollständige Hostsuite mit ASan/UBSan: **30/30 PASS**;
- relevante TSan-Läufe für Snapshotmailbox, Logger-MPSC/Core, IMU-Ring,
  ProcessingService, SerialConsumer und V2-015-Decoupling: **6/6 PASS**;
- DLOG-V2-Decodertest und `check_sprotz_logger.sh`: **PASS**;
- Architekturguards V2-009, V2-010, V2-013, V2-014 und V2-015: **PASS**;
- finales DLOG-Fixture vor/nach R8: jeweils 1.144 Byte, byteidentisch und
  identischer SHA-256;
- Arduino-Targetcompile `esp32:esp32:m5stack_tab5`: **PASS**, 917530 Byte
  Flash und 157012 Byte globale Daten;
- Upload/Flashverifikation auf `<serial-port>`: **PASS**;
- realer Display-/SD-Smoke: Displayinitialisierung mit `action_failures=0`,
  `TAB 0` bis `TAB 3` akzeptiert, SD `storage_present=1`, ein
  `START -> MARKER -> STOP`-Zyklus geschlossen mit `state=1`, `error=0`,
  13 Snapshotrecords, 3 Events und allen Logger-/IMU-Dropzählern 0;
- während des Smokes ECU aktiv (`k409=1`, `kwp=1`, `ecu=1`) und
  `frame_drops=0`, `rx_drops=0`, `parser_rejects=0`, `byte_fault=0`,
  `action_failures=0`, `faults=0`; kein Assert/Panic.

Öffentliche C++-Typnamen, Methodensignaturen, Klassenlayout, Queuekapazitäten,
Mailboxsemantik, Merge-Reihenfolge und Snapshotstruktur blieben unverändert.
Die `DisplayUi`-Methoden sind nun out-of-line gelinkt; deshalb wuchs das
Firmwareimage gegenüber dem R6-Ausgangsbuild um 1.226 Byte. Das ist keine
fachliche API- oder Datenformatänderung. DLOG-V1/V2-Bytes blieben nachweislich
unverändert.

Ein formales 60-s-ECU-Gate war für den rein mechanischen Schnitt nicht
erforderlich. R8 ist **PASS und abgeschlossen**. R7 bleibt ausdrücklich
**BLOCKED**. Die anschließende R9-Test-/Schattenbereinigung ist im folgenden
Abschnitt dokumentiert; eine produktive Runtimeverdrahtung wurde dabei nicht
freigegeben.

## R9 – Schattenabstraktionen bereinigt – 2026-08-25

Die Vorher-/Nachher-Vertragsmatrix liegt in
`../docs/archive/refactoring/R9_CONTRACT_MATRIX.md`. Die fünf untersuchten Typen waren nicht Teil der
ausgelieferten Runtime: `GenerationTracker` wurde nicht von den USB-Callbacks
verwendet, `OperationLifecycle` und `KwpRunnerModel` modellierten nicht die
produktive KWP-Verdrahtung, `KwpReceiveService` war nur ein Wrapper des
Schattenmodells und `UiState` war ein ungenutzter Include-/Mailboxvorläufer.

Entfernt wurden ausschließlich diese ungenutzten Header:

- `src/transport_operation_lifecycle.h`
- `src/kwp_receive_service.h`
- `src/kwp_runner_model.h`
- `src/ui_state.h`

`GenerationTracker` wurde aus `src/k409_device_filter.h` entfernt; die
produktive Funktion `k409::matches()` und ihr Test bleiben bestehen. Die
Schatten-Tests wurden nicht ersatzlos gestrichen:

- `kwp_measurement_session_lifecycle_test.cpp` prüft reale Session-
  Generation/Token-Korrelation und Disconnect-Verhalten;
- `kwp_protocol_core_token_test.cpp` prüft stale Completion direkt am realen
  `KwpProtocolCore`;
- `snapshot_runtime_boundary_test.cpp` prüft den produktiven Snapshot-Fanout;
- `kwp_byte_engine_test.cpp` prüft weiterhin die produktive Byte-Engine ohne
  Receive-Service-Wrapper.

Die Hosttestzahl blieb unverändert bei 30 (vorher/nachher). Verifikation:

- C++20 Hosttests mit Warnings-as-errors: **30/30 PASS**;
- ASan/UBSan: **30/30 PASS**;
- relevante TSan-Tests (`snapshot_mailboxes`, `sprotz_logger`,
  `imu_sample_ring`, `processing_service`, `serial_consumer`,
  `v2_015_decoupling`): **6/6 PASS**;
- DLOG-V2-Test: **PASS**;
- Architekturguards V2-009, V2-010, V2-013, V2-014, V2-015 und
  `check_sprotz_logger.sh`: **PASS**;
- Arduino-Targetcompile `esp32:esp32:m5stack_tab5`: **PASS**, 917530 Byte
  Flash, 157012 Byte globale Daten.

Es wurden keine produktiven Runtimeobjekte neu verdrahtet, keine Task-,
Queue-, KWP-, Transport- oder Loggerverträge geändert und keine
DLOG-/Snapshotdatenformate verändert. Die produktive Runtime und die
Targetgröße bleiben gegenüber dem R8-Build unverändert (917530 Byte Flash,
157012 Byte globale Daten); ein separater Bit-Hashvergleich des ELF wurde nicht
durchgeführt. Ein reales ECU-Gate war daher nicht erforderlich.

R9 ist abgeschlossen. Verbleibende technische Schulden sind vor allem die
offene KWP-Targetkapselung (R7) und der nicht reproduzierte
`xTaskPriorityDisinherit`-Assert im Logger-/SD-Pfad. R7 bleibt ausdrücklich
**BLOCKED**; R9 liefert keine Freigabe für R7.

## R5 – Dummyconsumer nur im expliziten Stressbuild – 2026-08-25

R5 entfernt die historischen Bluetooth-/Web-Dummyconsumer aus dem normalen
Produktionsbuild. Der Compile-Time-Schalter `V2_015_TARGET_STRESS` ist
standardmäßig `0`. Dann werden weder `bluetooth_snapshot_dummy` noch
`web_snapshot_dummy` als Task angelegt und `SnapshotConsumerFanout` publiziert
nur an Serial und Display. Die optionalen Mailboxen und ihre Fanout-Methoden
bleiben als bewährte, hosttestbare Erweiterungsgrenze vorhanden, sind aber im
Produktionsbuild inaktiv. Queue-/Mailboxtypen und Slotsemantik wurden nicht
geändert.

Bei `V2_015_TARGET_STRESS=1` werden beide langsamen Dummyconsumer, ihre
Taskhandles und die optionale Snapshotpublikation aktiviert. Der bestehende
500-ms-Displaystall bleibt ausschließlich in diesem Stressbuild aktiv. Damit
bleiben die V2-015-/Decoupling-Nachweise erhalten, ohne Bluetooth- oder
Webfunktion zu implementieren.

Änderungen:

- `M5Tab5_Digifant_Analyzer.ino`: optionale Dummyobjekte, Taskhandles und
  `xTaskCreate`-Aufrufe unter den Stressschalter gestellt;
- `src/measurement_snapshot_mailbox.h`: explizite Aktivierung optionaler
  Consumer und Overwritezählung nur für aktivierte Consumer;
- `src/processing_service.h`: bestehende Snapshottelemetrie nutzt die
  Fanoutzählung, ohne Decoder-/Model-/Queueverhalten zu verändern;
- `tests/v2_015_decoupling_test.cpp`: Stressfixture aktiviert optionale
  Consumer explizit;
- `tests/snapshot_runtime_boundary_test.cpp`: Produktionsfanout prüft, dass
  optionale Consumer deaktiviert bleiben;
- `tools/check_pipeline_v2_015.sh`: prüft Produktions-/Stressschalter und
  weiterhin die einzige Pipeline.

Verifikation:

- vollständige Hosttests: **30/30 PASS**;
- ASan/UBSan: **30/30 PASS**;
- relevante TSan-Tests: **6/6 PASS**;
- DLOG-Test: **PASS**;
- Architekturguards V2-009, V2-010, V2-013, V2-014, V2-015 und Loggerguard:
  **PASS**;
- Produktions-Targetcompile: **PASS**, 917220 Byte Flash, 157020 Byte globale
  Daten;
- Stress-Targetcompile mit `-DV2_015_TARGET_STRESS=1`: **PASS**, 917596 Byte
  Flash, 157028 Byte globale Daten.

Der reale Produktionslauf auf `<serial-port>` dauerte 65 Sekunden und
zeigte Generation 1/Session 1 mit `k409=1`, `kwp=1`, `ecu=1`, Gruppenbetrieb,
`frame_drops=0`, `rx_drops=0`, `action_failures=0`, Logger-/IMU-Drops 0 sowie
`storage_present=1`. Der finale Produktionssnapshot hatte
`snapshot_overwrites=2`; diese stammen nur aus den realen Serial-/Display-
Mailboxen, nicht aus Dummyconsumern.

Der reale Stresslauf dauerte ebenfalls 65 Sekunden. Er blieb bei
`k409=1`, `kwp=1`, `ecu=1` und allen relevanten KWP-/Parser-/Actionfehlern 0;
Logger-/IMU-Drops blieben 0. Die absichtlich langsamen optionalen Consumer
erzeugten erwartete `snapshot_overwrites` (im Lauf bis 88), ohne KWP,
Processing, Logger oder IMU zu beeinflussen.

R5 ist damit **PASS und abgeschlossen**. Der `xTaskPriorityDisinherit`-Assert
wurde nicht verändert oder mitigiert. R7 bleibt ausdrücklich **BLOCKED**.

## Bedienung realer Sprotz-Messfahrten – 2026-08-25

Die Logging-Bedienung trennt nun ausdrücklich `LOG_START`, `LOG_STOP`,
`SPROTZ_START`, `SPROTZ_STOP` und `MARKER`. Die Kurzbefehle `START`/`STOP`
bleiben als kompatible Aliase erhalten. `SPROTZ_START` und `SPROTZ_STOP`
ändern ausschließlich den Sprotz-Ereigniszustand; ECU- und IMU-Aufzeichnung
bleiben zwischen `LOG_START` und `LOG_STOP` kontinuierlich aktiv. Ungültige
Übergänge (z. B. ein zweites `SPROTZ_START` oder `SPROTZ_STOP` ohne aktives
Ereignis) werden deterministisch ignoriert. Ein aktives Ereignis wird im
Display und in `SPROTZ_LOGGER` als `sprotz_active=1` angezeigt.

Die Zustands-/Markerlogik liegt in `src/sprotz_event_state.h` und wird im
bestehenden `SprotzLoggerCore` ausgeführt. Für Sprotz-Start, Sprotz-Stop und
freie Marker werden ausschließlich vorhandene Markerrecords und dieselbe
monotone Zeitbasis verwendet; DLOG-V1/V2-Bytes und Merge-Reihenfolge bleiben
unverändert. `tests/sprotz_event_state_test.cpp` prüft Zustandsübergänge,
ignorierte Doppelbefehle, Markerreihenfolge und das deterministische Schließen
eines offenen Ereignisses bei `LOG_STOP`.

Verifikation dieses Schritts:

- Hosttests: **31/31 PASS**;
- ASan/UBSan: **31/31 PASS**;
- relevante TSan-Tests: **7/7 PASS**;
- DLOG-Test, Architekturguards und Pipelineguards: **PASS**;
- Arduino-Produktionscompile: **PASS**, 918076 Byte Flash, 157028 Byte globale
  Daten;
- Arduino-Stresscompile: **PASS**, 918446 Byte Flash, 157036 Byte globale
  Daten.

Der reale Standtest lief mit der Produktionsfirmware, SD, K409/Digifant-ECU und
IMU. Ausgeführt wurden `LOG_START`, zwei gültige
`SPROTZ_START`/`SPROTZ_STOP`-Paare, ein zusätzlicher `MARKER`, absichtliche
Doppelbefehle sowie Tabwechsel und `LOG_STOP`. Die Doppelbefehle erzeugten
keine zusätzlichen Ereignisse. Der Logger endete mit `state=1`,
`sprotz_active=0`, `events=7`, `queue_drops=0`,
`snapshot_queue_drops=0`, `imu_queue_drops=0`, `command_queue_drops=0` und
`error=0`. Während des gesamten Laufs blieben `kwp=1`, `ecu=1`,
`frame_drops=0`, `rx_drops=0`, `parser_rejects=0`, `byte_fault=0`,
`action_failures=0` und `faults=0`; KWP-Snapshots und IMU-Samples liefen vor,
während und nach der Session weiter.

Die vom Logger gemeldete Datei war `/sprotz/g0_s0_86260534_0.dlog` mit
`records=29`, `events=7`, `bytes=63518` und `imu_samples=458`. Ein
nachträglicher Host-Readback/Decoderlauf der physischen SD-Datei war in dieser
Umgebung nicht möglich, da die vom Target verwendete SD-Karte nicht als
Host-Volume unter `/Volumes` verfügbar war. Die zeitliche Markerreihenfolge
ist durch den Host-Regressionstest und die Target-Ereigniszähler abgesichert;
ein unabhängiger Byte-/Datei-Goldenvergleich der konkreten Standtestdatei
steht noch aus.

KWP, Processing, IMU-Sampling, Logger-Merge, Queueverträge und R7 wurden nicht
geändert. Der nicht reproduzierte `xTaskPriorityDisinherit`-Assert bleibt ein
separates Beobachtungsrisiko.

## DLOG V2 Event-Subtypen – 2026-08-25

V2-`MARKER`-Records sind weiterhin `kind=MARKER` und behalten Record- und
Hauptversion `2`. Neue Marker verwenden einen FNV-geschützten, zweibyteigen
Payload: `01 01` = `SPROTZ_START`, `01 02` = `SPROTZ_STOP`, `01 03` = freier
`MARKER`. `payloadLength=0` bleibt der gültige historische V2-Fall und wird
vom Hostkonverter als `MARKER_LEGACY` ausgegeben. DLOG V1 und der V2-Header
bleiben byteunverändert. Das ungeschützte Header-`flags`-Byte trägt keine
Eventsemantik.

Der CSV-Konverter ergänzt `event_subtype`; Nicht-Marker bleiben leer.
Unbekannte, strukturell gültige Payloads werden explizit als
`UNKNOWN_EVENT_SCHEMA_<n>` beziehungsweise `UNKNOWN_EVENT_SUBTYPE_<n>`
ausgegeben. Eine Marker-Payloadlänge ungleich 0 oder 2 ist ein Decodefehler;
eine beschädigte Payload wird weiterhin durch die bestehende FNV-Prüfung
abgelehnt.

Neue Golden-/Roundtrip-Prüfungen decken Legacy-V2-Marker, alle drei neuen
Subtypen, unbekanntes Schema, unbekannten Subtyp, falsche Payloadlänge und
beschädigte Payloadchecksumme ab. Die vollständige Hostsuite (**31/31**),
ASan/UBSan (**31/31**), relevante TSan-Tests (**7/7**), DLOG-Test und alle
Architekturguards sind PASS. Der Produktions-Targetcompile ist PASS (918200
Byte Flash, 157028 Byte globale Daten). Die vor der Erweiterung erzeugte reale
Datei `g0_s0_86260534_0.dlog` dekodiert weiterhin vollständig und enthält fünf
`MARKER_LEGACY`-Records.

Der reale Target-Smoke erzeugte mit
`LOG_START → SPROTZ_START → MARKER → SPROTZ_STOP → LOG_STOP` die Datei
`g0_s0_80838060_0.dlog`. Die SD-Quelle und die damals erzeugte Hostkopie
hatten identische SHA-256-Hashes. Die konkrete Hostkopie und ihre Rohdaten
werden nicht im öffentlichen Repository verteilt. Der unabhängige
Hostreadback war DLOG V2, 27906 Byte, 214 Records:

- 13 `ECU_SNAPSHOT`, 195 `IMU_SAMPLE`, 1 `IMU_ORIENTATION`;
- 1 `START`, 3 `MARKER`, 1 `STOP`;
- monotone Zeitachse von 80838060 bis 89948865 µs, gültige Recordlängen und
  FNV-Checksummen;
- aus den DLOG-Bytes allein: `SPROTZ_START` bei 82875873 µs, freier `MARKER`
  bei 84878066 µs und `SPROTZ_STOP` bei 86911775 µs;
- ECU-/IMU-Daten vor dem Sprotz-Ereignis (3/44), währenddessen (7/82) und
  danach (3/69).

Damit ist die neue Messfahrtdatei selbstbeschreibend; die Event-Subtyp-
Erweiterung ist auf Target und Host verifiziert. KWP, Processing, IMU, Logger-
Merge, Queueverträge, R7 und der offene `xTaskPriorityDisinherit`-Befund wurden
nicht verändert.
