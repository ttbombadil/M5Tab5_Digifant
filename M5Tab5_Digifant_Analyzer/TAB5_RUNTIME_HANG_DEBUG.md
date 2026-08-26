# Tab5 Runtime-Hänger – Debug-Untersuchung

Status: **NOT-REPRODUCED / OPEN, beobachtetes Targetproblem**. Es gibt keinen
Produktivfix. K409 bleibt für die Diagnose abgezogen; Logger- und DLOG-Pfad
werden nicht verändert.

## Bereits belastbarer Befund

Im beobachteten Hängezustand blieb `/dev/cu.usbmodem21101` am Host
enumeriert. Ein 55-s-Hostversuch mit 21 `TAB n`- und sechs `STATUS`-Befehlen
erhielt keine einzige Anwendungsausgabe. Ein reiner Touchfehler erklärt den
Befund daher nicht; offen sind insbesondere ein blockierter Serialtask, ein
USB-CDC-Problem, ein gemeinsamer Lock oder ein Scheduler-/Speicherfehler.

## Debug-Build

Der Debug-Build ist strikt opt-in und verändert weder Produktionsbinary noch
KWP-, Queue-, Logger- oder DLOG-Verträge:

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 \
  --build-property 'compiler.cpp.extra_flags=-DTAB5_RUNTIME_DEBUG=1 -DTAB5_RUNTIME_DEBUG_WATCHDOG=1' \
  M5Tab5_Digifant_Analyzer
arduino-cli upload --fqbn esp32:esp32:m5stack_tab5 --port /dev/cu.usbmodem21101 \
  --build-property 'compiler.cpp.extra_flags=-DTAB5_RUNTIME_DEBUG=1 -DTAB5_RUNTIME_DEBUG_WATCHDOG=1' \
  M5Tab5_Digifant_Analyzer
```

`DEBUG_STATUS` wird ausschließlich im Serial-Task beantwortet. Es liefert
bounded RAM-Zustand: Heartbeat, letzte Zeit, Phase, maximale Poll-Dauer und
Stack-High-Water-Mark für Display, Serial, Processing, IMU, Logger und Arduino
Runtime; außerdem Serial-Poll/RX/TX-Zähler, Touchzustand, Tabsequenzen,
Heap/PSRAM und die letzten 16 von 256 Flight-Recorder-Einträgen. Andere Tasks
emittieren keine Serialausgabe. Der Debug-only Task-Watchdog überwacht Display
und Serial mit acht Sekunden und soll einen dauerhaften Taskblock mit
Panic-Backtrace sichtbar machen.

Phasen: Serial `3` = Poll, `4..8` = Logger-/Snapshot-/IMU-/Antwort-/
Diagnoseausgabe; Display `9/10` = vor/nach `M5.update()`, `11/12` = vor/nach
Touchverarbeitung, `13/14` = vor/nach `draw()`. Ein
eingefrorener Heartbeat mit einer Vorphase lokalisiert den letzten
erreichten Blockpunkt. Steigt `serial`-Heartbeat fortlaufend, während
`rx_bytes` bei gesendeten Befehlen nicht steigt, erreicht USB-CDC den
SerialConsumer nicht. Bleibt dagegen der Serial-Heartbeat stehen, ist der
Task/Scheduler bzw. ein geteilter Block verdächtig. Der Recorder hält zusätzlich
die Display-Grenzen vor/nach `M5.update()`, Touch und `draw()` fest.

## Reproduktion unmittelbar ab Reset

Der Hosttreiber wird direkt nach dem Upload/Reset gestartet und schreibt den
gesamten Trace einschließlich der letzten gesunden Antwort. Er sendet keine
Loggerbefehle.

```sh
python3 M5Tab5_Digifant_Analyzer/tools/run_tab5_runtime_probe.py \
  --port /dev/cu.usbmodem21101 --mode a --minutes 30 \
  --log /tmp/tab5-hang-A-no-sd.log
```

- A: `--mode a` – alle 2 s `TAB 0..3`, alle 10 s `DEBUG_STATUS`.
- B: `--mode b` – nur `DEBUG_STATUS`.
- C: `--mode c` – keine Serial-TAB-Befehle; manuelle Touch-Tabwechsel, während
  der Treiber die Diagnose abfragt.
- D: `--mode d` – nur `TAB 0..2` (KOMPAKT/LISTE/SYSTEM).
- E: `--mode e` – einmal MITSCHRIEBE (`TAB 3`), danach nur Diagnosen.

Jede Variante ist einmal ohne und einmal mit SD-Karte auszuführen, bis zum
ersten Fehler oder jeweils 30 Minuten. Bei `PROBE_FAILURE` bleibt der letzte
vollständige `DEBUG_STATUS` im Log; bei einem Watchdog-Panic ist der
Panic-Backtrace ebenfalls dort erfasst. Die Auswertung klassifiziert erst
danach `ROOT-CAUSE-CONFIRMED`, `ROOT-CAUSE-PROBABLE`,
`REPRODUCED-NOT-LOCALIZED` oder `NOT-REPRODUCED`.

## Bereitschaft für das nächste natürliche Auftreten

Der Debug-Build bleibt als separat zu flashender Diagnosebuild erhalten:

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 \
  --build-property 'compiler.cpp.extra_flags=-DTAB5_RUNTIME_DEBUG=1 -DTAB5_RUNTIME_DEBUG_WATCHDOG=1' \
  M5Tab5_Digifant_Analyzer
```

Die Produktivkonfiguration wird nicht mit den Debug-Flags gebaut oder
geflasht. Display-, IMU-, Processing-, Logger- und KWP-Tasks geben weiterhin
nichts aus; die Diagnose bleibt auf atomare/bounded Telemetrie im bestehenden
Debugpfad und den festen Flight-Recorder begrenzt. Heartbeats, Phasen,
Tab-request/applied/rendered, Serial-Poll/RX/TX, Stack-HWM, Heap/PSRAM und
Flight-Recorder bleiben unverändert nutzbar.

Bei einem natürlichen Fehler gilt diese Reihenfolge:

1. Nicht resetten. Die laufende Hostprobe offen lassen und weiter auslesen,
   solange CDC noch Daten liefert.
2. Den letzten vollständigen `DEBUG_STATUS` sichern. Bei fehlender Antwort
   werden `RX_PARTIAL`, `PROBE_STATE` mit `last_tx`, RX-Gesamtbytes,
   Antwortzähler, offenem Responsezustand und Partial-Länge sowie
   `PROBE_FAILURE` protokolliert.
3. USB-Enumeration, Portname, letzte TX/RX-Sequenz und alle noch erreichbaren
   Hostdaten sichern. Enumeriertes USB bei ausbleibender Anwendungsausgabe ist
   ausdrücklich als eigener Zustand zu dokumentieren.
4. Bei Watchdog/Panic den vollständigen Backtrace und den letzten
   Flight-Recorder-/Taskstatus aus dem unveränderten Log sichern.
5. Erst danach resetten und klassifizieren: `ROOT-CAUSE-CONFIRMED`,
   `ROOT-CAUSE-PROBABLE`, `REPRODUCED-NOT-LOCALIZED` oder `NOT-REPRODUCED`.

Bis `ROOT-CAUSE-CONFIRMED` vorliegt, werden keine weiteren künstlichen
Langzeittestmatrizen und kein Produktfix begonnen. R7 bleibt **BLOCKED**.

## Ergebnis der kontrollierten Läufe am 2026-08-26

Der Debug-Build (`TAB5_RUNTIME_DEBUG=1`,
`TAB5_RUNTIME_DEBUG_WATCHDOG=1`) wurde auf dem ESP32-P4 mit Arduino-ESP32
3.3.11 ausgeführt. K409 war abgezogen; die verfügbare Hardware enthielt die
SD-Karte (`storage_present=1`). Der Port war nach dem Upload
`/dev/cu.usbmodem1101`.

- A: 30 Minuten, vom Host bestätigt mit 900/900 `TAB`-Antworten und 180/180
  `DEBUG_STATUS`-Antworten (vorhandene Nutzeraufzeichnung).
- B: 30 Minuten, nur `DEBUG_STATUS`: `PROBE_PASS debug_replies=180`, kein
  Watchdog/Panic/Timeout.
- D: 30 Minuten, `TAB 0..2` alle 2 s: `PROBE_PASS debug_replies=180`,
  900/900 `TAB`-Bestätigungen, kein Watchdog/Panic/Timeout.
- E: 30 Minuten, einmal `TAB 3`, danach nur `DEBUG_STATUS`:
  `PROBE_PASS debug_replies=180`, kein Watchdog/Panic/Timeout.
- C: 30 Minuten Touch-Idle-Test, `PROBE_PASS debug_replies=180`; 89.686
  Touch-Samples, aber `presses=0`. Es fand keine manuelle Touch-Interaktion
  statt, daher ist dies kein Test eines echten Touch-Tabwechsels.

In B, D und E blieben Heap/PSRAM und die überwachten Heartbeats stabil. Die
Serial-Stack-HWM blieb bei 144 Wörtern. Der längste gemessene Serial-Poll lag
je nach Lauf bei etwa 2,41 s (B-Startburst), 1,77 s (D) bzw. 1,68 s (E),
danach wurden weitere Antworten verarbeitet. Im Log ist der B-Startburst als
Snapshot-/Logger-Ausgabe vor der ersten Diagnoseantwort sichtbar. Das belegt
eine begrenzte CDC-Ausgabeverzögerung unter Backpressure, aber keinen
dauerhaften 55-s-Serial-Hänger.

Die historische Störung wurde in diesen Läufen nicht reproduziert. Es wurde
kein Produktivfix implementiert; externe CDC-/I²C-Locks sowie Scheduler-,
Stack- und Speicherhypothesen bleiben ohne Root-Cause-Bestätigung offen.
Klassifikation: **NOT-REPRODUCED** (Touch-Interaktion und ein Lauf ohne SD
bleiben hardwarebedingt separat zu bewerten). R7 bleibt **BLOCKED**.

Die vollständigen Hostlogs liegen unter `/tmp/tab5-hang-B-with-sd-clean.log`,
`/tmp/tab5-hang-C-with-sd-clean.log`, `/tmp/tab5-hang-D-with-sd-clean.log` und
`/tmp/tab5-hang-E-with-sd-clean.log`. Der A-Log stammt aus der vorherigen
Aufzeichnung; ein Lauf ohne SD war in dieser Sitzung nicht möglich.
