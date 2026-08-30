# M5Tab5 Digifant Analyzer

Arduino/M5Stack-Firmware zur Analyse von Digifant 1.7 über AutoDia K409 und
KWP1281 auf dem M5Stack Tab5 (ESP32-P4).

## Aktueller Stand

Die produktive Pipeline unterstützt reale ECU-/K409-Kommunikation, kontinuier-
liches ECU- und Tab5-IMU-Sampling, Snapshot-Verarbeitung und SD-Logging. Die
MITSCHRIEBE-Ansicht steuert den Sprotz-Logger mit `SPROTZ_START`,
`SPROTZ_STOP` und `MARKER`; die kompatiblen Kurzkommandos `LOG_START` und
`LOG_STOP` bleiben verfügbar. DLOG V2 enthält selbstbeschreibende Event-
Subtypen sowie ECU-, IMU-, Start-, Stop- und Marker-Records.

Die Displaytabs sind KOMPAKT, LISTE, SYSTEM und MITSCHRIEBE. Die vollständigen
technischen Nachweise stehen chronologisch in
[`verification.md`](verification.md).

Bekannte offene Risiken:

- R7 bleibt `BLOCKED` wegen des Target-Gates zu `xTaskPriorityDisinherit`.
- Der Assert wurde auf dem Target beobachtet, ist derzeit `NOT-REPRODUCED`.
- Der Tab5-Runtime-Hänger ohne K409 ist `NOT-REPRODUCED / OPEN`.

## Build und Upload

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer
arduino-cli upload --fqbn esp32:esp32:m5stack_tab5 --port <serieller-port> M5Tab5_Digifant_Analyzer
```

Der Diagnosebuild ist strikt getrennt und opt-in:

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 \
  --build-property 'compiler.cpp.extra_flags=-DTAB5_RUNTIME_DEBUG=1 -DTAB5_RUNTIME_DEBUG_WATCHDOG=1' \
  M5Tab5_Digifant_Analyzer
```

Details zur konservierten Target-Diagnose stehen in
[`TAB5_RUNTIME_HANG_DEBUG.md`](TAB5_RUNTIME_HANG_DEBUG.md). Der normale
Produktionsbuild verwendet `TAB5_RUNTIME_DEBUG=0`.

## Logger und DLOG V2

Logs liegen auf FAT32-microSD unter `/sprotz/` als versionierte `.dlog`-Dateien.
`LOG_START`/`LOG_STOP` und `SPROTZ_START`/`SPROTZ_STOP` steuern den Logger;
`MARKER` erzeugt einen eigenen Record. Die DLOG-V2-Records enthalten
selbstbeschreibende Event-Subtypen sowie ECU-/IMU-Daten und Zeit-/Sequenz-
Provenienz. Konvertierung:

```sh
./tools/decode_sprotz_log.py input.dlog -o output.csv
```

## Tests und maßgebliche Dokumente

Vom Repository-Root aus bündelt `./tools/run_checks.sh` die verfügbaren
Hosttests, Architektur-/Loggerguards und den Touch-Probe-Compile. Einzelne
Analyzer-Tests und Builds können weiterhin wie unten beschrieben ausgeführt
werden.

Die C++-Hosttests liegen in `tests/`; der DLOG-Test läuft mit
`python3 tests/dlog_v2_test.py`. Architektur- und Loggerguards liegen in
`tools/`; Host-/Sanitizer-/Targettests und reale ECU-/IMU-/SD-Abnahmen sind in
[`verification.md`](verification.md) nachgewiesen.

- [`ARCHITECTURE.md`](ARCHITECTURE.md): produktive Analyzer-Architektur.
- [`../docs/architecture/ARCHITECTURE_V2.md`](../docs/architecture/ARCHITECTURE_V2.md): verbindliche V2-Verträge.
- [`../docs/archive/plans/IMPLEMENTATION_PLAN_V2.md`](../docs/archive/plans/IMPLEMENTATION_PLAN_V2.md): historischer,
  abgeschlossener Implementierungsplan.
- [`../docs/digifant/DIGIFANT_MEASUREMENT_SEMANTICS.md`](../docs/digifant/DIGIFANT_MEASUREMENT_SEMANTICS.md):
  ECU-Felder, Formeln und Evidenz.
- [`TARGET_ASSERT_XTASKPRIORITYDISINHERIT.md`](TARGET_ASSERT_XTASKPRIORITYDISINHERIT.md):
  Target-Assert-Nachweis und R7-Entscheidung.
- [`TAB5_RUNTIME_HANG_DEBUG.md`](TAB5_RUNTIME_HANG_DEBUG.md): Diagnosebuild und
  Bereitschaft für das offene Runtimeproblem.

`../M5Tab5_Digifant_Proto/` bleibt eine unveränderliche Referenz und wird nicht
in den Analyzer eingebunden.
