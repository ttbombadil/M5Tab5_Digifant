# Projektarchitektur

Dieses Projekt wird klein und bei Bedarf modular aufgebaut. Neue Unterordner
oder Schichten entstehen erst, wenn mehrere Dateien oder eine konkrete
technische Notwendigkeit dies erfordern.

Unverändert verbindlich sind [`../docs/architecture/ARCHITECTURE_V2.md`](../docs/architecture/ARCHITECTURE_V2.md):

- Der Protocol Runner ist alleiniger Owner von K409-Transport, RX-Verbrauch und
  KWP-Sessionzustand.
- TX- und Control-Operationen benötigen korrelierte Tokens; ein Timeout gibt
  keine Leitung zur Wiederverwendung frei. Quiescence ist vor jeder Reuse nötig.
- KWP-Deadlines bleiben Core-owned und absolut; Callback-, UI-, Logging- und
  Persistenzarbeit darf sie nicht verlängern.
- Überlast und RX-Lücken sind sichtbar und deterministisch zu behandeln;
  Downstream darf den Protokollfortschritt nicht blockieren.

## Aktueller Runtime-Iststand

Die historische V2-Arbeitsreihenfolge ist abgeschlossen. Der aktuelle
Produktivdatenfluss lautet:

```text
K409 callback → RxIngressRing → Arduino-Loop als KWP-Owner
→ KwpMeasurementSession → ValidatedFrameQueue
→ Processing-Task/ProcessingService → DiagnosticDecoder → MeasurementModel
→ unabhängige Snapshot-Mailboxen → Serial/Display/Logger
```

Die Arduino-Loop ist funktional der KWP-Runner, auch wenn die geplante
`KwpProtocolRunner`-Datei noch nicht existiert. Die Strukturarbeit darf diesen
Owner später in einen flachen Target-Runtimebaustein verschieben, aber keine
Transport-/KWP-State-Machine oder Deadline ändern. V2-009 bis V2-019 und der
reale ECU-/IMU-/SD-Nachweis sind in `verification.md` statusgeführt.
