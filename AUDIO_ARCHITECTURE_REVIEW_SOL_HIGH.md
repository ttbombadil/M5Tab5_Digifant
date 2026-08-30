# Audio-Architekturreview für `M5Tab5_Digifant_Analyzer`

Stand: 2026-08-26  
Status: **Analyse und Architekturplanung; keine Produktivcodeänderung**

## 1. Ergebnis in Kürze

Die bevorzugte erste Produktarchitektur ist **kein Audio-Recordstrom im DLOG**,
sondern eine separate, kontinuierliche PCM/WAV-Datei mit eigener bounded
Pipeline:

```text
Tab5-Mikrofone/ES7210
        │  I²S0 RX + DMA
        ▼
MicrophoneSource
        │  feste PCM-Blöcke, Sampleindex, monotone Blockgrenze
        ▼
bounded AudioBlockPool/Ring in PSRAM
        │  SPSC, niemals auf SD warten
        ▼
AudioWriterTask ───────────────► eigene WAV-Segmente auf SD
        │                         gleicher Dateistamm wie DLOG
        └── AudioStatus           DGTM-Zeitreferenz im WAV
```

Startkonfiguration: **16 kHz, signed PCM 16 Bit, mono**. Das sind
**32.000 Byte/s, 256 kbit/s, 115,2 MB/h (109,86 MiB/h)** zuzüglich
vernachlässigbarer WAV-/Metadaten. 8 kHz halbiert diese Werte, schneidet aber
den auswertbaren Frequenzbereich zu früh ab. Höhere Raten sind erst nach einem
Spektraltest begründet.

Die Audio-Datei erhält dieselbe `esp_timer_get_time()`-Zeitbasis wie DLOG und
einen festen Bezug aus DLOG-`startedAtUs`, globalem Sampleindex, nominaler
Samplingrate und tatsächlicher monotoner Zeit an der ersten Samplegrenze.
Sampleindex und Rate ergeben jeden nominellen Timestamp; ein zusätzlicher
Endanker je Segment korrigiert auf langen Aufnahmen die gemessene Clockdrift,
ohne eine zweite Clock oder ein Synchronisationsprotokoll einzuführen.

Das größte technische Risiko ist **nicht die mittlere Datenrate**, sondern die
Kombination aus SD-Latenzspitzen, zwei offenen Dateien/SD-Nutzern und der noch
offenen `xTaskPriorityDisinherit`-Beobachtung. R7 bleibt ausdrücklich
`BLOCKED`; der historische Assert und der nicht reproduzierte Runtime-Hänger
werden weder als Audiofehler umgedeutet noch in dieser Arbeit behoben.

Der erste konkrete Luna-Implementierungsschritt ist ein **eigenständiger,
nicht in den Analyzer eingebundener `M5Tab5_Audio_Probe`**: beide Mikrofonkanäle
mit M5Unified bei 16 kHz/16 Bit lesen, wenige Sekunden in vorab reserviertes
PSRAM aufnehmen und erst nach der Aufnahme Min/Max, RMS und Clipping ausgeben.
Noch kein DLOG, kein gemeinsamer SD-Schreibbetrieb und keine Erkennung.

## 2. Scope und unveränderte Verträge

Für diese Bewertung gelten folgende harte Grenzen:

- ECU-, KWP-, Processing- und IMU-Pfade bleiben funktional und zeitlich
  unabhängig von Audio.
- `SPROTZ_START`, `SPROTZ_STOP` und `MARKER` bleiben die manuelle Ground Truth
  im DLOG. Sie starten oder stoppen den kontinuierlichen Audiostrom nicht.
- `LOG_START`/`LOG_STOP` bestimmen die Messfahrt und damit den gewünschten
  Audio-Lebenszyklus.
- DLOG V2, `SprotzLoggerCore`, der 25-ms-Time-Merge und bestehende Decoder
  bleiben in der empfohlenen ersten Integration unverändert.
- Audiofehler dürfen keinen KWP-Reconnect, keinen Logger-Stop und keine
  Änderung am ECU-/IMU-Zustand auslösen.
- R7 bleibt `BLOCKED`. Die beiden Dokumente
  [`TARGET_ASSERT_XTASKPRIORITYDISINHERIT.md`](M5Tab5_Digifant_Analyzer/TARGET_ASSERT_XTASKPRIORITYDISINHERIT.md)
  und [`TAB5_RUNTIME_HANG_DEBUG.md`](M5Tab5_Digifant_Analyzer/TAB5_RUNTIME_HANG_DEBUG.md)
  bleiben maßgeblich und werden nicht erweitert oder „mitgelöst“.

## 3. Tatsächlich vorhandene Audiohardware

### 3.1 Belegter Signalpfad

| Stufe | Befund | Primäre Evidenz |
|---|---|---|
| Schallwandler | Zwei `MSM381A3729H9BPC`, analoge MEMS-Mikrofone | Das offizielle [Tab5-Schaltbild](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf) enthält genau zwei Bauteile dieses Typs; die [Herstellerübersicht](https://en.memsensing.com/product/176.html) klassifiziert ihn als analog und nennt u. a. 65 dB SNR und 123 dB AOP. |
| Mikrofon-ADC/Frontend | Everest `ES7210`, vierkanaliger Audio-ADC; auf dem Board werden zwei analoge Mikrofone benutzt | [M5Stack Tab5-Dokumentation](https://docs.m5stack.com/en/core/Tab5), Schaltbild und [ES7210-Datenblatt](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/ES7210.PDF). |
| Codec-Steuerung | I²C-Adresse `0x40`, SDA G31, SCL G32; derselbe interne I²C-Bus wird auch für BMI270/RTC und weitere Boardbausteine benutzt | M5Stack-Pinmap im [Tab5-Dokument](https://docs.m5stack.com/en/core/Tab5#pinmap) und [Espressif Tab5-BSP API](https://github.com/espressif/esp-bsp/blob/master/bsp/m5stack_tab5/API.md). |
| Audiodaten | ES7210 `ASDOUT` nach ESP32-P4 G28; MCLK G30, SCLK/BCLK G27, LRCK/WS G29 | Offizielle Pinmap und Tab5-BSP. |
| ESP32-P4-Peripherie | I²S0 RX im M5Unified-Pfad, DMA-basierter Empfang | [M5Unified 0.2.19 `M5Unified.cpp`](https://github.com/m5stack/M5Unified/blob/0.2.19/src/M5Unified.cpp) und [`Mic_Class.cpp`](https://github.com/m5stack/M5Unified/blob/0.2.19/src/utility/Mic_Class.cpp); allgemeine [ESP32-P4-I²S-Dokumentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/i2s.html). |

Der entscheidende Befund lautet: **Das Tab5 hat keine direkt an den P4
angeschlossenen PDM-Mikrofone.** Die Kapseln liefern analog an den ES7210. Der
ES7210 digitalisiert, filtert und serialisiert; der ESP32-P4 empfängt PCM über
Standard-I²S oder TDM. PDM-Fähigkeiten des P4 sind vorhanden, gehören aber
nicht zum verdrahteten Tab5-Mikrofonpfad.

`ES7210 AEC` in den Produkttexten bedeutet hier die Eignung des Mehrkanal-
Frontends für Echo-Cancellation-Anwendungen. Weder M5Unified noch dieses
Projekt führen dadurch automatisch AEC oder Sprotz-Erkennung aus.

### 3.2 Relevante Pins und geteilte Ressourcen

| Funktion | ESP32-P4 | Bemerkung |
|---|---:|---|
| I²S MCLK | G30 | gemeinsamer Audiotakt für ES7210/ES8388 |
| I²S BCLK/SCLK | G27 | serieller Bittakt |
| I²S LRCK/WS | G29 | Frame-/Kanaltakt |
| Mikrofon-Daten `ASDOUT` | G28 | Eingang zum P4 |
| Lautsprecher-Daten `DSDIN` | G26 | Ausgang zum ES8388; für Aufnahme nicht benötigt |
| Audio-/System-I²C SDA | G31 | geteilt, u. a. ES7210, ES8388, IMU, RTC |
| Audio-/System-I²C SCL | G32 | geteilt, u. a. ES7210, ES8388, IMU, RTC |
| microSD SDMMC | G43/G44/G39/G40/G41/G42 | CLK/CMD/D0/D1/D2/D3; entspricht dem vorhandenen `SdMmcLogSink` |

Die SD-Pins im Projekt stimmen mit Board- und Libraryquellen überein; siehe
[`sprotz_logger_target.h`](M5Tab5_Digifant_Analyzer/src/sprotz_logger_target.h).
Audio-I²S und SDMMC verwenden unterschiedliche Peripherie und Pins, teilen
aber CPU, Interruptbudget, internen Speicherbus/PSRAM und letztlich Scheduler-
Zeit.

### 3.3 M5Unified 0.2.19 und Arduino-ESP32 3.3.11

Die lokal installierten, tatsächlich kompilierten Versionen sind:

- Arduino-ESP32 `3.3.11`, Board `esp32:esp32:m5stack_tab5`, ESP-IDF 5.5.5;
- M5Unified `0.2.19`;
- M5GFX `0.2.26`.

M5Unified konfiguriert für Tab5:

- I²S0, G30/G27/G29/G28;
- `input_stereo`, `over_sampling = 1`, `magnification = 2`;
- den ES7210 über I²C, wobei MIC1/MIC2 aktiv und MIC3/MIC4 abgeschaltet werden;
- Standard-I²S mit 16-Bit-Slots und DMA (`dma_buf_len = 128`,
  `dma_buf_count = 8` per Default);
- eine eigene `mic_task`, Default-Priorität 2, und zwei interne
  Aufnahme-Deskriptoren als Ping/Pong-Warteschlange.

Der bestehende Analyzer ruft derzeit nirgends `M5.Mic.begin()` oder
`M5.Mic.record()` auf. `M5.begin()` hinterlegt durch den standardmäßig
aktivierten `internal_mic` zwar die Tab5-Pin-/Codec-Konfiguration, es läuft aber
noch keine Audioaufnahme und keine Mic-Task.

Die öffentliche API kann `uint8_t` oder `int16_t`, mono oder stereo liefern.
Bei mono mittelt `Mic_Class` die zwei Eingangskanäle. Zusätzlich erfolgen in
der Library ein automatischer Nulloffset-Abgleich, die konfigurierte digitale
Verstärkung und optional ein IIR-artiger Noise-Filter. „Raw PCM“ bedeutet bei
dieser Route daher **uncompressed PCM am M5Unified-Ausgang**, nicht rohe
ADC-Wörter vor Library-/Codec-Verarbeitung.

Auch bei einer späteren Mono-Datei empfängt und verarbeitet der M5Unified-
Tab5-Pfad zunächst zwei 16-Bit-Kanäle. Bei 16 kHz sind das intern etwa
64.000 Byte/s I²S-Nutzdaten, bevor auf 32.000 Byte/s mono reduziert wird. Die
SD-Rate ist also mono, die Capture-/CPU-Arbeit nicht vollständig.

Das offizielle [M5Unified-Mikrofonbeispiel](https://github.com/m5stack/M5Unified/blob/0.2.19/examples/Basic/Microphone/Microphone.ino)
verwendet 16 kHz und `int16_t`. Es wurde im Rahmen dieses Reviews unverändert
für das genannte FQBN kompiliert:

```text
Sketch:           579076 Byte Flash
Globale Variablen: 28424 Byte internes RAM
Ergebnis:          Compile PASS
```

Das ist ein **Build-Nachweis, kein Target-Aufnahmenachweis**.

### 3.4 Sinnvoller Samplingpfad und bekannte Grenzen

Für den Hardware-Proof ist `M5.Mic` der kleinste und am besten durch die
vorhandene Library belegte Pfad. Für die integrierte Langzeitaufnahme gilt ein
Entscheidungsgate:

1. Zuerst M5Unified verwenden und echte Kontinuität, Kanalabbildung,
   Clipping sowie Timestamp-Jitter messen.
2. Nur wenn dessen zwei Buffer-Slots, interne Samplebearbeitung oder nicht
   exponierte DMA-Grenzzeit die Anforderungen verfehlen, `MicrophoneSource`
   intern durch direkten ESP-IDF-I²S-RX ersetzen. Das wäre Standard-I²S/TDM,
   nicht PDM.

Ein direkter I²S-Pfad darf nicht gleichzeitig mit `M5.Mic` I²S0 besitzen. Er
müsste außerdem die belegte ES7210-Konfiguration übernehmen oder einen
geeigneten Codec-Treiber benutzen. Das ist vor einem Hardware-Proof unnötige
Komplexität.

Weitere Grenzen:

- Das generische M5Unified-Beispiel beendet den Speaker vor der Aufnahme und
  warnt vor gleichzeitiger Benutzung. Für Phase 1 gilt deshalb: kein
  M5-Speaker während der Mikrofonaufnahme.
- ES7210 unterstützt laut Datenblatt 24-Bit-ADC, 8–100 kHz sowie I²S,
  left-justified, PCM/DSP und TDM. Das beweist nicht, dass jede Kombination in
  M5Unified 0.2.19 auf Tab5 getestet ist. Belegt ist dort der 16-Bit-Pfad.
- M5Unified erzeugt seine Semaphore und Mic-Task beim `begin()`. Diese
  Allokationen gehören in die Initialisierung, nicht in den laufenden
  Aufnahmezyklus.
- Audio-Codec-Initialisierung schreibt auf den mit der IMU geteilten I²C-Bus.
  Sie sollte vor dem Start der IMU-Task erfolgen. Wiederholtes
  `M5.Mic.end()/begin()` während einer Messfahrt ist zu vermeiden.
- Die Board-Spezifikation nennt 0–40 °C Betriebstemperatur. Der ES7210 allein
  ist höher spezifiziert; maßgeblich für den montierten Tab5 bleibt dennoch
  die Boardgrenze. Ein heißer Montageort nahe Motor ist deshalb ein echtes
  Target-Gate.
- Der Mikrofonhersteller nennt 123 dB AOP. Ein hoher Schalldruck kann trotzdem
  Mikrofon, Analogverstärker oder ADC clippen. Software-Nachverstärkung kann
  analoges Clipping nicht reparieren.

## 4. Ist-Architektur und Konsequenz für Audio

Der Analyzer hat derzeit diese relevanten Eigenschaften:

- Processing läuft mit Priorität 3, Display mit 2, Logger und IMU mit 1;
  siehe [`M5Tab5_Digifant_Analyzer.ino`](M5Tab5_Digifant_Analyzer/M5Tab5_Digifant_Analyzer.ino).
- ECU-Snapshots gehen über eine bounded SPSC-Queue, IMU-Samples über einen
  bounded Ring und Befehle über eine bounded MPSC-Queue.
- `SprotzLoggerService` ist alleiniger Projekt-Owner von `SD_MMC`,
  `SprotzLoggerCore` und der DLOG-Datei.
- `LoggerTimeMerge` hält Kandidaten 25 ms zurück und sortiert Start, Marker,
  ECU, IMU und Stop nach derselben monotonen Zeitbasis.
- DLOG V2 hat 26 Byte Recordheader, aber die aktuelle maximale Payload ist auf
  1112 Byte begrenzt; siehe
  [`sprotz_log_format.h`](M5Tab5_Digifant_Analyzer/src/sprotz_log_format.h).
- Ein IMU-Record belegt 66 Byte. Bei den vorhandenen 25 Hz sind das rund
  1.650 Byte/s bzw. 5,94 MB/h. 16-kHz-Mono-Audio ist schon ohne Header etwa
  19,4-mal so groß. Ein ECU-Snapshot belegt 1138 Byte; seine tatsächliche Rate
  hängt von der Messsession ab und wird hier nicht angenommen.
- Marker werden sofort geflusht; ansonsten wird nach 16 ECU-Snapshots oder
  zwei Sekunden geflusht.
- Ein Sink-Schreibfehler versetzt den gesamten Sprotz-Logger in
  `WriteError`/`StorageFull` und schließt dessen Datei.

Audio in `SprotzLoggerCore` würde damit aus einem heute kleinen, zeitgeordneten
Metadaten-/Messwertlogger einen Audio-Container machen. Es würde die
Audioverfügbarkeit zudem an denselben Fehlerzustand wie ECU/IMU koppeln. Genau
das widerspricht der geforderten Fehlerisolation.

## 5. Datenraten und Startkonfiguration

Alle Werte sind unkomprimiertes PCM, mono, 16 Bit. MB verwendet 10⁶ Byte,
MiB 2²⁰ Byte.

| Rate | Nutzdaten/s | Bitrate | MB/h | MiB/h | Frequenzbereich, ideal | Bewertung |
|---:|---:|---:|---:|---:|---:|---|
| 8 kHz | 16.000 B | 128 kbit/s | 57,6 | 54,93 | bis <4 kHz | kleinste Last; kann scharfe Impulse und höhere Abgas-/Mechanikanteile verlieren |
| **16 kHz** | **32.000 B** | **256 kbit/s** | **115,2** | **109,86** | bis <8 kHz | **empfohlener Startpunkt**; gute Reserve für Impuls-/Bandanalyse bei niedriger Last |
| 24 kHz | 48.000 B | 384 kbit/s | 172,8 | 164,79 | bis <12 kHz | nur wenn 16-kHz-Spektrum am oberen Rand relevante Energie zeigt |
| 32 kHz | 64.000 B | 512 kbit/s | 230,4 | 219,73 | bis <16 kHz | vorerst nicht begründet |
| 48 kHz | 96.000 B | 768 kbit/s | 345,6 | 329,59 | bis <24 kHz | in BSP/Codec technisch plausibel, fachlich und hinsichtlich Targetlast noch unbelegt |

Der ES7210-Digitalfilter hat laut Datenblatt im Single-Speed-Modus eine
Passbandgrenze von etwa `0,4535 × Fs`; real sind bei 16 kHz also ungefähr
7,26 kHz und bei 8 kHz ungefähr 3,63 kHz verzerrungsarm nutzbar. 16 kHz ist
deshalb der bessere Explorationskompromiss.

Bei Variante A wären wegen der aktuellen DLOG-Payloadgrenze beispielsweise
1024 Byte PCM pro Record praktikabel. Allein der 26-Byte-Recordheader erhöht
den Strom dann auf rund 59,06 MB/h bei 8 kHz bzw. 118,13 MB/h bei 16 kHz.
Wichtiger als diese wenigen Prozent sind die vielen kleinen VFS-Schreibaufrufe
und die Kopplung an Merge/Core.

Ein 30-Minuten-WAV-Segment enthält bei 16 kHz 57,6 MB, bei 8 kHz 28,8 MB.
Damit bleibt es weit unter der FAT32-/RIFF-Größengrenze und ist nach einem
Abbruch leichter reparierbar.

## 6. Variantenvergleich

### A. PCM als Records innerhalb DLOG

Mögliche Form: neuer Recordtyp `AUDIO_PCM`, jeder Record mit
`firstSampleIndex`, Rate/Formatschlüssel und höchstens etwa 1024 Byte PCM.

Vorteile:

- nur eine Datei und inhärent eine Recordreihenfolge;
- DLOG-Checksumme je Audioblock;
- explizite Audio-Gaps könnten als eigener Record dargestellt werden;
- nur ein physischer Projekt-Owner von SD bleibt erhalten.

Nachteile:

- `SprotzLoggerCore`, `LoggerTimeMerge`, Recordtypen, Decoder und Tests werden
  für einen datenintensiven Fremdstrom geändert;
- 16 kHz erzeugen etwa 31 Audio-Records/s bei 1024-Byte-Blöcken, zusätzlich zu
  IMU und ECU;
- vor jedem heutigen DLOG-V2-Write ruft der Core `sink_.freeBytes()` auf; der
  SD-Sink ermittelt dafür `SD_MMC.usedBytes()`. Dies etwa 31-mal/s zusätzlich
  auszuführen ist ein konkretes, nicht nur theoretisches Timingrisiko;
- jeder Audiofehler, `StorageFull` oder Write-Fehler kann den gesamten DLOG-
  Logger schließen;
- DLOG-Extraktion ist vor Hören, Audacity, Python/Scipy oder Spektrogrammen
  zwingend;
- alte Werkzeuge zeigen den neuen Kind bestenfalls als `UNKNOWN`; echte
  Semantik ist nicht rückwärts verfügbar;
- große Einzelfile, schlechtere Crash-Isolation und auf FAT32 spätestens vor
  4 GiB zu splitten;
- hoher Änderungs- und Regressionseinfluss gerade auf den bereits abgenommenen
  ECU-/IMU-/Eventpfad.

### B. Separater kontinuierlicher PCM/WAV-Strom

Mögliche Form: gleicher DLOG-Dateistamm, nummerierte WAV-Segmente, eigener
kleiner Zeitreferenz-Chunk.

Vorteile:

- DLOG V2, Decoder, `SprotzLoggerCore` und Time-Merge bleiben unverändert;
- große, ausgerichtete 4-KiB-Schreibblöcke statt vieler kleiner DLOG-Records;
- sofort mit Standard-Audiowerkzeugen hör- und analysierbar;
- Audio kann bei Quelle, Buffer, Datei oder Speicherplatz unabhängig ausfallen;
- Segmentsplitting, WAV-Reparatur und spätere Löschung von Audio sind
  unabhängig vom Messwertlog;
- keine Audio-Copies durch den DLOG-Recordbuilder und keine 1112-Byte-Grenze.

Nachteile:

- Korrelation muss explizit, aber einfach, gespeichert werden;
- zwei Filehandles und zunächst zwei SD-nutzende Tasks verletzen die heutige
  „ein Projekt-SD-Owner“-Topologie;
- FatFs/VFS/SDMMC serialisieren intern. Latenzspitzen eines Audio-Writes können
  den DLOG-Task indirekt warten lassen;
- Start/Stop beider Dateien ist kein atomarer Dateisystemvorgang. Status und
  Dateikennung müssen daher eindeutig sein.

### Bewertungsmatrix

| Kriterium | A: Audio im DLOG | B: separate WAV-Segmente |
|---|---|---|
| Runtime-/Timingrisiko | **hoch**: Audio läuft durch Core und Merge | **mittel**: eigener Pfad; gemeinsames SD bleibt Gate |
| SD-I/O-Last | gleiche Nutzdaten, aber kleine Records/mehr Calls | gleiche Nutzdaten, gut bündelbar in 4 KiB |
| RAM/PSRAM | bounded Audio-Ring trotzdem erforderlich | bounded Audio-Ring erforderlich; kein DLOG-Zusatzbuffer |
| Datenvolumen | leicht höher durch Recordheader | nahezu reine PCM-Menge |
| KWP/Processing | indirekt über vergrößerten Loggerpfad | kein Softwarepfad; nur gemessene System-/SD-Last |
| IMU/Logger | teilt Merge, Sink und Fehlerzustand | DLOG logisch unverändert; nur SD-Konkurrenz |
| Offline-Auswertung | Extraktion/Decoder zwingend | direkt hörbar, FFT-/Python-/Audacity-tauglich |
| Lange Fahrten | DLOG-Splitting neu zu entwerfen | 30-min-Segmente einfach und FAT32-sicher |
| Synchronisation | Recordtimestamp je Block | Sampleindex + monotone Segmentanker |
| SD-Fehler | kann ECU/IMU-DLOG mit Audio beenden | Audiozustand separat; physischer SD-Ausfall betrifft natürlich beide Dateien |
| Rückwärtskompatibilität | neuer Recordkind/Tooländerung | DLOG bytegenau unverändert; WAV ist zusätzliche Datei |

**Entscheidung: B.** Ein separater `AudioWriterTask` ist fachlich und
architektonisch sinnvoller als das Einschleusen in `SprotzLoggerCore`.

Die gemeinsame SD-Nutzung ist jedoch ein hartes Target-Gate. Beide Filehandles
müssen jeweils genau einer Task gehören. Für den ersten Integrationsversuch
sollen DLOG- und Audio-Writer dieselbe niedrige Priorität verwenden, damit
kein bewusst erzeugtes Low-/High-Priority-Mutexgefälle hinzukommt. Wenn der
Dual-Writer-Test den historischen Assert, unbounded DLOG-Latenz oder Drops
zeigt, wird dieser Befund **nicht als R7-Fix bearbeitet**. Dann ist der
Fallback ein einzelner physischer Storage-Owner mit zwei strikt getrennten
Lanes und DLOG-Vorrang; Audio bleibt auch dort außerhalb des
`SprotzLoggerCore` und seiner Formate.

## 7. Schlanke Zielstruktur

### 7.1 Komponenten, Ownership und Speicher

Keine allgemeine Media- oder Streaming-Abstraktion ist nötig. Vier kleine
Bausteine reichen:

1. `MicrophoneSource`: Tab5-spezifisches Starten und fortlaufendes Füllen eines
   bereits bereitgestellten Blocks. Liefert Sampleanzahl, globalen Sampleindex,
   Status und eine monotone Zeit an der Blockgrenze.
2. `AudioBlockPool`: fester Pool plus zwei bounded SPSC-Indexqueues (`free`,
   `ready`). Keine Sample-Copies zwischen Producer und Writer.
3. `AudioSampler`: besitzt Quelle und Producerseite. Er blockiert nie auf SD
   und führt weder Dateizugriff noch Ausgabe oder Formatierung aus.
4. `AudioWaveWriter`: besitzt genau seine WAV-Datei, Writerseite und
   Segmentzustand. Nur diese Task formatiert Header/Dateinamen und schreibt SD.

Block-Ownership:

```text
Free ──Sampler reserviert──► MicOwned ──Block fertig──► Ready
 ▲                                                     │
 └────────────Writer gibt zurück◄──── WriterOwned ◄────┘
```

Empfohlener Startwert:

- 2048 Mono-Samples je Block = 4096 Byte = 128 ms bei 16 kHz;
- 32 queued Blöcke = 128 KiB in PSRAM = 4,096 s Pufferzeit;
- zwei zusätzliche In-flight-Blöcke für M5Unified-Ping/Pong;
- kleine Deskriptoren/Atomics in internem RAM;
- M5Unified-DMA und Taskstack bleiben in internem DMA-fähigem RAM.

PSRAM wird einmal vor Taskstart reserviert und vollständig validiert. Scheitert
die Reservierung, bleibt Audio `Unavailable`; es gibt keinen kleineren
unbounded Heap-Fallback. Im laufenden Pfad sind `new`, `malloc`, wachsende
Container und `String` verboten.

### 7.2 Producer-/Callback-Kontext

Mit M5Unified produziert dessen interne `mic_task` in zwei vom Sampler
bereitgestellte Blöcke. Der Projekt-Sampler verwaltet nur die Blockzustände und
Sampleindizes. Seine Priorität wird vor `M5.Mic.begin()` explizit auf den
niedrigen, zu testenden Wert gesetzt; der Librarydefault 2 darf nicht
unbesehen übernommen werden.

Bei einem späteren direkten ESP-IDF-I²S-Pfad gilt:

- DMA-EOF-Callback im ISR-Kontext macht nur
  `esp_timer_get_time()`, Descriptor/Counter aktualisieren und Tasknotify;
- kein Copy, kein Float, kein SD, kein Dateiname, kein Logging und kein
  `printf` im Callback;
- `esp_timer_get_time()` ist laut
  [Espressif-Dokumentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/esp_timer.html)
  lockfrei und aus Tasks wie ISR nutzbar;
- PCM wird erst in der Sampler-Task aus internem DMA-RAM in den PSRAM-Pool
  überführt.

### 7.3 Prioritäten und KWP-Isolation

Startkonfiguration für die Messung, nicht als vorweggenommener Targetbeweis:

- Processing bleibt Priorität 3;
- Display bleibt 2;
- Audio-Sampler/M5-Mic zunächst 1;
- DLOG-Writer bleibt 1;
- Audio-Writer zunächst ebenfalls 1 und blockiert ausschließlich in seiner
  eigenen Task.

Eine Core-Pinnung wird nicht geraten, solange die reale Corebelegung von
Arduino-Loop, USB Host, SDMMC und vorhandenen Tasks nicht gemessen ist. Sie ist
eine mögliche spätere Optimierung, kein Startvertrag.

Audio darf niemals:

- aus KWP-/USB-Callbacks aufgerufen werden;
- Locks besitzen, die KWP, Parser oder Processing benötigen;
- Producer bei vollem Ring warten lassen;
- ECU-/IMU-Queues zurücksetzen oder deren Fehlerstatus verändern;
- während der Fahrt formatieren oder seriell ausgeben.

Target-Akzeptanz verlangt gegenüber einer identischen Fahrt ohne Audio: keine
neuen `rx_drops`, Frame-Drops, Parser-Rejects, Action-Failures, Sessionabbrüche
oder IMU-/Snapshot-Queue-Drops. Durchschnittswerte reichen nicht; maximale
Poll-/Write-Latenzen und High-Watermarks sind zu erfassen.

### 7.4 Overflow und Dropsemantik

Wenn kein freier Block vorhanden ist, wartet der Sampler nicht:

- der aktuelle Audioblock wird verworfen;
- `audioDroppedBlocks`, `audioDroppedSamples` und `audioRingHighWatermark`
  werden atomar erhöht;
- der globale Sampleindex läuft weiter;
- der nächste speicherbare Block trägt seinen echten höheren Sampleindex.

Der Writer darf getrennte Blöcke nicht nahtlos zusammenkleben, weil das die
Zeitachse komprimieren würde. Erkennt er einen Sampleindex-Sprung, schließt er
das bisherige WAV-Segment und beginnt nach Drain/Erholung ein neues Segment
mit neuem monotonem Anker. So ist die Lücke explizit; es werden weder erfundene
Nullsamples noch unmarkiert verkürzte Zeit erzeugt.

## 8. Dateiformat und eindeutige Zeitsynchronisation

### 8.1 WAV-Segmente

Vorgesehen ist Standard-RIFF/WAVE mit:

- PCM (`format = 1`), signed 16-bit little endian, mono;
- `fmt `-Chunk;
- kleinem anwendungsspezifischem `DGTM`-Chunk;
- `data`-Chunk mit ausschließlich PCM;
- maximal 30 Minuten pro Segment oder früherem Split bei Drop/Fehler.

Unbekannte RIFF-Chunks werden von üblichen WAV-Werkzeugen übersprungen; die
Datei bleibt direkt abspielbar. `DGTM` ist eine feste, versionierte Struktur,
kein JSON und keine wachsende Metadatenliste. Sie enthält mindestens:

- Schema und Flags (`cleanClose`, `timingQuality`, `gapBefore`);
- DLOG-`startedAtUs`, Session-Epoch und Transport-Generation bzw. den
  eindeutigen DLOG-Dateistamm;
- Segmentnummer;
- Sampleformat, Kanalabbildung und nominale Samplingrate;
- globalen `firstSampleIndex`;
- monotone Zeit `firstBoundaryUs` unmittelbar vor Sample 0;
- bei sauberem Close `finalBoundarySampleIndex` und `finalBoundaryUs`;
- kumulative Source-/Dropzähler beim Segmentstart und -ende.

Dateibeispiel:

```text
/sprotz/g1_s3_123456789_0.dlog
/sprotz/g1_s3_123456789_0.audio.000.wav
/sprotz/g1_s3_123456789_0.audio.001.wav
```

Der Header wird mit Platzhaltern geschrieben. Bei sauberem Stop werden RIFF-
und Data-Länge sowie Endanker per Seek gepatcht und danach geflusht. Nach
Reset/Stromverlust kann ein Hosttool die Data-Länge aus der physischen
Dateilänge reparieren; der Startanker bleibt erhalten. 30-Minuten-Splitting
begrenzt den nicht finalisierten Schaden und liegt weit unter FAT32/RIFF 4 GiB.

### 8.2 Zeitabbildung

Alle Zeitwerte stammen aus derselben lokalen monotonen Clock wie ECU/IMU und
Events: `esp_timer_get_time()`. Für ein Segment mit `N` Samples seien `B0` die
Grenze vor Sample 0 und `BN` die Grenze nach Sample `N-1`.

Bei sauberem Segment ergibt sich die Samplemitte ohne Clockannahme durch:

```text
t(i) = B0 + (i + 0,5) * (BN - B0) / N,    0 <= i < N
```

Fehlt wegen eines unsauberen Abbruchs `BN`, gilt nominal:

```text
t(i) = B0 + (i + 0,5) * 1.000.000 / sampleRate
```

Damit sind DLOG-Marker, RPM, G69, Last, Einspritzzeit, Lambda und IMU direkt
auf dieselbe Mikrosekundenachse projizierbar. Start-/Endanker pro 30-Minuten-
Segment genügen; es wird weder RTC/NTP noch eine komplexe Clock-Synchronisation
eingeführt.

Für M5Unified ist die beobachtete Fertigstellungszeit zunächst nur eine
Software-Approximation der DMA-Grenze. Der Hardware-Proof muss diese Unsicherheit
mit einem extern getriggerten akustischen Klick messen. Ist sie für die
Impulskorrelation zu groß oder nicht bounded, ist genau das das Gate zum
direkten I²S-EOF-Timestamp, nicht zu einer komplizierteren Clockarchitektur.

Ein zusätzlicher DLOG-`AUDIO_START`/`AUDIO_SYNC`-Record ist in Phase 1 nicht
nötig. Er würde DLOG-Schema und Tools verändern. Später kann ein kleiner
Statusrecord ergänzt werden, wenn ein eigener DLOG-Versionsschritt geplant
ist; Audio-PCM gehört trotzdem nicht in DLOG.

## 9. Start/Stop- und Fehlerverhalten

### 9.1 Lebenszyklus

- `M5.Mic`/Codec einmal kontrolliert vor IMU- und Displaytasks initialisieren
  und fortlaufend drainieren; kein wiederholtes Codec-I²C-Toggling pro Log.
- Ein erfolgreiches DLOG-`LOG_START` veröffentlicht non-blocking eine kleine
  `AudioStartRequest` mit dem originalen Command-Timestamp und dem tatsächlich
  gewählten DLOG-Dateistamm. Kann Audio nicht starten, bleibt DLOG `Recording`.
- Der Audiostartanker ist der tatsächliche erste Samplegrenzwert, nicht
  blind der Command-Timestamp. Beide Werte werden gespeichert.
- `SPROTZ_START`, `SPROTZ_STOP` und `MARKER` bleiben reine DLOG-Ereignisse;
  Audio läuft durch.
- DLOG-`LOG_STOP` wird nie auf Audio verzögert. Audio erhält non-blocking den
  Stop-Timestamp, beendet am nächsten definierten Blockrand, drainiert bereits
  fertige Blöcke, finalisiert WAV und meldet seinen eigenen Zustand.

### 9.2 Fehlerfälle

| Fehler | Audioverhalten | ECU/IMU/DLOG-Verhalten |
|---|---|---|
| Ring voll | Block verwerfen, Sampleindex weiter, Dropzähler; nächstes Segment nach Lücke | unverändert |
| SD kurz langsam/blockiert | Writer blockiert nur in eigener Task; Ring absorbiert bis ca. 4 s | Queues/Logger müssen im Targetgate dropfrei bleiben |
| SD dauerhaft langsam | Ringoverflow und Segmentlücke; bei wiederholtem Overflow Audio für Session auf `Failed` | Aufnahme-/Verarbeitung läuft weiter |
| Audio-Datei voll / Reserve erreicht | Segment sauber schließen, `StorageReserve` melden, keine weiteren Audio-Writes | DLOG erhält reservierten Restplatz und entscheidet weiterhin selbst |
| SD fehlt bei Start | Audio `NoStorage`, keine Datei | bestehender Logger verhält sich wie bisher |
| Karte wird entfernt | laufender Audio-Write scheitert, Filehandle nur in Audio-Task schließen; kein `SD_MMC.end()` aus Audio | Erwerb von ECU/IMU läuft; DLOG kann wegen desselben physischen Mediums unabhängig ebenfalls scheitern |
| Mikrofon-/I²S-Fehler | Segment soweit möglich finalisieren, Source-Error zählen, kein automatischer Reinit-Loop während Session | unverändert |
| Audio-Task fällt zurück | High-Watermark/Writer-Lag, dann definierte Drops/Segmentierung | unverändert; Testgate muss KWP-/IMU-Nichtbeeinflussung zeigen |
| Taskerzeugung/PSRAM fehlt | Audio `Unavailable`, kein Laufzeitfallback mit Heapwachstum | Analyzer startet ohne Audio weiter |
| Reset/Stromverlust | letztes Segment ggf. unfinalisiert, am Host über Dateilänge reparierbar | bestehende DLOG-Crashcharakteristik unverändert |

Der Audio-Writer prüft vor jedem neuen Segment und periodisch den freien Platz
und hält initial **128 MiB Reserve** für DLOG/FAT-Operationen zurück. Dieser Wert
ist ein konservativer Startparameter und muss mit realen Fahrtdauern geprüft
werden. Audio darf den letzten freien Speicher nie beanspruchen.

`flush()` ist nicht pro Block zulässig. Startwert: WAV-Daten in 4-KiB-Blöcken,
Metadaten/Datei etwa alle fünf Sekunden sowie bei Split/Stop flushen. Das
reduziert VFS-/FatFs-Lock- und Kartenlatenz, begrenzt aber den möglichen Verlust
des jüngsten Audioendes. Die konkrete Periode ist ein SD-Targetmesswert.

## 10. Roh-Audio oder nur Features?

Für die erste Phase ist **PCM zwingend sinnvoller als ausschließlich
abgeleitete Features**:

- Das gesuchte akustische Muster ist noch unbekannt. RMS/Peak verlieren
  Frequenz-, Phasen- und Impulsforminformationen irreversibel.
- Bandenergie setzt vorab gewählte Bänder voraus; falsche Bänder können das
  relevante Signal vollständig verdecken.
- Impulsrate benötigt bereits einen Schwellwert und ist damit eine frühe,
  noch unbelegte Erkennung.
- Nur PCM erlaubt später erneutes Filtern, Hören, Spektrogramme, andere
  Fenstergrößen und einen reproduzierbaren Vergleich mit Ground Truth.

Zulässig sind zunächst nur billige Gesundheitsmetriken pro Block oder Session:
Min/Max, Anzahl Samples nahe Full Scale, Dropzähler und optional RMS. Sie
ersetzen PCM nicht und gehören nicht in den ISR. FFT, Bandenergie,
Impulserkennung und automatische Sprotz-Klassifikation bleiben Offline-Phase 5.

## 11. Hardware- und Lasttests vor Integration

Jeder Test speichert beide Kanäle zunächst getrennt oder wertet sie getrennt
aus. Erst danach wird entschieden, ob mono durch linken Kanal, rechten Kanal
oder Mittelwert entsteht. Blindes Mitteln könnte bei mechanischer Kopplung oder
Phasenunterschieden Information auslöschen.

1. **Mikrofon grundsätzlich lesen:** Boarderkennung, `M5.Mic.begin()`, beide
   Kanäle, Samplezähler, konstante Rate, kein All-zero/konstanter Wert. Noch
   kein Analyzer und möglichst kein SD im Hotpath.
2. **Leerlauf aufnehmen:** 30–60 s am vorgesehenen Gain; WAV am Host hören,
   DC, RMS, Peak, Spektrum und Kanalgleichheit prüfen.
3. **Drehzahlstufen:** je mindestens 30 s bei etwa 1000/1500/2000/2500 rpm;
   Tachoreferenz protokollieren, aber noch keine automatische Klassifikation.
4. **Clipping/Dynamik:** Full-Scale-Häufigkeit, AEC-/HPF-/Gain-Effekt und
   wiederholbare laute Impulse prüfen. Bei Clipping zuerst analogen ES7210-Gain
   untersuchen, nicht nur PCM digital herunter skalieren.
5. **Vorgesehener Montageort:** Tab5 in finaler Orientierung/Gehäuse/Halterung
   nahe Motor testen. Wind, Körperschall, Resonanzen und verdeckte Mic-Ports
   dokumentieren.
6. **CPU/RAM/PSRAM/SD:** mit und ohne Audio vergleichen: Tasklaufzeiten,
   Stack-HWM, Heap/PSRAM vor/nach, Ring-HWM/Drops, SD-Write-Maximum und Flush-
   Maximum, KWP-/Frame-/IMU-/Loggerdrops.
7. **Temperatur/Vibration:** Gehäusetemperatur über die Fahrt messen und gegen
   die 0–40-°C-Boardgrenze halten; Halterung auf Lockerung, Kontaktprellen der
   SD und vibrationsinduzierte Sättigung prüfen.

Für die Zeitreferenz ist zusätzlich ein externes, GPIO-getriggertes
Klick-/Piezo-Signal sinnvoll: Triggerzeit mit `esp_timer_get_time()` erfassen,
akustischen Peak suchen und Offset/Jitter über viele Wiederholungen bestimmen.
Der interne Speaker ist dafür wegen des geteilten Audiopfads nicht zu benutzen.

## 12. Priorisierter Implementierungsplan

### Phase 1 — Mikrofon-Hardware-Proof

#### Schritt 1: eigenständiger Dual-Channel-Probe

- **Ziel:** Beweisen, dass beide real verbauten Mikrofone über M5Unified
  kontinuierliche 16-kHz-/16-Bit-Samples liefern; Kanalreihenfolge,
  DC-Verhalten, Pegel und Clipping erfassen.
- **Komponenten/Dateien:** neu `M5Tab5_Audio_Probe/M5Tab5_Audio_Probe.ino`;
  Analyzer-Dateien bleiben unberührt.
- **Risiken:** M5Unified-internes Gain/Offset, verdeckter Mikrofonport,
  Speaker-/I²S-Konflikt, PSRAM-Allocation.
- **Hosttests:** WAV-/PCM-Längen-, Header- und Sampleformatprüfung; deterministische
  Min/Max/RMS-/Clipping-Berechnung mit Fixture.
- **Targettests:** Stille, Sprache/Klatschen, beide Kanäle, 5–30 s; danach die
  sechs oben genannten Hardwaretests.
- **Gates:** reale Hardware **ja**; SD zunächst **nein**, danach kurze SD-Datei;
  ECU **nein**; Motor **ab Schritt 2–5 ja**.

#### Schritt 2: Samplingrate und Monoabbildung entscheiden

- **Ziel:** 8 und 16 kHz sowie Stereo-Kanäle am Montageort vergleichen;
  16-kHz-mono nur bestätigen, wenn kein Clipping und ausreichend Nutzsignal.
- **Komponenten/Dateien:** Probe-Sketch und ein kleines Offline-Notebook/Skript,
  noch keine Analyzer-Integration.
- **Risiken:** Aliasanteile, Motor-/Halterungsresonanz, Kanal-Auslöschung durch
  Mittelung.
- **Hosttests:** Spektren, Peak/RMS, Kanal-Korrelation, resamplingfreier Vergleich.
- **Targettests:** Leerlauf und 1000/1500/2000/2500 rpm, finaler Montageort.
- **Gates:** Hardware **ja**, SD **ja**, ECU optional nur als Referenz,
  Motor **ja**.

### Phase 2 — Audio-Sampler und bounded Buffer

#### Schritt 3: hosttestbarer Blockpool und Zustandsautomat

- **Ziel:** feste Ownership `Free→MicOwned→Ready→WriterOwned→Free`, monotone
  Sampleindizes, Dropzähler und gap-sichere Semantik ohne Heap im Hotpath.
- **Komponenten/Dateien:** künftig
  `src/audio_types.h`, `src/audio_block_pool.h`, `src/audio_sampler.h`;
  `tests/audio_block_pool_test.cpp`, `tests/audio_sampler_test.cpp`.
- **Risiken:** ABA/Index-Reuse, falsche Memory-Order, Overflow ohne sichtbaren
  Sampleindexsprung, PSRAM-Latenz.
- **Hosttests:** Wrap-around, Producer schneller/langsamer als Consumer,
  Overflow, Source-Error, Start/Stop-Races, Sanitizer.
- **Targettests:** Synthetic Source, danach Mic ohne SD-Writer; CPU, Stack,
  Pool-HWM und Langlauf mindestens 30 min.
- **Gates:** Hardware für Mic-Teil **ja**; SD **nein**; ECU **nein**;
  Motor **nein**.

#### Schritt 4: `MicrophoneSource` mit M5Unified anbinden

- **Ziel:** zwei In-flight-Blöcke lückenlos nachfüllen, Source außerhalb eines
  Logs drainieren, Blockgrenzen timestampen und keine I²C-Reinitialisierung
  während laufender Tasks durchführen.
- **Komponenten/Dateien:** künftig `src/m5tab5_microphone_source.h` und
  minimaler Setup-Hook in `.ino`, zunächst per Buildflag standardmäßig aus.
- **Risiken:** M5Unified-Zwei-Slot-Semantik, Scheduler-Jitter, Mic-Task-Priorität,
  Library-interne Samplebearbeitung.
- **Hosttests:** Fake-Source-Vertrag und Compile-Guard; M5Unified bleibt hinter
  Targetadapter.
- **Targettests:** 60 min discard/PSRAM-Ring ohne SD, keine Drops; externer
  Klicktest für Timestamp-Offset/Jitter.
- **Gates:** Hardware **ja**; SD **nein**; ECU zunächst **nein**, danach
  K409/ECU-Smoke **ja**; Motor **nein**.

Wenn hier Kontinuität oder Timing scheitert, folgt ein eigener, begrenzter
Entscheidungsschritt für ESP-IDF-I²S-RX. Er ist kein automatischer Teil von
Schritt 4.

### Phase 3 — Audio-Dateiformat und Zeitsynchronisation

#### Schritt 5: WAV- und `DGTM`-Format ausschließlich am Host

- **Ziel:** feste, versionierte WAV-Struktur, Start-/Endanker, Reparatur eines
  nicht finalisierten Segments und Sample→DLOG-Zeitabbildung beweisen.
- **Komponenten/Dateien:** künftig `src/audio_wave_format.h`,
  `tests/audio_wave_format_test.py`, `tools/inspect_audio_log.py`.
- **Risiken:** RIFF-Padding/Endian/Längenpatch, unbekannte Chunks in Tools,
  Off-by-one an Samplegrenzen.
- **Hosttests:** Python `wave`/Audacity-kompatible Fixtures, sauberes Close,
  Truncation/Reparatur, Segment-/Gap-Merge, Driftformel.
- **Targettests:** keine für den reinen Formatkern.
- **Gates:** Hardware/SD/ECU/Motor **nein**.

#### Schritt 6: separater Audio-Writer und SD-Fehlerisolation

- **Ziel:** 4-KiB-PCM-Blöcke in 30-min-Segmente schreiben, fünfsekündiges
  Flush, 128-MiB-Reserve, Split bei Gap, eigener Status; DLOG-Status nie setzen.
- **Komponenten/Dateien:** künftig `src/audio_wave_writer.h`,
  `src/audio_writer_target.h`, `tests/audio_writer_test.cpp`; noch keine
  Änderung an `SprotzLoggerCore` oder `sprotz_log_format.h`.
- **Risiken:** SD-Latenzspitzen, Kartenentfernung, Headerpatch, zwei VFS-
  Filehandles und der offene historische Mutex-Assert.
- **Hosttests:** Fake-Sink mit Short-write, Full, Flush-/Close-Fehler,
  verzögertem Consumer, Splits und exakten Dropzählern.
- **Targettests:** zunächst Audio-only SD 2 h; danach DLOG+Audio mit synthetischen
  ECU-Snapshots/IMU, wiederholte Start/Stop/Marker, langsame/nahezu volle Karte.
- **Gates:** Hardware **ja**; echte SD **ja**; ECU zunächst **nein**;
  Motor **nein**. Bei Assert oder DLOG-Drops stoppen und Befund getrennt von
  R7 dokumentieren.

### Phase 4 — gemeinsame ECU-/IMU-/Audio-Messfahrt

#### Schritt 7: minimaler LOG_START/LOG_STOP-Control-Link

- **Ziel:** Audio nach erfolgreichem DLOG-Start non-blocking mit demselben
  Dateistamm/Startwert anfordern; DLOG-Stop niemals auf Audio warten lassen.
- **Komponenten/Dateien:** neue bounded `AudioControlQueue`/`AudioStatus`;
  minimale Integration in `sprotz_logger_target.h`, UI nur für lesbaren
  Audiostatus. Keine Audio-Payload und kein neuer Recordkind in DLOG.
- **Risiken:** verlorener Start-/Stop-Control, abweichende Lifecycle-Zustände,
  Status-UI-Rückwirkung.
- **Hosttests:** Start DLOG erfolgreich/Audio fehlschlägt; Stop bei vollem Ring;
  doppelte Befehle; Eventmarker verändern Audio nicht.
- **Targettests:** reale SD, IMU, Touch/Serial, K409 zunächst ohne Motor bzw.
  Prüfstand; vollständige Dateien am Host korrelieren.
- **Gates:** SD **ja**, K409/ECU **ja**, Motor zunächst **nein**.

#### Schritt 8: gemeinsame Messfahrt und Langzeitgate

- **Ziel:** akustische Ground-Truth-Daten mit RPM, G69, Last,
  Einspritzzeit, Lambda, IMU und manuellen Sprotz-Markern gewinnen.
- **Komponenten/Dateien:** keine neue Erkennung; nur Konfiguration,
  Messprotokoll und Offline-Inspektionstool.
- **Risiken:** reale SD-Latenz, thermische Grenze, Vibration, Wind,
  Motorgeräusch übersteuert, KWP-Regressionswirkung.
- **Hosttests:** DLOG/WAV-Stammzuordnung, Zeitachsenmerge, Segment-/Gap-Bericht,
  Audio hörbar und Spektrogramm reproduzierbar.
- **Targettests:** gestufte Fahrten 15 min, 60 min, dann längste geplante
  Fahrt; mehrere manuelle `SPROTZ_START/STOP` und `MARKER`; Temperatur und
  Task-/Queue-/Droptelemetrie sichern.
- **Gates:** reale ECU **ja**, reale SD **ja**, realer Motor/Fahrt **ja**.
  Bestehen nur bei unverändert stabiler KWP-/Processing-/IMU-Pipeline.

### Phase 5 — erst später Offline-Sprotz-Erkennung

#### Schritt 9: Offline-Exploration, ausdrücklich nicht Targetcode

- **Ziel:** Hörbarkeit, Spektrogramme, impulsive Muster und Korrelationen gegen
  manuelle Ground Truth quantifizieren; erst danach Features/Modell bewerten.
- **Komponenten/Dateien:** separates Offline-Analyseverzeichnis/-skript;
  keine Änderung der Firmware-Erkennung.
- **Risiken:** Ground-Truth-Latenz menschlicher Marker, Overfitting auf eine
  Fahrt/Montage, Verwechslung von Schalt-, Vibrations- und Abgasimpulsen.
- **Hosttests:** reproduzierbare Windowing-/Filterparameter, train/test nach
  Fahrt trennen, Fehlstellen aus Dropmetadaten maskieren.
- **Targettests:** keine automatische Erkennung; nur weitere Datenerhebung bei
  Bedarf.
- **Gates:** ECU/SD/Motor nur für zusätzliche Datensätze **ja**; Deployment-
  Gate ausdrücklich **später**.

## 13. Abnahmekriterien vor einer Produktintegration

Die erste gemeinsame Aufnahme ist erst freigegeben, wenn alle Punkte erfüllt
sind:

- beide Mikrofonkanäle sind identifiziert, nutzbar und am Montageort nicht
  dauerhaft geclippt;
- 16 kHz/16 Bit/mono ist durch Spektrum und Pegel begründet;
- Audio-Ring und Writer sind bounded, Heap bleibt nach Start konstant;
- keine Formatierung/Serialausgabe und keine Allokation im Audio-Hotpath;
- Dropzähler, Sourcefehler, Writer-Lag und Ring-HWM sind unabhängig sichtbar;
- WAV ist direkt lesbar; DLOG/WAV-Zeitbezug und Gap-Splits werden vom Hosttool
  geprüft;
- SD-Entfernung, Full, Short-write, langsame Karte und Reset erzeugen keinen
  Audio→KWP-/IMU-Steuerpfad;
- reale KWP-/ECU-Gates zeigen null neue RX-/Frame-/Snapshot-/IMU-Drops und
  keine zusätzliche Sessionunterbrechung;
- kein `xTaskPriorityDisinherit`-Assert tritt im Dual-Writer-Gate auf. Ein
  Auftreten blockiert die Audiointegration, ändert aber weder R7-Status noch
  dessen Ursachenanalyse;
- Temperatur bleibt innerhalb der Tab5-Spezifikation und die Halterung bleibt
  vibrationsfest.

## 14. Klare Empfehlung

- **Bevorzugte Audioarchitektur:** separate WAV-Segmente über
  `MicrophoneSource → bounded AudioBlockPool → AudioWriterTask → SD`; Audio
  bleibt außerhalb von DLOG-Recordformat, `LoggerTimeMerge` und
  `SprotzLoggerCore`.
- **Empfohlene Samplingrate:** 16 kHz, signed PCM 16 Bit, mono. Im Hardware-
  Proof beide Kanäle zunächst separat aufnehmen und die Monoabbildung erst
  danach wählen.
- **Erwartete Datenrate:** 32.000 Byte/s = 256 kbit/s = 115,2 MB/h
  (109,86 MiB/h); 57,6 MB je 30-Minuten-Segment.
- **Synchronisation:** gleicher DLOG-Dateistamm und `DGTM`-Chunk mit
  DLOG-`startedAtUs`, globalem Sampleindex sowie erster/letzter
  `esp_timer_get_time()`-Samplegrenze. Nominale Rate als Fallback, lineare
  Start-/Endankerabbildung bei sauberem Segment.
- **Größtes technisches Risiko:** SD-/FatFs-Latenz und Lockverhalten bei zwei
  Dateien/Tasks vor dem Hintergrund des nicht reproduzierten
  `xTaskPriorityDisinherit`-Asserts; danach Clipping/Vibration am realen
  Montageort.
- **Erster konkreter Luna-Schritt:** eigenständigen
  `M5Tab5_Audio_Probe` bauen und auf echter Hardware beide ES7210-Kanäle bei
  16 kHz/16 Bit ohne Analyzer-Integration prüfen. Erst nach diesem Gate werden
  Sampler/Ring oder Writer entworfen.
