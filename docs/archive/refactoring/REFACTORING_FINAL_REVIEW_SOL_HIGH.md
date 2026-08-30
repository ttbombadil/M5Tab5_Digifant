# Abschlussreview Refactoring – M5Tab5_Digifant_Analyzer

**Stand:** 2026-08-25  
**Scope:** ausschließlich der aktuelle Quellbaum; R7 wurde weder implementiert
noch neu geplant.

## Gesamtbefund

Das Projekt ist heute ausreichend klar und stabil strukturiert, um neue
Featurearbeit zu beginnen. Die bewiesenen Pipeline-, Queue-, Snapshot-,
Timing- und Entkopplungsverträge sind im Dateischnitt sichtbar und durch
produktnahe Tests geschützt. Ein weiterer allgemeiner Architektur- oder
Dateischnitt ist vor neuen Features nicht erforderlich.

Die `.ino` ist mit 455 Zeilen noch kein reiner Composition Root. Processing,
Serial, Logger, IMU und Display sind jedoch sinnvoll abgegrenzt; die wesentliche
verbleibende Implementierungslogik ist die bewusst nicht extrahierte
KWP-Targetorchestrierung. Solange R7 BLOCKED ist, muss dieser Zustand akzeptiert
werden.

## Status nach R5

R0–R6 sowie R8–R9 sind nach dem aktuellen Quellbaum und den ausgeführten Gates
abgeschlossen. `DummySnapshotConsumer`, `bluetooth_snapshot_dummy` und
`web_snapshot_dummy` werden nur noch bei `V2_015_TARGET_STRESS=1` angelegt;
der Produktionsbuild publiziert keine optionalen Bluetooth-/Web-Snapshots.
R7 bleibt BLOCKED.

## Bewertung des Ist-Zustands

| Bereich | Bewertung |
|---|---|
| Lesbarkeit und Verantwortungen | Gut. `ProcessingService`, `SerialConsumer`, Logger-Core/Channels/Format, Snapshot-Typen/Mailboxen und `DisplayUi` besitzen klare Aufgaben. Die größeren KWP-/Displaydateien sind fachlich kohärent und müssen nicht allein wegen ihrer Länge geteilt werden. |
| `.ino` als Composition Root | Akzeptabel, aber unvollständig. Taskanlage und Verdrahtung dominieren; USB-/KWP-Callbacks, Completionwait, Handshake, Measurement und Reconnect bleiben als bekannter R7-Block enthalten. |
| Ownership und Tasks | Gut nachvollziehbar. `loop()` besitzt die KWP-Session, Processing besitzt Decoder/Model, Logger besitzt SD/Datei, IMU besitzt Sampling, Serial/Display sind Blattconsumer. Loggerkommandos nutzen MPSC; Snapshot-/IMU-Übergaben sind bounded. |
| Testnähe | Gut. R9 testet reale `KwpProtocolCore`-, `KwpMeasurementSession`-, `KwpByteEngine`-, Fanout-, Display-, Processing- und Serial-Komponenten statt Schattenmodellen. Hardware-/FreeRTOS-Pfade benötigen weiterhin reale Gates. |
| Dokumentation | Aktueller R5-Status und Stressschalter sind nachgetragen; einzelne chronologische Baselineabschnitte bleiben als historische Nachweise erkennbar. |

Die aktuellen C++20-Hosttests wurden im Abschlussreview erneut ausgeführt:
**30/30 PASS**. V2-009, V2-010, V2-013, V2-014, V2-015 und der Loggerguard
sind ebenfalls **PASS**. Die unmittelbar vorausgehende R9-Verifikation
dokumentiert zusätzlich **30/30 ASan/UBSan**, **6/6 relevante TSan-Tests**,
DLOG-PASS und einen erfolgreichen Arduino-Targetcompile.

R5 selbst wurde mit beiden Targetvarianten kompiliert und 65 Sekunden real
gegatet: Produktionsbuild 917220/157020 (Flash/globale Daten), Stressbuild
917596/157028. Beide Läufe hielten KWP/ECU und alle Fehler-/Dropzähler stabil;
nur der Stressbuild erzeugte die erwarteten zusätzlichen Snapshot-Overwrites.

## Verbleibende technische Schulden

1. Der nicht reproduzierbare `xTaskPriorityDisinherit`-Assert im mutierenden
   Logger-/SD-Pfad ist das wichtigste Betriebsrisiko.
2. Die KWP-Targetorchestrierung bleibt direkt in der `.ino`; dies ist bekannte,
   derzeit ausdrücklich nicht zu bearbeitende R7-Schuld.
3. Einzelne Status- und Baselineangaben der Dokumentation bilden den aktuellen
   Stand nur in späteren Ergänzungen korrekt ab.

Keine dieser Schulden rechtfertigt einen Rewrite oder neue Frameworkschichten.
`display_ui.cpp`, `kwp_measurement_session.h` und `kwp_byte_engine.h` sind zwar
lang, aber ausreichend kohärent; ein weiterer rein größengetriebener Schnitt
wäre derzeit ohne klaren Nutzen.

## Separates Risiko: `xTaskPriorityDisinherit`

Der Assert wurde real beobachtet, ließ sich aber trotz wiederholter
`START -> MARKER -> STOP`-Zyklen und temporärer Mutex-Owner-Instrumentierung
nicht erneut auslösen. Belegt ist nur die verletzte FreeRTOS-Invariante:
gespeicherter Mutexholder und freigebende Task stimmen an der Assert-Stelle
nicht überein. Der konkrete Mutex beziehungsweise Fehlerpfad ist nicht belegt;
ein Produktivfix wäre daher spekulativ.

Das Risiko betrifft vor allem Änderungen oder Freigaben, die verlässliches
mutierendes SD-Logging voraussetzen. Beim nächsten Auftreten müssen vollständiger
Panic-Backtrace und ELF desselben Builds gesichert werden. Es ist kein Beleg
für einen allgemeinen KWP-, Processing- oder Queuefehler.

## Entscheidung

**READY-WITH-CONDITIONS**

Neue Featurearbeit kann beginnen; ein allgemeiner weiterer Refactoringblock
ist nicht erforderlich. Es gelten maximal diese drei Bedingungen:

1. **KWP-Runtime und R7 bleiben unangetastet.** Neue Features nutzen nur die
   vorhandenen KWP-/Transportgrenzen und verändern keine Timing- oder
   Queueverträge.
2. **Logger-/SD-Arbeit bleibt hardwaregegatet.** Mutierende Loggeränderungen
   oder entsprechende Releases benötigen wiederholte reale
   `START -> MARKER -> STOP`-Zyklen; bei erneutem Assert werden Panic-Backtrace
   und passende ELF gesichert, ohne spekulativen Fix.
