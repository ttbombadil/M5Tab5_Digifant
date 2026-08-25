# Targetanalyse `xTaskPriorityDisinherit`

Stand: 2026-08-25  
Entscheidung: **NOT-REPRODUCED**  
Folgeentscheidung: **R7 BLOCKED**

## Ausgangsbefund und Scope

Vor dieser Untersuchung war der Assert zweimal auf dem realen ESP32-P4 mit
eingelegter SD-Karte beobachtet worden: einmal nach einem `MARKER` im
Loggerzustand `Ready`, einmal bei einem mutierenden
`START -> MARKER -> STOP`-Smoke. Die sichtbare Meldung lautete:

```text
assert failed: xTaskPriorityDisinherit tasks.c:5156
(pxTCB == pxCurrentTCBs[ xPortGetCoreID() ])
```

Untersucht wurde Arduino-ESP32 3.3.11 auf Basis von ESP-IDF 5.5.5. R7-R9,
KWP-/Transport-State-Machines, Taskprioritäten, Timings, Queueverträge,
DLOG-Formate sowie ECU-/IMU-Merge-Semantik wurden nicht verändert.

## Assert-Stelle und verletzte Invariante

In ESP-IDF 5.5.5 steht der Assert in
[`tasks.c`, `xTaskPriorityDisinherit()`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/freertos/FreeRTOS-Kernel/tasks.c#L5141-L5158).
Beim Rückgeben eines Priority-Inheritance-Mutex muss der im Mutex gespeicherte
Holder-TCB die auf dem aktuellen Core laufende Task sein:

```c
configASSERT(pxTCB == pxCurrentTCBs[portGET_CORE_ID()]);
```

Der Aufruf erfolgt beim Mutex-`give` aus
[`queue.c`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/freertos/FreeRTOS-Kernel/queue.c#L2451-L2471),
bevor `xMutexHolder` gelöscht wird. Verletzt ist daher nicht eine
Loggerformat-Invariante, sondern die Mutex-Ownership-Invariante: gespeicherter
Holder und freigebende Task stimmen nicht überein. Direkte Ursachenklassen sind
ein `give/unlock` aus einer fremden Task, ein bereits ungültiger/wiederverwendeter
Mutex oder Speicherbeschädigung von Queue/TCB. Bei einem rekursiven Mutex prüft
`xQueueGiveMutexRecursive()` den Owner bereits vor dem eigentlichen Give; ein
Assert dort würde zusätzlich eine Zustandsbeschädigung oder ein sehr enges
Lifecycle-/SMP-Problem voraussetzen.

## Verfolgter Runtime- und Lockpfad

`SerialConsumer` und `DisplayUi` erzeugen trivially-copyable
`LoggerCommand`-Werte. `LoggerCommandQueue` ist eine feste MPSC-Queue mit
`atomic_flag`; sie enthält keinen FreeRTOS-Mutex. Nur der Task
`sprotz_logger` konsumiert die Queue und ruft `SprotzLoggerService::poll()` auf.
`LoggerTimeMerge` ordnet die schon vorhandenen Zeitstempel; es verschiebt keine
SD-Operation in eine andere Task.

`SprotzLoggerCore` führt aus:

- `START`: `SdMmcLogSink::open()`, Header, Orientierung, START-Record;
- `MARKER`: nur in `Recording`, Record und `flush()`;
- `STOP`: nur in `Recording`, STOP-Record, `flush()`, `close()`, dann `Ready`.

`SdMmcLogSink`, `SprotzLoggerCore` und `fs::File` sind statische Bestandteile
des globalen `SprotzLoggerService`; es gibt während `STOP` keine Destruktion.
`SdMmcLogSink::close()` ist idempotent. Arduino `File::close()` schließt den
VFS-Handle und setzt seinen Shared-Handle auf null. Ein zweites `STOP` wird
bereits durch `state != Recording` verworfen. Ein doppelter Close/Flush- oder
Unlock-Pfad ist im Projektcode daher nicht vorhanden.

Relevante Synchronisation:

| Lock | Take/Give | Task-/Lifecycle-Befund |
|---|---|---|
| `LoggerCommandQueue::operationLock_` | atomarer Try-Lock | Serial/UI/Logger, kein FreeRTOS-Mutex und daher nicht direkter Assert-Auslöser |
| Arduino-`FSImpl::_mtx` | rekursiver Mutex, RAII in `open/exists/mkdir/...` | Take und Give innerhalb desselben API-Aufrufs; alle Projektaufrufe aus `sprotz_logger` |
| ESP-IDF VFS-FAT-Kontextlock | Standardmutex je VFS-Aufruf | synchroner Acquire/Release im selben Aufruf und Logger-Task |
| FatFs-Volume-Mutex | Standardmutex in `f_open/f_write/f_sync/f_close/f_getfree` | synchrones Take/Give; der aufrufende Projekt-Owner ist ausschließlich `sprotz_logger` |
| SDMMC-Host/Transfer-Synchronisation | ESP-IDF-intern | synchrone `SD_MMC`-/FatFs-Aufrufe; kein Projekt-Callback schließt die Datei |
| HWCDC-TX-Lock | Standardmutex in `Serial.write/flush` | nicht Teil des SD-Owners; wegen möglicher fremder Core-Logs und Backpressure separat geprüft |

`SD_MMC.end()` wird nur beim idle Storage-Probe ausgeführt. Während
`Recording` ruft `probe(false)` ausdrücklich kein Unmount auf. Der Projekt-Scan
ergab keinen zweiten `SD_MMC`-Nutzer. Logger- und Taskobjekte werden nicht
gelöscht; ihre Handles bleiben über die gesamte Laufzeit stabil.

Der frühere Einzelbefund mit `MARKER` bei `Ready` ist wichtig: Ein solcher
Marker wird von `LoggerTimeMerge` verworfen und erreicht weder
`SdMmcLogSink::flush()` noch `close()`. Er belegt deshalb gerade nicht, dass
`STOP` oder das DLOG-Dateiende die Assert-Ursache ist.

## Kontrollierte Targetläufe

Target: realer M5Stack Tab5/ESP32-P4, `/dev/cu.usbmodem2101`, eingelegte
microSD, K409/Digifant-ECU und IMU aktiv. Produktionsbuild:
916304 Byte Flash, 157012 Byte globale Daten.

| Variante | Ergebnis |
|---|---|
| `START`, anschließend 50 s | kein Assert |
| frischer Upload, `START`, 10 s, `STOP`, Gesamtlauf 40 s | kein Assert |
| frischer Upload, `MARKER` bei `Ready`, Gesamtlauf 35 s | kein Assert; keine Dateioperation |
| vier Serial-Zyklen, je `START -> MARKER -> MARKER -> STOP`, 58 s | kein Assert |
| `START -> MARKER -> STOP`, dazwischen 15 s keine Host-Reads | kein Assert; Serial-Backpressure ohne Logger-Rückwirkung |
| instrumentiert: acht Serial-Zyklen, je drei Marker, 92 s/40 Kommandos | kein Assert, kein erkannter Mutex-Owner-Mismatch |

Die temporäre Diagnose ersetzte linkzeitig `xQueueGenericSend()` durch einen
dünnen Wrapper. Unmittelbar vor jedem Semaphore-/Mutex-Give wurden für echte
Mutexe gespeicherter Holder und `xTaskGetCurrentTaskHandle()` verglichen. Nur
im Fehlerfall wären Queueadresse, Tasknamen/-handles, Core, Stack-High-Water-
Mark und Return-PC per ROM-Ausgabe gemeldet worden. Das Instrument erzeugte
in 92 s **0** `MUTEX_OWNER_MISMATCH`-Ereignisse und wurde danach vollständig
entfernt.

Am Ende des instrumentierten Laufs:

```text
KWP_SNAPSHOT generation=1 session=1 seq=157 k409=1 kwp=1 ecu=1
frames=158 frame_drops=0 rx_drops=0 parser_rejects=0
byte_fault=0 action_failures=0 faults=0
SPROTZ_LOGGER state=1 error=0 records=9 events=5
queue_drops=0 snapshot_queue_drops=0 imu_queue_drops=0
command_queue_drops=0 storage_present=1
SPROTZ_STACK_FREE_WORDS=10860
IMU_STATUS samples=3018 seq=3017 validity=7
```

Nicht autonom ausführbar waren echte Touch-only- und gemischte Touch/Serial-
Folgen sowie eine physisch abgetrennte ECU bzw. IMU. Diese Varianten dürfen
nicht durch Produktcode-Schalter simuliert werden, weil das die zu prüfende
Runtime und Hardwaretopologie verändern würde.

## Bewertung und verbleibende Hypothesen

Die aktuelle Entscheidung ist **NOT-REPRODUCED**, nicht
`ROOT-CAUSE-CONFIRMED`. Die vorangegangenen Assert-Beobachtungen bleiben real,
aber ohne Panic-Backtrace oder Owner-Mismatch-Datensatz lässt sich der konkrete
Mutex nicht belastbar zuordnen. Deshalb wurde kein Fix implementiert.

Verbleibende, noch nicht bewiesene Ursachenklassen:

1. transienter Fremd-Task-Give oder Mutex-Lifecycle-Fehler in einem
   Target-/Bibliothekspfad (VFS/FatFs/SDMMC oder USB-CDC),
2. vorherige Speicherbeschädigung eines Queue-/Mutex- oder TCB-Objekts,
3. eine nur mit realer Touch/Serial-Konkurrenz oder anderer Hardwaretopologie
   erreichbare Reihenfolge.

Gegen einen projektspezifischen `STOP`-/Close-Fehler sprechen der eindeutige
Single-Task-Owner, die idempotenten Zustandsgrenzen, der frühere Assert nach
einem wirkungslosen Marker sowie die nun wiederholt erfolgreichen realen
Close-Zyklen. Das reicht aber nicht, um einen tieferen FatFs-/SDMMC-Fehler
auszuschließen.

## Nächster zulässiger Diagnoseschritt

Beim nächsten Auftreten muss die Panic-Ausgabe einschließlich Backtrace
vollständig erhalten und mit der ELF-Datei desselben Builds dekodiert werden.
Zusätzlich ist die oben verwendete Give-Instrumentierung einzuschalten. Danach
sind die noch offenen Varianten einzeln auszuführen: Touch-only, gemischt,
ECU physisch getrennt und IMU physisch/deklarativ deaktiviert, jeweils ohne
weitere gleichzeitige Änderung.

Bis ein konkreter Mutex und der fehlerhafte Owner-/Lifecycle-Pfad so belegt
sind, gilt: **kein spekulativer Produktivfix und R7 BLOCKED**.
