# Unabhängiger DLOG-Readback – 2026-08-25

Quelle: reale SD-Karte aus dem Tab5, `/sprotz/g0_s0_86260534_0.dlog`.
Die Datei wurde unverändert nach `readback_20260825/` kopiert und mit
`M5Tab5_Digifant_Analyzer/tools/decode_sprotz_log.py` dekodiert.

## Ergebnis

- Dateigröße: **63.518 Byte**
- DLOG-Version: **V2** (`DGFTSPT2`, Header 64 Byte)
- Gesamtzahl Records: **495**
- ECU-Snapshots: **29**
- IMU-Samples: **458**
- IMU-Orientation-Records: **1**
- Events: **7** (1 START, 5 MARKER, 1 STOP)
- erster Timestamp: **86.260.534 µs**
- letzter Timestamp: **107.717.536 µs**
- monotone Record-Zeitachse: **PASS** (keine Rückwärtsbewegung; eine zulässige
  Gleichheit bei Orientation/START)
- Header-/Payload-Timestamps: **PASS**
- V2-Magie, Recordlängen und FNV-Checksummen: **PASS**
- vollständige Konverter-Dekodierung: **PASS**

## Eventreihenfolge

Die Datei enthält diese Byte-Reihenfolge:

```text
START  86.260.534
MARKER 88.314.552
MARKER 92.339.535
MARKER 94.373.799
MARKER 98.424.552
MARKER 100.456.549
STOP   107.717.536
```

Die fünf Marker-Timestamps stimmen mit dem Target-Serial-Trace des
Standtests überein: `SPROTZ_START`, `MARKER`, `SPROTZ_STOP`, `SPROTZ_START`,
`SPROTZ_STOP`. Das DLOG-V2-Format speichert diese drei fachlichen Aktionen
bewusst alle als `MARKER`; eine subtype-spezifische Unterscheidung ist daher
aus den Bytes allein nicht möglich. Die Zuordnung erfolgt ausschließlich über
die bereits aufgezeichneten Befehls-Timestamps, nicht über eine Änderung des
DLOG-Formats.

## ECU-/IMU-Abdeckung

| Bereich | ECU | IMU |
|---|---:|---:|
| LOG_START bis vor erstem Marker | 1 | 45 |
| zwischen erstem und letztem Marker | 17 | 261 |
| nach letztem Marker bis LOG_STOP | 11 | 152 |

Damit sind ECU- und IMU-Daten vor, zwischen und nach den Sprotz-Markierungen
im Readback vorhanden.

Es wurden keine Produktivdateien und keine DLOG-Encoder geändert.
