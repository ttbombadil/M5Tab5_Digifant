# M5Tab5 Digifant Analyzer

Eigenständiger Arduino/M5Stack-Neuaufbau für Tab5, AutoDia K409 und Digifant.
Die verbindlichen kritischen Verträge stehen in [`ARCHITECTURE.md`](ARCHITECTURE.md)
und der übergeordneten [`../ARCHITECTURE_V2.md`](../ARCHITECTURE_V2.md).

## Aktueller Verifikationsstand

Der historische V2-001-Smoke-Test ist abgeschlossen. Die aktuelle Firmware
führt die vollständige V2-Pipeline mit K409, KWP, Decoder, Snapshot-Consumer,
Logger und Tab5-IMU aus. Die detaillierten Befehle, Zähler, Targetläufe und
Host-Rücklesungen stehen chronologisch in
[`verification.md`](verification.md); am Anfang dieses Dokuments gibt es eine
kompakte Statusmatrix.

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer
arduino-cli upload --fqbn esp32:esp32:m5stack_tab5 --port <serieller-port> M5Tab5_Digifant_Analyzer
```

Das Altprojekt `../M5Tab5_Digifant_Proto/` ist unveränderliche Referenz und darf
nicht inkludiert, gelinkt oder umgebaut werden.

## Langzeitlogger „Sprotz"

Der Tab **MITSCHRIEBE** enthält `SPROTZEN START/STOP` und `MARKER`. Der Logger
schreibt ausschließlich Kopien des `MeasurementSnapshot` aus einer festen
32-Snapshot-SPSC-Queue. SD-Zugriffe und Binärcodierung laufen in einer eigenen
Task mit Priorität 1; der KWP-/Decoderpfad wartet nie auf den Logger.

Benötigt wird eine FAT32-formatierte microSD. Logs liegen unter `/sprotz/` als
versionierte `.dlog`-Dateien. Jeder Snapshotrecord enthält alle 26 ECU-Felder
sowie monotone Zeit, Sequenz, Sessionepoch und Transportgeneration. START,
STOP und MARKER sind eigene timestamped Records. Die UI und Serial zeigen
`BEREIT`, `REC`, `KEINE SD`, `VOLL` oder `FEHLER`; Queueverluste werden gezählt.

Konvertierung auf dem Entwicklungsrechner:

```sh
./M5Tab5_Digifant_Analyzer/tools/decode_sprotz_log.py input.dlog -o output.csv
```

Bei abruptem Stromverlust können höchstens die seit dem letzten periodischen
Flush noch nicht vom Dateisystem persistierten Datensätze fehlen. STOP und
MARKER führen sofort einen Flush aus.

## Entwicklung und Verifikation

Hosttests werden aus `tests/` einzeln mit C++20, Warnings-as-errors und
ASan/UBSan gebaut. Die statischen Pipelinechecks liegen unter `tools/`.
Targetbuild und Upload:

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Digifant_Analyzer
arduino-cli upload --fqbn esp32:esp32:m5stack_tab5 --port <serieller-port> M5Tab5_Digifant_Analyzer
```

Die vollständige ECU-/IMU-/SD-Abnahme V2-019 ist in `verification.md`
dokumentiert. Neue Runtimeänderungen benötigen weiterhin den dort definierten
60-s-ECU-Regressionslauf.
