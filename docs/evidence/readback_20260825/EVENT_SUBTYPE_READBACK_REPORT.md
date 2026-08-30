# DLOG V2 Event-Subtyp-Readback – 2026-08-25

Quelle: reale Tab5-Datei `/sprotz/g0_s0_80838060_0.dlog`, erzeugt durch
`LOG_START → SPROTZ_START → MARKER → SPROTZ_STOP → LOG_STOP`.

- SHA-256 Quelle und Hostkopie:
  `9c4176b7ef16e89c842985935a6615003806233cccb9745104fd6ab2ced39b18`
- Größe: 27906 Byte
- Format: DLOG V2 (`DGFTSPT2`), Headergröße 64 Byte
- vollständige Konverterdekodierung: PASS
- Records: 214 = 13 ECU-Snapshots, 195 IMU-Samples, 1 Orientation,
  1 START, 3 MARKER, 1 STOP
- Recordlängen und FNV-Payloadchecksummen: PASS
- Zeitachse: monoton, 80838060 bis 89948865 µs

## Aus den DLOG-Bytes rekonstruierte Ereignisse

| Timestamp (µs) | V2 kind | Payload | event_subtype |
|---:|---|---|---|
| 80838060 | START | leer | |
| 82875873 | MARKER | `01 01` | `SPROTZ_START` |
| 84878066 | MARKER | `01 03` | `MARKER` |
| 86911775 | MARKER | `01 02` | `SPROTZ_STOP` |
| 89948865 | STOP | leer | |

ECU-/IMU-Daten liegen vor dem Sprotz-Ereignis (3/44), währenddessen (7/82)
und danach (3/69) vor. Die Datei ist damit ohne Serial-Trace selbstbeschreibend.
