# Audio-Integration – Implementierungsplan

Status: **aktiv – Umsetzung in Einzelschritten**

Zielprojekt: `M5Tab5_Digifant_Analyzer/`

Stand: 2026-08-31

Dieses Dokument ist für die Audio-Integration maßgeblich. Der archivierte
Architekturreview begründet die Entscheidungen, steuert aber nicht den
Umsetzungsstatus.

## Ziel

Der Analyzer soll während einer ECU-/IMU-Messung kontinuierlich Motorgeräusche
aufzeichnen und als separat lesbare WAV-Segmente auf der microSD speichern.
DLOG und WAV müssen über dieselbe monotone Zeitbasis korrelierbar sein.

Die Audioaufnahme bleibt vom KWP-, Processing-, IMU- und DLOG-Datenpfad
entkoppelt.
Audio darf bei Überlauf oder SD-Fehlern keinen ECU-Reconnect, Logger-Stop oder
Reset anderer Queues auslösen.

Nicht Teil dieses Plans sind automatische Sprotz-Erkennung auf dem Target,
Audioausgabe über den Lautsprecher, Streaming über WLAN/USB sowie Änderungen
am KWP-Protokoll oder am DLOG-V2-Recordformat.

## Festgelegte Architektur

```text
Tab5-Mikrofon / M5Unified
        → MicrophoneSource
        → bounded AudioBlockPool im PSRAM
        → AudioSampler
        → AudioWriterTask
        → separate WAV-Segmente auf SD
```

- Kandidatenformat: 16 kHz, signed PCM, 16 Bit. Beide Kanäle werden am
  Montageort separat geprüft; erst dieses Gate legt Stereo oder eine konkrete
  Monoabbildung als Produktformat fest.
- Audio wird nicht als PCM in DLOG-V2-Records geschrieben.
- DLOG-Datei und WAV-Segmente erhalten denselben Dateistamm.
- Synchronisation: `startedAtUs`, globaler Sample-Frame-Index sowie monotone Zeit an
  erster und letzter Samplegrenze (`DGTM`-Metadaten im WAV).
- Keine Allokation, Dateizugriffe, Formatierung oder blockierende Wartezeit im
  Aufnahme-Hotpath.
- Der M5-Speaker bleibt während der Aufnahme deaktiviert.

### Unveränderliche Verträge

- Ein Audio-Block hat zu jedem Zeitpunkt genau einen Owner.
- Der Aufnahme-Producer wartet niemals auf Pool, Writer oder SD.
- Der Writer besitzt ausschließlich seine WAV-Datei; der DLOG-Logger besitzt
  ausschließlich seine DLOG-Datei.
- Ein Queue-/Pool-Überlauf verwirft Audio, erhöht monotone Zähler und erzeugt
  eine rekonstruierbare Lücke; er überschreibt keinen noch besessenen Block.
- Zeitindizes zählen **Sample-Frames**, nicht einzelne kanalverschachtelte
  `int16_t`-Werte. Ein Stereo-Frame enthält zwei Samples, ein Mono-Frame eins.
- Format, Kanalzahl und Rate ändern sich innerhalb eines WAV-Segments nicht.
- `SPROTZ_START`, `SPROTZ_STOP` und `MARKER` beeinflussen den Audiostrom nicht.
- Kein Audiofehler darf DLOG-, KWP-, Processing- oder IMU-Zustand verändern.

## Ausgangslage

Der eigenständige [`M5Tab5_Audio_Probe`](../../M5Tab5_Audio_Probe/README.md)
hat Mikrofonstart/-stop, nichtblockierende Aufnahme, Pegelauswertung und
WAV-Schreiben auf echter Hardware nachgewiesen. Die Audioaufnahme ist noch
nicht in den Analyzer eingebunden.

Die Architekturentscheidung und die ausführliche Risikoanalyse stehen im
[Audio-Architekturreview](../archive/audio/AUDIO_ARCHITECTURE_REVIEW_SOL_HIGH.md).
Dieses Dokument ist der aktive Arbeitsplan; der Review bleibt als historische
Begründung erhalten.

## Status und Reihenfolge

| ID | Arbeitspaket | Status | Nächstes Gate |
|---|---|---|---|
| A1 | Audio-Probe und generischer Hardware-Proof | PASS | Nachweis liegt vor |
| A2 | Motor-/Montageort-Proof und Produktformat | PENDING-HARDWARE | reale Motoraufnahme |
| A3 | Blockpool und Sampler-Core | PASS | Host-/Sanitizernachweis liegt vor |
| A4 | M5Unified-Quelladapter | BLOCKED-BY-A2 | Target-Langlauf ohne SD |
| A5 | WAV-Format und Inspektionstool | PENDING | Host-Golden-/Repairtests |
| A6 | Audio-Writer und SD-Fehlerisolation | PENDING | Audio-only/Dual-Writer |
| A7 | Analyzer-Control-Link | PENDING | ECU-/IMU-/SD-Integration |
| A8 | Gemeinsame Messfahrt | PENDING | gestufte reale Fahrten |
| A9 | Offline-Auswertung | PENDING | reproduzierbare Analyse |

A2 und A3 durften parallel vorbereitet werden: Das Produktformat wird erst in
A2 festgelegt, während A3 formatneutral mit einer festen Bytekapazität je
Block arbeitet. A3 ist abgeschlossen. A4 beginnt erst nach A2; A4 bis A8
werden strikt in Reihenfolge abgeschlossen.

Pro Arbeitspaket gilt:

1. Vertrag und gezielte RED-Tests festlegen.
2. Nur den kleinsten produktiven Schritt implementieren.
3. Gezielte Tests, vollständige Host-/Sanitizer-Regression,
   `git diff --check` und – sobald Targetcode berührt ist – den
   Produktionscompile ausführen.
4. Hardwareabhängige Nachweise nicht durch Simulation ersetzen.
5. Status und Evidenz in diesem Plan beziehungsweise in
   `M5Tab5_Digifant_Analyzer/verification.md` aktualisieren.

## Implementierungsphasen

### Phase 1 – Hardware-Proof

Status: **A1 PASS; A2 PENDING-HARDWARE**.

- Beide Mikrofonkanäle und ihre Kanalreihenfolge bestätigen.
- 16 kHz/16 Bit sowie Stereo-zu-Mono-Entscheidung am vorgesehenen Montageort
  mit Pegel-, Spektrum- und Clippingdaten absichern.
- Leerlauf sowie 1000, 1500, 2000 und 2500 rpm aufnehmen.
- Den noch offenen Motor-Audio-Proof in
  [`HARDWARE_PROOF.md`](../../M5Tab5_Audio_Probe/HARDWARE_PROOF.md) ergänzen.

### Phase 2 – Sampler und bounded Buffer

#### A3: Hosttestbarer Blockpool und Sampler-Core

Status: **PASS – 2026-08-31**. Implementierung und Nachweis stehen in
`M5Tab5_Digifant_Analyzer/verification.md`.

Neue Komponenten:

- `M5Tab5_Digifant_Analyzer/src/audio_types.h`
- `M5Tab5_Digifant_Analyzer/src/audio_block_pool.h`
- `M5Tab5_Digifant_Analyzer/src/audio_sampler.h`
- `M5Tab5_Digifant_Analyzer/tests/audio_block_pool_test.cpp`
- `M5Tab5_Digifant_Analyzer/tests/audio_sampler_test.cpp`

Ownership-Vertrag:

```text
Free → MicOwned → Ready → WriterOwned → Free
```

Der Pool ist formatneutral, fest begrenzt und erhält seinen zusammenhängenden
PCM-Speicher beim Setup vom Targetadapter. Er allokiert selbst nicht. Jeder
Block hat 4096 Byte Nutzkapazität sowie Metadaten für Sequenz,
`firstFrameIndex`, Frameanzahl, Format und monotone Zeit der ersten
Samplegrenze.

Der Sampler-Core besitzt Sessionzustand, nächsten Frameindex, sichtbare
Drop-/Gap- und Sourcefehlerzähler sowie definierte Start-/Stop-Semantik. Kann
er keinen freien Block reservieren, muss die Quelle weiter drainiert werden;
die verworfene Frameanzahl wird der nächsten Veröffentlichung als Gap
zugeordnet.

Zu testen sind Initialisierung, jede erlaubte und unerlaubte
Ownership-Transition, Wrap-around, vollständiger Pool, Writer langsamer als
Producer, fortgesetzte Frameindizes nach Drops, Source-Fehler, Start/Stop und
Sessionneustart. Mindestens Pool- und Samplertest laufen zusätzlich mit
ASan/UBSan und TSan.

Startwert: 4096 Byte je Block, 34 Blöcke insgesamt. Davon können höchstens 32
fertige Blöcke gepuffert und zwei von M5Unified gehalten werden. Das sind
136 KiB PCM-Speicher; bei 16-kHz-/16-Bit-Mono entsprechen 32 fertige Blöcke
4,096 Sekunden Reserve. Die tatsächliche Framekapazität wird aus dem später
festgelegten Kanalformat berechnet.

Abnahme A3:

- keine Heapallokation in Pool oder Sampler-Core;
- exakt ein Owner je Block, auch bei Fehlern und Stop;
- keine Datenüberschreibung bei vollem Pool;
- monotone Blocksequenz und Frameindizes einschließlich expliziter Gaps;
- Zähler und High-Watermarks sättigen definiert statt unbemerkt überzulaufen;
- gezielte Tests sowie vollständige Host-/Sanitizer-Regression PASS;
- noch keine Analyzer-Runtimeverdrahtung und kein Target-Hardwaregate.

#### A4: M5Unified-Quelle anbinden

Neue Komponente: `src/m5tab5_microphone_source.h` plus minimaler Setup-Hook
im `.ino`, zunächst per Buildflag standardmäßig deaktiviert.

Der Adapter reserviert den PCM-Speicher einmalig aus PSRAM, validiert die
vollständige Kapazität vor Taskstart und besitzt den M5Unified-Lebenszyklus.
Außerhalb einer Logsession drainiert er die Quelle ohne Veröffentlichung. Eine
I²C-/Codec-Reinitialisierung während laufender Analyzer-Tasks ist unzulässig.

Die Quelle muss 60 Minuten ohne SD-Writer und ohne Drops laufen. Gemessen
werden Blockrate, Quelllatenz, Ring-HWM, Stack und Heap/PSRAM vor und nach dem
Lauf. Erst danach folgt ein K409-/ECU-Smoke. Scheitert die Kontinuität, wird
als separater Entscheidungsschritt ein direkter ESP-IDF-I²S-RX geprüft; das
ist kein automatischer Umbau.

### Phase 3 – WAV-Format und Writer

#### A5: Hostformat und Zeitsynchronisation

Neue Komponenten:

- `src/audio_wave_format.h`
- `tests/audio_wave_format_test.py`
- `tools/inspect_audio_log.py`

Zu prüfen sind Standard-WAV-Kompatibilität, RIFF-Padding, Little Endian,
Längengrenzen, Header-Reparatur nach Abbruch, unbekannte RIFF-Chunks,
Segment-/Gap-Merge sowie die Frameindex-zu-DLOG-Zeitabbildung. Die
versionierten `DGTM`-Metadaten enthalten mindestens DLOG-Stamm/Startwert,
Format, ersten globalen Frameindex und monotone Start-/Endanker.

#### A6: Separater AudioWriter

Neue Komponenten:

- `src/audio_wave_writer.h`
- `src/audio_writer_target.h`
- `tests/audio_writer_test.cpp`

Der Writer schreibt ausgerichtete 4-KiB-Blöcke in höchstens 30 Minuten lange
WAV-Segmente,
besitzt sein eigenes Fehler-/Lag-/Drop-Reporting und verändert niemals den
DLOG-Status. Er flusht höchstens periodisch, beim Marker jedoch nicht, und
hält vor Segmentstart 128 MiB Speicherreserve ein. Hosttests müssen
Short-write, volle/langsame/entfernte SD, Flush-/Close-Fehler, nicht
finalisierte Header, Segmentwechsel und Gap-Splits abdecken.

Target-Gates: zunächst zwei Stunden Audio-only, danach synthetische
ECU-/IMU-Daten mit DLOG+Audio. Beim Auftreten des historischen
`xTaskPriorityDisinherit`-Asserts oder neuer DLOG-Drops wird die Integration
angehalten; R7 wird dadurch nicht als gelöst betrachtet.

### Phase 4 – Analyzer-Lebenszyklus

#### A7: `LOG_START`/`LOG_STOP` anbinden

Eine kleine bounded `AudioControlQueue` und ein per Mailbox veröffentlichtes
`AudioStatus` verbinden den bestehenden Logger-Lebenszyklus mit dem
Audio-Service. Der erfolgreiche DLOG-Start veröffentlicht einen unveränderlichen
Startdeskriptor mit Dateistamm und `startedAtUs`:

- Audio startet erst nach erfolgreichem DLOG-Start.
- DLOG-Stop wartet niemals auf Audio.
- `SPROTZ_START`, `SPROTZ_STOP` und `MARKER` bleiben reine DLOG-Ground-Truth
  und starten/stoppen den Audiostrom nicht.
- Audiofehler bleiben im Audio-Status sichtbar und verändern KWP/IMU nicht.
- Verlorene oder doppelte Control-Befehle werden über Session-ID und
  monotone Befehlssequenz erkannt und deterministisch abgelehnt.

Tests: fehlgeschlagener Audiostart, voller Control-/Audio-Ring beim Stop,
doppelte oder veraltete Befehle, Reset während Aufnahme und Marker während
Aufnahme.

### Phase 5 – Gemeinsame Messfahrt

- Produktionsbuild mit realer ECU, K409, IMU und SD testen.
- Gestufte Läufe: 15 Minuten, 60 Minuten, anschließend längste geplante
  Fahrt.
- Mehrere `SPROTZ_START`/`SPROTZ_STOP`-Paare und freie `MARKER` setzen.
- DLOG und WAV am Host auf Dateistamm, Zeitachse, Gaps und Hörbarkeit prüfen.
- Gegenüber einer identischen Fahrt ohne Audio dürfen keine zusätzlichen
  RX-/Frame-/Parser-/Action-/Snapshot-/IMU-Drops oder Sessionabbrüche auftreten.

### Phase 6 – Offline-Auswertung

Erst nach stabiler Datenerfassung werden Spektrogramme, Impulsmuster und die
Korrelation mit Drehzahl, G69, Last und manuellen Sprotz-Markern untersucht.
Eine automatische Sprotz-Erkennung gehört ausdrücklich nicht zum ersten
Target-Release.

## Definition of Done

- Hardware-Proof am realen Montageort abgeschlossen.
- Blockpool, Sampler, Writer und Control-Link hostgetestet und sanitisiert.
- WAV ist nach normalem Stop und nach simuliertem Abbruch lesbar/reparierbar.
- DLOG/WAV-Zeitbezug ist durch ein Hosttool reproduzierbar geprüft.
- Audio- und DLOG-Schreiben bleiben bounded und melden eigene Gaps/Drops.
- Kombinierte Target-Läufe zeigen keine Regression im KWP-/IMU-/Loggerpfad.
- Der offene `xTaskPriorityDisinherit`-Befund ist separat dokumentiert und
  tritt im Dual-Writer-Gate nicht auf.
- Erst danach gilt der Analyzer als vollständig für akustisch korrelierte
  Messfahrten einsetzbar.

## Stop-/Fallback-Regeln

- Neue KWP-/IMU-/DLOG-Drops oder Sessionabbrüche stoppen das jeweilige
  Target-Gate; sie werden nicht durch größere unbounded Puffer kaschiert.
- Tritt beim Dual-Writer-Test `xTaskPriorityDisinherit` oder unbounded
  DLOG-Latenz auf, folgt als eigenes Arbeitspaket ein einzelner physischer
  Storage-Owner mit getrennten DLOG-/Audio-Lanes und DLOG-Vorrang. Audio bleibt
  dabei außerhalb von `SprotzLoggerCore` und DLOG V2.
- Liefert M5Unified keine nachweisbar kontinuierlichen Blockgrenzen, wird vor
  weiterem Targetcode über einen direkten ESP-IDF-I²S-RX-Adapter entschieden.
- Hardwaregates bleiben `PENDING-HARDWARE`; fehlende Hardware ist weder PASS
  noch ein Softwarefehler.

## Nächster konkreter Arbeitsschritt

Als nächstes **A2** am vorgesehenen Montageort abschließen und damit Kanalwahl
und Produktformat festlegen. Danach folgt **A4**, der M5Unified-Quelladapter
mit einmaliger PSRAM-Reservierung und zunächst deaktivierter
Analyzer-Runtimeintegration.
