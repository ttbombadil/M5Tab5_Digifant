# M5Tab5 Architecture Analysis – 5.6Sol

**Projekt:** KWP1281-Datenlogger für VW 2E / Digifant 1.7 auf M5Stack Tab5 (ESP32-P4)  
**Analysestand:** 20.08.2026, aktueller Workspace auf Branch `main`  
**Methode:** statische Analyse aller Firmware- und Tool-Quellen im Workspace, Abgleich mit Build-Konfiguration und der tatsächlich eingebundenen EspUsbHost-Version 2.7.8. Es wurden keine bestehenden Dateien oder Quelltexte verändert.

## Bewertungsbegriffe

Die Befunde verwenden durchgängig vier Evidenzklassen:

- **Nachgewiesenes Problem:** direkt aus dem aktuellen Code ableitbare Eigenschaft oder Verletzung der geforderten Architektur. Das bedeutet nicht automatisch, dass im bisherigen Fahrzeugtest bereits ein sichtbarer Ausfall auftrat.
- **Wahrscheinliches Problem:** der Code enthält einen plausiblen Ausfallmechanismus; Eintritt und Häufigkeit hängen von nicht gemessenen Laufzeiten oder externem Verhalten ab.
- **Potenzielles Risiko:** nur unter zusätzlichen, aktuell nicht belegten Bedingungen relevant.
- **Verbesserungsvorschlag:** Zielzustand, kein behaupteter Fehler des Istzustands.

Die Priorität bewertet die Auswirkung auf den funktionskritischen KWP1281-Empfang, nicht nur die Eintrittswahrscheinlichkeit.

## 1. Executive Summary

Die zentrale Anforderung einer vollständigen Entkopplung des zeitkritischen Digifant-Empfangs von Verarbeitung, Logging und UI ist im aktuellen Design **nicht erfüllt**. Im Anwendungscode gibt es keine eigene Protokoll-/RX-Task, keine Queue für vollständige Frames und keine Snapshot-Übergabe an nachgelagerte Verbraucher. Fast alles läuft sequenziell im Arduino-`loop()`:

`M5.update()` → serielle Kommandos → `EcuInitTester::update()` → Blockaufbau → Parsing/Decoding → synchrone Logs → Dashboard-Zustand → gegebenenfalls Display-Redraw → `delay(2)`.

Eine begrenzte physische Entkopplung existiert nur in der externen EspUsbHost-Bibliothek. Deren FreeRTOS-USB-Client-Task empfängt USB-Daten und legt CDC-Nutzbytes in einen spinlock-geschützten Ringpuffer. Dieser Ring hat standardmäßig 512 Byte bzw. effektiv 511 Byte Kapazität und verwirft bei Vollstand still das älteste Byte. Die Anwendung vergrößert den Ring nicht und erfasst keinen Overflow-Zähler. Der Ring schützt vor kurzen Loop-Pausen, löst aber das wichtigere Problem nicht: KWP1281 benötigt für fast jedes empfangene Byte zeitnah die invertierte Antwort `~b`; diese Antwort wird erst vom Anwendungscode im `loop()` erzeugt. Ein gepuffertes Byte ist daher nicht gleichbedeutend mit einem rechtzeitig quittierten Byte.

Besonders kritisch sind folgende nachgewiesene Eigenschaften:

1. Empfang, Byte-ACK, Protokollzustand, Parsing, Decoder, Dashboard-Setter und umfangreiches Logging teilen denselben Ausführungskontext (`EcuInitTester.cpp:452–601`). Ein langsamer Consumer verlängert damit unmittelbar die Reaktionszeit des Protokolls.
2. `Console::printf()` endet stets in synchronem `Serial.println()` (`Console.cpp:35–40, 82–98`). Im RX-Pfad wird unter anderem für fast jedes Byte geloggt (`EcuInitTester.cpp:518`). Display-Redraw wurde verschoben, serielles Logging aber nicht.
3. Das UI-Gate `!tester.isBusyReceiving()` erkennt nur ausgewählte Zustände und einen bereits begonnenen Block. Im verbundenen Zustand zwischen zwei Blöcken ist es false; genau dort kann ein kompletter Redraw starten, unmittelbar bevor das nächste ECU-Byte eintrifft (`M5Tab5_Digifant_Proto.ino:185–200`, `EcuInitTester.h:27–33`). Es gibt keine Deadline- oder Bus-Idle-Garantie.
4. USB-Geräte-Callbacks loggen aus dem EspUsbHost-Task in dieselbe globale `Console`, die der Loop gleichzeitig verändert und liest (`UsbCdcLink.cpp:7–16`, `Console.cpp:35–63`, `Dashboard.cpp:940–955`). `Console` besitzt keinen Mutex und ist nicht multi-producer-sicher. Das ist eine echte Daten-Race-/Konsistenzlücke.
5. Die KWP-Sendefunktion ignoriert die Rückgabe von `_link.write()`, wartet trotz fehlgeschlagenem Senden weiter auf Echo/ACK und liefert immer `true` (`EcuInitTester.cpp:111–170`). Ein TX-Fehler wird nicht sauber in den Sessionzustand propagiert.

Positiv sind die klare `SerialLink`-Abstraktion, feste RX-/Header-Puffer, Längen- und Endbyteprüfungen, expliziter Blockzähler, ein spinlock-geschützter Bibliotheksring, überlaufrobuste `millis()`-Differenzen und eine bereits vorgenommene Drosselung des Display-Redraws. Die dokumentierten 180-Sekunden-Fahrzeugläufe ohne Protokollfehler belegen Normalfall-Funktionalität. Sie belegen jedoch weder Worst-Case-Latenz noch Verhalten bei UI-, Logging-, USB- oder Speicherstress.

**Gesamturteil:** Im Normalbetrieb offenbar funktionsfähiger Prototyp, aber keine Architektur, die den Empfang „unter allen Umständen“ gegen langsame nachgelagerte Arbeit isoliert. Für das formulierte Zuverlässigkeitsziel ist eine eigene hochpriorisierte KWP-I/O-Task mit exklusivem Linkbesitz und kopierender Frame-Übergabe erforderlich.

## 2. Verstandene Gesamtarchitektur

### 2.1 Ausführungskontexte

| Kontext | Erzeugung / Quelle | erkennbare Konfiguration | Verantwortung | gemeinsamer Zustand |
|---|---|---:|---|---|
| Arduino-`loop()`-Task | Arduino-Core | im Projekt nicht explizit festgelegt | Touch, serielle Kommandos, Link-Polling, KWP-Zustandsmaschine, Byte-ACK, Parsing, Decoder, Logging-Aufrufe, UI-Rendern | praktisch gesamter Anwendungszustand |
| EspUsbHost-Task | EspUsbHost 2.7.8, `EspUsbHost::begin()` | Default Stack 8192, Priorität 5, `tskNO_AFFINITY` | USB-Host-Ereignisse und Start des Clientkontexts | Host-/Gerätezustand |
| EspUsbHost-Client-Task | EspUsbHost 2.7.8 intern | gleiche Bibliothekskonfiguration; exakte Laufzeitzuordnung extern | Transfer-Completions, CDC-Datenbehandlung, Callbacks, Befüllen des CDC-Rings | CDC-Ring und Callback-Ziele |
| ESP-IDF-/Treiberkontexte | ESP-IDF/Arduino-Core | nicht im Workspace definiert | USB-Hardware, Timer/Ticks, Display-/Touch-Treiber | bibliotheksintern |

Im Anwendungscode gibt es kein `xTaskCreate`, keine FreeRTOS-Queue, kein Event Group, keine Task Notification, kein anwendungseigenes Semaphore und keine ISR. Die Hardware-Interrupts und deren Treiberqueues liegen vollständig unterhalb der sichtbaren Abstraktion in ESP-IDF, USB-Host und M5Unified.

### 2.2 Komponenten und Verantwortlichkeiten

| Komponente | Rolle | Architekturbeobachtung |
|---|---|---|
| `SerialLink` | schmales byteorientiertes Transportinterface | gute Trennung für Simulation/Hardware; enthält aber weder Fehlerdetails noch Deadline-/Async-Semantik |
| `UsbCdcLink` | Adapter EspUsbHost/CDC → `SerialLink`; FTDI-Steuerung | besitzt `_host` und `_serial`; KWP-spezifisches Break/Latency leckt in den Transport; umgeht Bibliothekskapselung per `#define private public` |
| `EcuInitTester` | 5-Baud-Init, KWP-Sitzung, RX/TX, Parsing, Decoder, Messgruppensteuerung | zu viele Verantwortlichkeiten; zentrale Kopplungsstelle |
| `ConnectivityTester` | Roh-Link-Test | nicht Teil des Live-KWP-Pfads; begrenzt Reads pro Update auf 64 |
| `SimulatedLink` | request-getriggerter Replay-Transport | eigener 65-Byte-Puffer; simuliert nicht den echten Byte-Handshake und nicht die USB-Nebenläufigkeit |
| `Console` | Serial-Ausgabe plus 40×96-Zeichen-Zeilenring | globale Instanz; serieller Output synchron; Ring nicht threadsicher |
| `Dashboard` | Touch, Zustandsmodell, Historie, Rendering | globale Instanz; Setter direkt aus Decoder; große Sprites im PSRAM; Rendern im Loop |
| `ReplayData.h` | statischer Capture-Datensatz | immutable, klare Lebensdauer; wird nur im Simulationsbuild benutzt |
| `M5Tab5_USB_Check` | separater Test-Sketch | nicht Teil der Live-Anwendung; enthält eigene blockierende Diagnose-Loop |
| `make_replay_dataset.py` | Offline-Konvertierung Capture → CSV | kein Firmware-Concurrency-Einfluss |

### 2.3 Globale und singleton-artige Zustände

- `Console console` in `Console.cpp:7`.
- `Dashboard dashboard` in `Dashboard.cpp:6`.
- je nach Build genau ein globales `serialLink` in `M5Tab5_Digifant_Proto.ino:47–51`.
- genau ein globaler `tester` in `M5Tab5_Digifant_Proto.ino:53–59`.
- `g_linkReady` und mehrere funktionslokale statische Zeit-/UI-Zustände im Sketch.
- EspUsbHost verwaltet bibliotheksintern Geräte, Transfers, Tasks, Callbacks und CDC-Ring.

Die globalen Lebensdauern verhindern im aktuellen statischen Aufbau Dangling-Referenzen zwischen `tester`, `serialLink`, `console` und `dashboard`. Sie erschweren aber Isolation, Tests, Ownership-Dokumentation und spätere Mehrtask-Nutzung.

### 2.4 Puffer, Queues und Synchronisation

| Speicher / Kanal | Größe | Producer | Consumer | Ownership / Schutz | Vollverhalten |
|---|---:|---|---|---|---|
| EspUsbHost CDC-Ring | 512 B alloziert, effektiv 511 B | USB-Client-Task | Loop via `available/read` | Bibliothek; `portMUX_TYPE` | ältestes Byte wird still verworfen |
| `_rxBlockBuf` | 128 B | `EcuInitTester::update` | `parseBlock`, danach Ablaufsteuerung | exklusiv Loop | akzeptierte Protokolllänge max. 64 Folgbytes; kein Overflow im erlaubten Pfad |
| `_groupHeader[5][64]` | 320 B | `parseBlock` | `decodeNumberedGroup` | exklusiv Loop | nur Kopie bei `payloadLen <= 64`; vorheriger Header wird ersetzt |
| `Console::_lines` | 40 × 96 B | Loop und USB-Callbacks | Dashboard/Console-Redraw | global, **kein Schutz** | älteste Zeile wird logisch überschrieben |
| `Dashboard::_history` | 240 Samples | Dashboard-Update | Scope-Renderer | exklusiv Loop im Istdesign | zyklisches Überschreiben, beabsichtigt |
| `SimulatedLink::_rxBuffer` | 65 B | Simulation-Update | Tester/ConnectivityTester | exklusiv Loop | neue Anfrage verwirft ungelesene alte Antwort |
| USB-TX-One-shot-Transfers | dynamisch je Schreibaufruf | Loop | USB-Treiber | EspUsbHost | Allokations-/Submitfehler als `false/0`; Anwendung wertet sie meist nicht aus |

Es gibt **keine anwendungseigene Queue für rohe Bytes oder vollständige Frames**. Ebenso fehlen Overflow-Telemetrie, High-Water-Mark, Drop-Zähler und eine explizite Backpressure-Policy.

### 2.5 Kontrollfluss im Live-Modus

```text
setup
  ├─ Serial + M5 + Console + Dashboard initialisieren
  └─ UsbCdcLink.begin
       ├─ USB-Callbacks registrieren
       ├─ EspUsbHost-Tasks starten
       └─ CDC-Ring anlegen / CDC konfigurieren

loop (kooperativ, Sollpause 2 ms)
  ├─ M5.update / Touch-Treiber
  ├─ USB-Serial-Kommandos lesen
  ├─ Verbindungsstatus / Wartelog
  ├─ EcuInitTester.update
  │    ├─ Init-Zustände
  │    ├─ CDC-Ring pollen
  │    ├─ KWP-Byte quittieren
  │    ├─ Block validieren, parsen und decodieren
  │    ├─ Console und Dashboard direkt aktualisieren
  │    └─ nächsten Request synchron mit Echo/ACK-Warten senden
  ├─ falls !isBusyReceiving: Console-/Dashboard-Rendern
  └─ delay(2)
```

### 2.6 Kommunikationsschichten

Physisch verläuft der Pfad als Digifant K-Line ↔ AutoDia K409/FT232R ↔ USB Bulk ↔ ESP-IDF USB Host ↔ EspUsbHost CDC ↔ `UsbCdcLink` ↔ `EcuInitTester`. Die unteren Schichten empfangen nebenläufig; die KWP-Schicht besitzt den Link jedoch nicht in einem eigenen zeitkritischen Kontext.

## 3. Kritischer Digifant-Empfangspfad

### 3.1 Vollständiger RX-Weg

1. Die ECU sendet ein KWP1281-Byte auf der K-Line.
2. Der FT232R packetiert es mit FTDI-Statusbytes in USB-Bulk-IN-Transfers.
3. EspUsbHost verarbeitet den Transfer im Bibliothekskontext, entfernt für FTDI die ersten zwei Statusbytes und ruft `EspUsbHostCdcSerial::pushData()` auf.
4. `pushData()` kopiert die Bytes unter `portENTER_CRITICAL` in den 512-Byte-Ring. Bei Vollstand wird `rxTail_` weitergesetzt; ein altes Byte geht ohne Signal an die Anwendung verloren.
5. Der Arduino-Loop erreicht `EcuInitTester::update()` und pollt `available()`/`read()`.
6. Beim Längenbyte bzw. bei allen Bytes vor dem Terminator schreibt der Tester unmittelbar das invertierte Byte über `_link.write()` zurück und erwartet ein lokales K-Line-Echo.
7. Empfangene Bytes werden in `_rxBlockBuf` kopiert. Nach `expectedLen + 1` Bytes werden Terminator und Blockzähler geprüft.
8. `parseBlock()` schreibt direkt in Dashboard-Zustand bzw. Headerpuffer und erzeugt Logs. Bei `0xF4` folgen weitere Logs und Decoder.
9. Nach `delay(30)` wird synchron der nächste ACK-/Messgruppenblock gesendet; `sendBlockWithHandshake()` wartet pro Byte bis zu 150 ms auf Echo und bis zu 350 ms auf ECU-ACK.

### 3.2 Zeitkritische Klassifikation

| Abschnitt | Klasse | Begründung |
|---|---|---|
| USB-Transferannahme und CDC-Ring-Write | kritisch | verhindert unmittelbaren Hardware-/Treiberverlust |
| Ring auslesen, Echo unterscheiden, Blockzustand fortschreiben | hart zeitkritisch / funktionskritisch | ist Voraussetzung für korrekte Bytefolge und ACK-Entscheidung |
| `~b` senden und TX-Echo/ECU-ACK handhaben | hart zeitkritisch / funktionskritisch | KWP1281-Fortsetzung hängt von zeitgerechter Antwort ab |
| Länge, Terminator, Counter prüfen | zeitkritisch | muss vor dem nächsten Protokollturn fertig sein, ist aber begrenzt |
| Payload kopieren / Frame publizieren | zeitkritisch | sollte kurz, deterministisch und ohne Warten sein |
| Messwertdecodierung | nicht zeitkritisch | darf ausfallen oder nachlaufen, ohne Link zu stoppen |
| Console, Serial, Dashboard, Touch, Scope | nicht zeitkritisch | dürfen keine Protokolldeadline beeinflussen |

### 3.3 Tatsächlicher Entkopplungsgrad

- **USB-Hardware → CDC-Ring:** tatsächlich nebenläufig und synchronisiert.
- **CDC-Ring → KWP-ACK:** nicht entkoppelt; der Loop muss rechtzeitig laufen.
- **KWP-ACK → Parser/Decoder:** nicht entkoppelt; identischer Methodenaufruf und Stack.
- **Decoder → UI-Modell:** nicht entkoppelt; direkte globale Setter.
- **Logging:** Textformatierung und Serial-Schreiben erfolgen im Produzentenpfad; nur Display-Redraw ist verzögert.

Damit schützt die vorhandene Pufferung nur vor begrenztem RX-Datenstau, nicht vor einer verpassten KWP-Antwortdeadline.

### 3.4 Positive Schutzmechanismen

- Akzeptierte Längen sind auf 3–64 begrenzt; `_rxBlockBuf[128]` hat Reserve.
- Endbyte `0x03` und Blocklänge werden geprüft.
- Counterabweichungen führen zum kontrollierten Sessionneustart.
- Das eigene Echo wird über Zustand statt nur Wertvergleich modelliert.
- Display-Rendern findet nicht direkt in `Console::println()` statt.
- Zeitvergleiche verwenden weitgehend overflow-robuste Differenzen.

Diese Maßnahmen verbessern Fehlererkennung und Normalfallstabilität, ersetzen aber keine Scheduling-Isolation.

## 4. Datenfluss und Producer/Consumer-Modell

### 4.1 Stufe A: ECU/FTDI → USB-Client-Task

- **Producer:** ECU plus FT232R/USB-Treiber.
- **Consumer:** EspUsbHost-Clientlogik.
- **Speicherhoheit:** USB-Transferpuffer liegt bei der Bibliothek; Nutzdaten werden noch im Callback kopiert.
- **Blocking:** Aus dem Workspace nicht quantifizierbar; Bibliothek verarbeitet Transfers in eigener Task.
- **Überlast:** USB-/Treiberverhalten vor `pushData()` ist extern und nicht instrumentiert.
- **Pointerrisiko:** Die Anwendung erhält keinen Transferpointer; dadurch hier kein Anwendungs-Lifetimeproblem.

### 4.2 Stufe B: USB-Client-Task → CDC-Ring

- **Producer:** `EspUsbHost::handleSerial()`.
- **Consumer:** `UsbCdcLink::available/read`, aufgerufen vom Loop.
- **Speicherhoheit:** Bibliothek alloziert den Ring beim `begin()`; Producer und Consumer teilen ihn unter Spinlock.
- **Producer-Blocking:** kurze Critical Section proportional zur empfangenen USB-Nutzdatenmenge.
- **Consumer-Blocking:** `available/read` selbst kurz; der Consumer wird aber durch alle anderen Loop-Arbeiten verzögert.
- **Volle Queue:** kein Blockieren/Backpressure; ältestes Byte wird verworfen.
- **Datenverlust:** nachgewiesener Mechanismus bei Vollstand; Eintritt im bisherigen Fahrzeugbetrieb nicht belegt.
- **Mehrfachverarbeitung:** Ring-Tail wird pro `read` genau einmal bewegt; im Bibliotheksring nicht erkennbar.

Bei 1200 Baud und 8N1 sind theoretisch rund 120 Nutzbytes/s möglich. 511 Bytes entsprechen grob 4,26 s kontinuierlicher Leitungslast. Diese Rechnung ist **keine Sicherheitsgarantie**: KWP1281 wartet zwischen Bytes auf ACK, und die relevante ACK-Deadline ist erheblich kleiner als die Ringfüllzeit. Zudem können lokale TX-Echos ebenfalls RX-Platz und Bearbeitung beanspruchen.

### 4.3 Stufe C: CDC-Ring → `_rxBlockBuf`

- **Producer und Consumer:** dieselbe `EcuInitTester::update()`-Ausführung; keine Taskgrenze.
- **Speicherhoheit:** `EcuInitTester` besitzt den festen Puffer.
- **Blocking:** Die `while (available > 0)`-Schleife hat im Live-Pfad kein Bytebudget. Bei fortlaufendem Nachschub kann ein Update lang laufen; das ist für RX gut, aber bindet zugleich Parser/Logging ein.
- **Volles Ziel:** durch Protokolllängenlimit derzeit ausgeschlossen; ungültige Startbytes werden verworfen.
- **Überschreiben:** Puffer wird erst nach Parse/Decode zurückgesetzt. Die an `parseBlock()` übergebenen Pointer bleiben während des synchronen Aufrufs gültig.
- **Echoannahme:** Nach jeder eigenen Quittung wird genau das nächste empfangene Byte verworfen. Das ist nur korrekt, wenn das lokale Echo garantiert vorhanden und vor dem nächsten ECU-Byte geordnet ist.

### 4.4 Stufe D: Block → Parser/Header/Decoder

- **Producer:** vollständiger Block im RX-Zustandsautomaten.
- **Consumer:** `parseBlock`, danach teilweise `decodeNumberedGroup`.
- **Speicherhoheit:** Payloadpointer zeigt in `_rxBlockBuf`; Header werden wertweise in `_groupHeader` kopiert.
- **Blocking:** unbeschränkte Anzahl synchroner `console.printf` relativ zur Payload; `String`-Konstruktion bei ECU-Ident; Dashboard-Setter.
- **Überlast:** keine Queue; jeder Block muss vollständig verarbeitet werden, bevor der Protokollablauf fortgesetzt wird.
- **Datenverlust/Mehrfachverarbeitung:** valide Blöcke werden einmal synchron verarbeitet. Bei Counterfehler Neustart; keine Duplikaterkennung jenseits der erwarteten Counterfolge.
- **Headerkonsistenz:** Ein Header je Gruppe bleibt bis zum nächsten Header derselben Gruppe erhalten. Es gibt keine Versions-/Sequenzmarke, aber im Single-Loop keine parallele Mutation.

### 4.5 Stufe E: Decoder → UI/Log/Scope

- **Producer:** `parseBlock`, `decodeNumberedGroup`, Verbindungszustandsautomat.
- **Consumer:** `Dashboard::draw*`, `pushScopeSample`, `Console`/Tab-2-Log.
- **Speicherhoheit:** skalare Messwerte werden kopiert; `String`-Felder besitzen ihren Speicher selbst; Console kopiert maximal 95 Zeichen pro Aufruf.
- **Blocking:** Setter sind meist kurz, können bei `String` aber allozieren; Logging formatiert auf dem Stack und schreibt synchron an `Serial`; Rendering kann laut Code-Kommentar 100–140 ms kosten.
- **Überlast:** Dashboard ist Latest-Value-Speicher, keine Messwertqueue. Zwischen zwei Draws werden Zwischenwerte absichtlich überschrieben. Console hält nur 40 Zeilen. Der Scope sampelt den jeweils letzten Zustand in 100-ms-Intervallen, nicht jeden ECU-Frame.
- **Semantik:** UI-Drops sind für die Anzeige akzeptabel, werden aber nicht explizit gezählt. Für eine lückenlose Messaufzeichnung ist dieses Modell ungeeignet.

## 5. FreeRTOS-Analyse

### 5.1 Tasks und Prioritäten

Die Anwendung erstellt keine eigene Task und setzt weder Core-Affinität noch Priorität. EspUsbHost verwendet in der eingebundenen Version standardmäßig Stack 8192, Priorität 5 und keine Affinität. Der Arduino-Loop-Kontext und interne ESP-IDF-Prioritäten sind aus dem Workspace/Buildkommando nicht sicher abzulesen. Daher kann nicht belegt werden, auf welchem Core Loop und USB-Client tatsächlich gleichzeitig laufen oder ob weitere Treibertasks sie zeitweise verdrängen.

Folge: USB-Transfers können zwar unabhängig vom Loop angenommen werden, die zeitkritische KWP-Antwort bleibt von der nicht explizit priorisierten Loop-Ausführung abhängig.

### 5.2 Queues, Notifications, Event Groups

- Keine Anwendungs-Queue.
- Keine Task Notification.
- Kein Event Group.
- Kein Message-/Stream-Buffer.
- Kein anwendungseigenes Semaphore/Mutex.
- Der einzige relevante Kanal ist der Bibliotheks-Ringpuffer; er transportiert Bytes, keine atomaren KWP-Frames.

Das Byte-Ring-Modell hat keine Framegrenzen und keine Reserveregel. Ein verlorenes Byte beschädigt nicht nur einen Messwert, sondern kann die Interpretation aller nachfolgenden Bytes bis zur Resynchronisation verschieben.

### 5.3 Blocking Calls

| Stelle | Worst Case aus Code | Einordnung |
|---|---:|---|
| `send5BaudAddress` | 10 × 200 ms = 2 s | in Init beabsichtigt; blockiert Loop/UI, aber Datenphase noch nicht aktiv |
| Baudumschaltung-Nachpause | 50 ms | kritisch nahe Sync-Phase; danach wird Puffer geleert |
| zweites KB2 | 35 ms | protokollbedingt, aber blockierend |
| `sendBlockWithHandshake` Echo | bis 150 ms pro TX-Byte | blockierend; liest und verwirft Bytes |
| `sendBlockWithHandshake` ECU-ACK | zusätzlich bis 350 ms je Nicht-Endbyte | blockierend; Funktion setzt trotz Timeout fort |
| Blocknachlauf | 30 ms pro vollständigem RX-Block | explizite Pause im zentralen Pfad |
| Loop-Pause | 2 ms | stetige Zusatzlatenz |
| Dashboard-Redraw | kein harter Code-Grenzwert; Kommentar nennt 100–140 ms | nicht präemptiv innerhalb derselben Loop-Task |
| `Serial.println` | kein projektseitiger Timeout/Bound | Laufzeit abhängig von Arduino-HW-CDC und Host |

Für einen vier Byte langen TX-Block ergibt sich allein aus den programmierten Timeoutgrenzen theoretisch bis zu `4*150 + 3*350 = 1650 ms`, zuzüglich Logging und Scheduling. Für einen fünf Byte langen Gruppenrequest bis zu 2150 ms. Das ist ein Fehler-/Timeoutpfad, kein erwarteter Normalfall, zeigt aber fehlende deterministische Obergrenzen.

### 5.4 Mutex, Priority Inversion, Deadlock, Starvation

- **Mutex:** Anwendung hat keinen. Der CDC-Ring verwendet einen Spinlock korrekt für Head/Tail/Bytes.
- **Datenrace:** `Console` wird potenziell von USB-Callbacks und Loop gleichzeitig benutzt; siehe Abschnitt 6.
- **Priority Inversion:** kein klassischer anwendungseigener Mutexfall nachweisbar. Die Architektur erzeugt stattdessen funktionale Inversion: niederkritisches UI/Logging läuft im selben Kontext vor dem nächsten hochkritischen ACK.
- **Deadlock:** Kein zyklischer Lockgraph im Anwendungscode. Ein klassischer Deadlock ist nicht nachgewiesen.
- **Starvation:** Der unlimitierte Live-RX-Loop kann bei ständigem Byteangebot UI/Touch verzögern; umgekehrt kann UI-Rendern den nächsten RX-Aufruf verzögern. Das ist kooperative Konkurrenz, keine saubere Priorisierung.
- **Scheduler-Yield:** `delay(1/2/30/35/50/200)` gibt anderen Tasks Zeit, blockiert aber den logischen KWP-Consumer im Loop. Yielding macht den Protokollpfad nicht unabhängig.

### 5.5 Stack und Heap

- EspUsbHost-Taskstack: Default 8192 laut eingebundener Bibliothek.
- Arduino-Loop-Stack: nicht aus Projektquellen belegt.
- `EcuInitTester` nutzt kleine lokale Arrays (`txBuf[32]`, `asciiStr[64]`) und `Console::printf` einen 96-Byte-Stackpuffer; kein offensichtlicher Stackoverflow aus diesen Einzelobjekten.
- Dashboard-Sprites werden beim Start im PSRAM alloziert. Rückgaben von `createSprite()` werden nicht geprüft; `_spritesCreated` wird dennoch true gesetzt.
- `String`-Operationen im UI-/Statuspfad können Heapallokation und langfristig Fragmentierung verursachen. Ein konkreter Allokationsfehler ist nicht belegt.
- EspUsbHost `sendSerial()` alloziert ohne aktivierte Async-Queue für jeden Write einen USB-Transfer dynamisch. KWP sendet byteweise; Allokationsfehler sind damit im zeitkritischen TX-Pfad möglich und werden vom Tester nicht wirksam behandelt.

## 6. Race Conditions

### 6.1 Nachgewiesene fehlende Synchronisation: USB-Callbacks ↔ Console

`UsbCdcLink::begin()` registriert Lambdas, die `console.printf()` aufrufen. EspUsbHost ruft Lifecycle-/Datenverarbeitung aus seinem eigenen Taskkontext auf. Gleichzeitig kann der Loop `console.printf`, `console.getLine`, `console.update` oder `Dashboard::drawInfo` ausführen.

Betroffene Felder sind `_lines`, `_head`, `_count`, `_dirty` und `_lastRedrawMs`. Schreibsequenzen bestehen aus mehreren nicht atomaren Operationen (`strncpy`, Nullterminierung, Head/Count-Update). Mögliche Auswirkungen:

- Leser sieht eine teilweise kopierte oder inkonsistent indizierte Zeile.
- gleichzeitige Producer überschreiben denselben Slot oder verlieren Updates.
- `_dirty`-Update geht verloren.
- Datenrace im C++-Speichermodell; Verhalten ist formal nicht definiert.

Die Callbacks sind selten, aber Connect/Disconnect ist gerade unter USB-Störungen relevant. Priorität **HIGH**, Evidenz: nachgewiesene ungeschützte Mehrkontextnutzung; konkrete beobachtete Korruption nicht belegt.

### 6.2 Dashboard

Im aktuellen Hardwarepfad schreiben USB-Callbacks nicht direkt ins Dashboard. Parser, Simulation und Renderer laufen im Loop, daher ist für skalare Dashboard-Felder derzeit keine echte parallele Mutation nachgewiesen. Sobald RX/Parser in eine eigene Task verschoben würde, wären praktisch alle Dashboard-Felder und `String`s races; die heutige direkte Setter-API ist deshalb architektonisch fragil und nicht ohne Snapshot-Übergabe migrierbar.

### 6.3 EspUsbHost-Zustand

`connected`, `write`, `setDtr/Rts/Baud` werden aus dem Loop auf bibliotheksinternen Gerätezustand angewandt, während USB-Tasks Connect/Disconnect bearbeiten können. Diese Methoden sind öffentliche Bibliotheks-APIs und sollen grundsätzlich taskübergreifend nutzbar sein; ohne vollständige Bibliotheks-/ESP-IDF-Verifikation wird daher kein Projektfehler behauptet. Der `#define private public`-Zugriff auf `submitVendorSerialControl` verlässt jedoch die garantierte API-Oberfläche und macht deren Thread-Sicherheitsannahme unsicher.

### 6.4 Zustandsautomat

Alle `EcuInitTester`-Felder werden aktuell nur aus dem Loop bearbeitet. Dort existiert kein Race. Es gibt aber logische Zustandskopplungen:

- `_blockCounter` wird nach TX inkrementiert und nach RX aus ECU-Counter abgeleitet.
- `_expectedRxCounter` nimmt stets einen Sprung um zwei an.
- `_measurementGroup`, `_awaitingGroupBody`, `_groupRequestNeedsAck` und `_awaitingGroupSwitchAck` bilden gemeinsam einen impliziten Unterzustandsautomaten.

Diese Felder sind nicht parallel unsicher, aber die zulässigen Kombinationen sind nicht typisiert. Fehler-/Sonderantworten können deshalb leicht einen schwer nachvollziehbaren Zustand erzeugen.

## 7. Ownership- und Lifetime-Probleme

### 7.1 Sichere aktuelle Lifetimes

- `EcuInitTester` hält eine Referenz auf das globale `UsbCdcLink`; beide leben für die Programmdauer.
- `ConnectivityTester` hält entsprechend eine Referenz auf ein globales `SerialLink`.
- Payloadpointer in `parseBlock` und `decodeNumberedGroup` werden nur synchron benutzt und nicht gespeichert.
- Headerdaten werden vor späterem Gebrauch kopiert.
- Replayframes sind statische Konstanten; Referenzen bleiben gültig.
- Console kopiert `const char*`-Inhalte in den eigenen Ring, statt fremde Pointer aufzubewahren.

Es ist im aktuellen Anwendungscode kein Use-after-free nachgewiesen.

### 7.2 Unklare oder fragile Ownership

1. **USB-Callbacks:** Die Lambda selbst captured nichts, greift aber auf globale `console` zu. Beim statischen Programmende ist das praktisch irrelevant; bei zukünftigem dynamischem Start/Stop wäre Callback-Abmeldung/Lifetime ungeklärt.
2. **CDC-Ring:** Ownership liegt in externer Bibliothek, aber die Anwendung kennt weder Füllstandshistorie noch Drops. Die Datenhoheit ist technisch klar, betrieblich aber unsichtbar.
3. **Messdaten:** Dashboard ist gleichzeitig Consumer, Latest-Value-Store und Scope-Datenquelle. Es gibt kein unveränderliches Messwertobjekt mit Sequenz, Zeitstempel, Gültigkeitsmaske und Herkunft.
4. **Console-Zeilen:** `getLine()` liefert einen Pointer in intern veränderlichen Ring. Im Single-Loop ist die unmittelbare Verwendung okay; bei parallelem Producer kann der Pointerinhalt während des Lesens wechseln.
5. **`String`-Temporäre:** Setter kopieren die Strings, daher kein Dangling Pointer. Das Problem ist Laufzeit/Allokation, nicht Lifetime.
6. **`txBuf[32]`:** `sendBlockWithHandshake` prüft `payloadLen` nicht gegen 28 Nutzbytes. Alle aktuellen privaten Aufrufstellen verwenden 0 oder 1 Byte, daher kein aktueller Overflow; die Funktion ist aber lokal nicht selbstsicher.

### 7.3 Datenhoheit bei Überlast

Es gibt keine explizite Aussage, welcher Datensatz bei Überlast geopfert werden darf. Tatsächlich gelten drei verschiedene implizite Policies:

- CDC-Ring: drop oldest byte, mit möglicher Framekorruption.
- Dashboard: latest value wins, ohne Drop-Zähler.
- Console: drop oldest line, beabsichtigter Zeilenring.

Nur die beiden letzten sind für ihre Verbraucher sinnvoll. Byteweiser Drop im Protokollstrom ist die ungünstigste Policy, weil er die Rahmung zerstört.

## 8. Echtzeit- und Blocking-Risiken

### 8.1 Kann RX unter maximaler Last sicher weiterlaufen?

**Nein, diese Garantie ergibt sich nicht aus dem Code.** Gründe:

- Der entscheidende Byte-ACK wird nicht im USB-Task und nicht in einer dedizierten High-Priority-Task erzeugt.
- Vor jedem `tester.update()` laufen `M5.update()` und serielle Kommandoverarbeitung.
- Nach einem Block laufen synchron Parser, Decoder, zahlreiche Logs, 30-ms-Pause und synchrones TX-Handshake.
- UI kann zwischen Blöcken einen langen Redraw beginnen.
- Serial-Ausgabe besitzt keine projektseitig belegte obere Laufzeitgrenze.
- TX nutzt dynamische USB-Transferallokation pro Byte und ignoriert Fehlschläge.
- RX-Overflow zerstört still alte Streambytes; keine Telemetrie löst eine definierte Recovery aus.

### 8.2 UI-Gate ist kein Scheduling-Schutz

`isBusyReceiving()` ist true während Initphasen oder wenn `_rxBlockPos > 0`. Im Zustand `RECEIVE_BLOCK` mit `_rxBlockPos == 0` ist es false. Dann ruft der Loop `dashboard.update()` auf; ein anstehender oder kurz danach eintreffender ECU-Block kann erst nach Ende des nicht-präemptiven Draws bearbeitet werden. Der Test erfolgt nur einmal vor dem Rendern. Es gibt weder Reservierungsfenster noch eine erneute Ringprüfung noch eine garantierte ECU-Ruhephase.

Dies ist ein **nachgewiesener Designfehler**. Ob er im Fahrzeug einen Timeout auslöst, hängt von realer Drawzeit und ECU-Timing ab und ist daher als Laufzeitausfall nur wahrscheinlich, nicht nachgewiesen.

### 8.3 Logging

Im RX-Pfad entstehen mindestens Logs für Blocklänge, jedes quittierte Byte, Endbyte, Blockinhalt und decodierte Werte. `Console::printf` erzeugt nicht nur Ringeinträge, sondern ruft bei jedem Aufruf `Serial.println` auf. Zudem behandelt jeder `printf`-Aufruf einen eigenen Console-Ringslot, auch wenn der Text semantisch nur ein Fragment einer Zeile ist. Lange Decoder-Ausgaben erzeugen damit viele Serial-Transfers und verdrängen nützlichere Logzeilen aus dem Ring.

Die Aussage in `Console.h`, Logging selbst blockiere nie mehr das teure Redraw, ist nur für das Display korrekt. Sie macht keine Aussage über Serial-I/O.

### 8.4 Burst- und Backpressure-Verhalten

- USB kann mehrere CDC-Bytes in einem Transfer anliefern; `pushData` kopiert sie als Burst.
- Der Loop leert im Live-Pfad ohne Leselimit, führt aber zwischen Bytes synchrone ACK-Writes und Logs aus.
- Bei langsamem Loop staut der CDC-Ring, nicht die ECU-Protokollsteuerung. Da die ECU ACKs erwartet, wird die Sitzung vermutlich bereits vor einem 511-Byte-Overflow abbrechen; das genaue Verhalten ist nicht im Workspace belegt.
- Es gibt keine High-Water-Mark-Reaktion, z. B. Logs/UI deaktivieren.
- Es gibt keine getrennten Policies für Rohframes (möglichst lückenlos), UI (latest wins) und Logs (best effort).

### 8.5 Speicherallokationen und Kopien

- Feste Framepuffer und wertweise Payloadkopien sind deterministisch und sinnvoll.
- Eine Kopie eines kompletten Frames in eine Queue wäre bei max. 65 Byte günstiger als geteilte Pointer und sollte nicht als problematische „unnötige Kopie“ betrachtet werden.
- Kritischer sind dynamische `String`-Operationen und EspUsbHost-One-shot-TX-Allokationen im Protokollpfad.
- Spriteallokation geschieht nur beim Start, aber ohne Fehlerprüfung. Rendering selbst arbeitet auf großen Puffern und verursacht lange Bus-/CPU-Arbeit.

### 8.6 Gemessene versus angenommene Zeiten

Im Code existiert ein `[PERF]`-Log für Draws über 20 ms, aber keine persistente Statistik zu Maximum, Perzentilen, RX-Ring-Füllstand, ACK-Latenz oder Scheduling-Jitter. Die Projektdokumentation nennt erfolgreiche 180-s-Läufe. Diese Daten sind wertvoll, reichen jedoch nicht als Worst-Case-Nachweis, weil weder gleichzeitige UI-Aktivität/Tabwechsel noch blockierter Logging-Host, USB-Störungen, PSRAM-Druck oder Dauerlauf mit Telemetrie dokumentiert sind.

## 9. Architekturprobleme

### 9.1 Vermischte Verantwortlichkeiten

`EcuInitTester` ist gleichzeitig:

- physischer Init-Controller,
- Transportsteuerung,
- KWP-Byte-Handshake,
- Frameassembler,
- Session-/Counterautomat,
- Messgruppen-Scheduler,
- Parser und Decoder,
- Log-Produzent,
- direkter UI-Produzent.

Damit kann keine dieser Verantwortlichkeiten separat priorisiert oder überlastet werden. Der Klassenname „Tester“ verdeckt, dass sie die produktive Live-Protokollengine ist.

### 9.2 Fehlende Schicht zwischen Protokoll und Verbraucher

Es fehlt ein Frame-/Measurement-Bus. Parser und Decoder rufen globale UI-Objekte direkt auf. Dadurch ist die Datenverarbeitung nicht austauschbar, Logging nicht best effort isolierbar und eine spätere Datei-Aufzeichnung nicht mit eigener Verlustpolicy ergänzbar.

### 9.3 Globale Seiteneffekte

`console` und `dashboard` werden aus mehreren Komponenten direkt benutzt. Eine Funktion wie `parseBlock()` verändert dadurch neben dem Protokollzustand auch UI, Statistik und Logs. Laufzeit und Nebenwirkungen sind aus der Signatur nicht erkennbar.

### 9.4 Hardware-Kapselung gebrochen

`UsbCdcLink.h:6–8` definiert vor dem Bibliotheksinclude `private` als `public`, um `submitVendorSerialControl` aufzurufen. Das kann alle privaten Deklarationen im inkludierten Header umschreiben, koppelt an Implementierungsdetails und umgeht API-/Thread-Sicherheitsgarantien. Bibliotheksupdates können den Build oder das Laufzeitverhalten brechen.

### 9.5 Keine explizite Fehler- und Backpressure-Strategie

- RX-Bytes: stiller Drop durch Bibliothek.
- TX: Rückgabewert ignoriert; Funktion meldet Erfolg.
- Parser/Consumer: keine Queue, daher keine messbare Consumer-Überlast.
- UI: Zwischenwerte gehen implizit verloren.
- Logging: Ringüberschreibung und mögliches Serial-Blocking.
- Session: viele Fehler enden in generischem Neustart, ohne strukturierte Ursache/Zähler.

### 9.6 Simulation deckt Concurrency nicht ab

`SimulatedLink` läuft vollständig im Loop. Es liefert ganze gespeicherte Frames nach 25 ms und aktualisiert das Dashboard teilweise selbst. Es simuliert nicht:

- USB-Task/Loop-Interleavings,
- FTDI-Statusbytes und lokale Echo-Reihenfolge,
- Byteweise ECU-ACK-Deadlines,
- Ringoverflow,
- Disconnect während eines Blocks,
- TX-Allokations-/Submitfehler,
- langsame Consumer.

Replay eignet sich für Decoder- und UI-Plausibilität, nicht als Nachweis der Echtzeitarchitektur.

### 9.7 Separater USB-Check

`M5Tab5_USB_Check.ino` ist ein früher Test-Sketch, nicht Teil des aktiven Loggers. Er loggt jedes Byte synchron, enthält `delay(1000)` bei Host-ready und generell `delay(50)`. Seine Concurrency-Eigenschaften dürfen nicht als Architektur des Hauptprogramms interpretiert werden; umgekehrt validiert er die robuste KWP-Datenphase nicht.

## 10. Priorisierte Befunde

### CRITICAL

#### C-1: Zeitkritischer KWP-RX/ACK-Pfad ist nicht von Parser, Logging und UI entkoppelt

- **Evidenzklasse:** nachgewiesenes Problem / nachgewiesene Verletzung der vorgegebenen Architektur
- **Dateien:** `M5Tab5_Digifant_Proto/M5Tab5_Digifant_Proto.ino:157–200`; `M5Tab5_Digifant_Proto/EcuInitTester.cpp:452–601`; `M5Tab5_Digifant_Proto/EcuInitTester.cpp:194–319`
- **Funktion/Klasse:** `loop`, `EcuInitTester::update`, `parseBlock`, `decodeNumberedGroup`
- **Problem:** Protokollabruf, Byte-ACK, Parsing, Decoder, Logging, UI-Setter und Requestfolge laufen synchron im selben Loop-Kontext.
- **Technische Begründung:** Es gibt keine Task-/Queue-Grenze nach dem Frameabschluss. Langsame nachgelagerte Arbeit verlängert direkt die Zeit bis zur nächsten Ringentleerung bzw. zum nächsten ACK.
- **Auswirkung:** KWP-Timeout, Sessionneustart, Verlust von Messdaten; die priorisierte Kernanforderung ist strukturell nicht garantiert.
- **Datenfluss:** CDC-Ring → KWP-ACK → Parser/Decoder → UI/Log.

#### C-2: UI-Sperre lässt lange Redraws im verbundenen Zwischenblockfenster zu

- **Evidenzklasse:** nachgewiesenes Designproblem; konkreter Fahrzeugausfall wahrscheinlich, aber nicht nachgewiesen
- **Dateien:** `M5Tab5_Digifant_Proto/EcuInitTester.h:27–33`; `M5Tab5_Digifant_Proto/M5Tab5_Digifant_Proto.ino:185–200`; `M5Tab5_Digifant_Proto/Dashboard.cpp:153–168, 227–251`
- **Funktion/Klasse:** `isBusyReceiving`, `loop`, `Dashboard::update/draw`
- **Problem:** `_rxBlockPos == 0` bedeutet „noch kein Block begonnen“, nicht „garantiehaft ausreichend Busruhe“. Dennoch wird dann ein kompletter nicht-präemptiver Draw erlaubt.
- **Technische Begründung:** Ein ECU-Byte kann direkt nach dem Gate eintreffen. Das Display kann laut Projektkommentar 100–140 ms belegen; der Protokollcode kann bis zum Draw-Ende kein `~b` senden.
- **Auswirkung:** verpasste Byte-ACK-Deadline trotz im CDC-Ring erfolgreich empfangenem Byte.
- **Datenfluss:** ECU → CDC-Ring (gepuffert) → verspätetes Loop-ACK.

### HIGH

#### H-1: Synchrones, sehr hochfrequentes Serial-Logging im Empfangspfad

- **Evidenzklasse:** nachgewiesenes Problem; konkrete Blockierdauer nicht gemessen
- **Dateien:** `Console.cpp:35–40, 82–98`; `EcuInitTester.cpp:491, 518, 521–523, 549–598`
- **Funktion/Klasse:** `Console::println/printf`, `EcuInitTester::update`
- **Problem:** Jedes Log schreibt sofort an `Serial`, darunter ein Log pro quittiertem RX-Byte.
- **Technische Begründung:** Der serielle Sink liegt im zeitkritischen Aufrufstack und besitzt keine projektseitig nachgewiesene Laufzeitgrenze oder Drop-Policy.
- **Auswirkung:** ACK-Jitter/Timeouts, reduzierter RX-Durchsatz, Log-Ring verdrängt schnell relevante Meldungen.
- **Datenfluss:** KWP RX → Formatierung → HW-CDC Serial, vor Abschluss des Protokollturns.

#### H-2: Ungeschützte Mehrtask-Nutzung der globalen Console

- **Evidenzklasse:** nachgewiesenes Problem
- **Dateien:** `UsbCdcLink.cpp:7–16`; `Console.cpp:35–63, 73–80`; `Dashboard.cpp:940–955`
- **Funktion/Klasse:** USB Connect/Disconnect-Callbacks, `Console`, `Dashboard::drawInfo`
- **Problem:** USB-Task und Loop greifen ohne Mutex/Queue auf denselben Ring und seine Indizes zu.
- **Technische Begründung:** `strncpy`, Head- und Count-Updates sind keine atomare Transaktion; `getLine` gibt internen Speicher frei lesbar zurück.
- **Auswirkung:** verlorene/korrupt angezeigte Logs, undefiniertes Verhalten; gerade bei USB-Störung zusätzliche Instabilität.
- **Datenfluss:** USB-Lifecycle-Callback → Console ↔ Loop/UI.

#### H-3: TX-Fehler werden ignoriert und als Erfolg gemeldet

- **Evidenzklasse:** nachgewiesenes Problem
- **Dateien:** `EcuInitTester.cpp:111–170, 431–448, 475, 488, 515`; `UsbCdcLink.cpp:37–39`
- **Funktion/Klasse:** `sendBlockWithHandshake`, RX-ACK-Sendestellen
- **Problem:** `write`-Ergebnisse werden weitgehend nicht geprüft; `sendBlockWithHandshake` gibt bedingungslos `true` zurück und erhöht den Counter auch nach fehlenden ACKs.
- **Technische Begründung:** EspUsbHost kann bei fehlendem Endpoint, Transferallokations- oder Submitfehler `false` liefern. Der Protokollautomat unterscheidet das nicht.
- **Auswirkung:** lokaler und ECU-Zustand laufen auseinander; Folgebytes werden konsumiert/verworfen; Recovery wird verzögert und Diagnoseursache verschleiert.
- **Datenfluss:** KWP TX/Byte-ACK → USB-Out.

#### H-4: CDC-Overflow verwirft still alte Streambytes; keine Telemetrie

- **Evidenzklasse:** nachgewiesener Mechanismus in der eingebundenen Bibliothek; Eintritt im Fahrzeug nicht belegt
- **Dateien:** extern `EspUsbHost.h` Default `ESP_USB_HOST_CDC_RX_BUFFER_SIZE=512`; extern `EspUsbHost.cpp`, `EspUsbHostCdcSerial::pushData`; Anwendung `UsbCdcLink.cpp:7–28`
- **Funktion/Klasse:** `EspUsbHostCdcSerial`, `UsbCdcLink::begin`
- **Problem:** effektive 511-Byte-Kapazität, drop-oldest ohne Zähler; Anwendung nutzt weder `setRxBufferSize` noch Füllstand/Overflow-Metrik.
- **Technische Begründung:** Byteverlust in einem ungeframten Protokollstrom zerstört Blockausrichtung und Counterfolge.
- **Auswirkung:** Blockverlust, falsche Rahmung, Sessionneustart; Ursache betrieblich nicht unterscheidbar.
- **Datenfluss:** USB-Client-Task → CDC-Ring → Loop.

#### H-5: Echo-Logik setzt garantiertes, streng geordnetes lokales Echo voraus

- **Evidenzklasse:** potenzielles Risiko / architektonisch fragil
- **Datei:** `EcuInitTester.cpp:496–505`; Sendepfad `132–159`
- **Funktion/Klasse:** `EcuInitTester::update`, `sendBlockWithHandshake`
- **Problem:** Nach RX-ACK wird immer exakt das nächste Ringbyte verworfen; beim Senden werden bis zum erwarteten Wert andere Bytes konsumiert und verworfen.
- **Technische Begründung:** Fehlt das Echo, kommt es verspätet oder wird USB-seitig mit ECU-Daten anders geordnet, wird ein echtes ECU-Byte entfernt. Aktuelle Fahrzeugcaptures sprechen dafür, dass das Echo beim vorhandenen Adapter normal funktioniert, beweisen aber keine Garantie bei Störung/Disconnect.
- **Auswirkung:** Frameverschiebung, falsches Endbyte, Counterfehler, Sessionabbruch.
- **Datenfluss:** lokales K-Line-Echo und ECU-RX teilen denselben CDC-Bytestrom.

### MEDIUM

#### M-1: Blockierende TX-Handshake-Funktion hat sehr große Fehlerpfad-Laufzeit

- **Evidenzklasse:** nachgewiesenes Problem
- **Datei:** `EcuInitTester.cpp:111–170`
- **Funktion:** `sendBlockWithHandshake`
- **Problem:** Busy-poll/Yield bis 150 ms Echo plus 350 ms ACK je Byte, danach Fortsetzung statt Abbruch.
- **Technische Begründung:** Ein 4-Byte-Block kann programmiert bis etwa 1,65 s binden; ein 5-Byte-Block bis etwa 2,15 s.
- **Auswirkung:** UI-Starvation, verzögerte Disconnect-Reaktion, unklare Sessionlage. Im turn-basierten Protokoll ist paralleler ECU-Blockempfang dabei normalerweise nicht erwartet, daher unterhalb HIGH.

#### M-2: Parser, Decoder, Messgruppensteuerung und UI sind strukturell gekoppelt

- **Evidenzklasse:** nachgewiesenes Architekturproblem
- **Dateien:** `EcuInitTester.cpp:172–319, 549–596`; `Dashboard.h`
- **Problem:** Direkte globale Setter/Logs und implizite Unterzustandsflags; kein Frame-/Measurement-Interface.
- **Auswirkung:** erschwerte Tests, keine unabhängige Priorisierung, riskante spätere Tasktrennung, unklare Messwert-Drop-Policy.

#### M-3: Dynamische Allokationen im bzw. nahe dem Protokollpfad

- **Evidenzklasse:** wahrscheinliches Langzeit-/Latenzrisiko
- **Dateien:** `EcuInitTester.cpp:270`; `Dashboard.cpp:204–224`; extern EspUsbHost `sendSerial`
- **Problem:** Arduino-`String` und USB-One-shot-Transferallokation; keine Allokationsfehler-/Latenztelemetrie.
- **Auswirkung:** Jitter, Heapfragmentierung oder TX-Ausfall im Dauerbetrieb; konkreter Ausfall nicht belegt.

#### M-4: Simulation validiert weder Byte-Timing noch Nebenläufigkeit

- **Evidenzklasse:** nachgewiesene Testlücke
- **Dateien:** `SimulatedLink.cpp:30–76`; `M5Tab5_Digifant_Proto.ino:171–175`
- **Problem:** Simulation und Consumer laufen im selben Loop; neue Writes verwerfen alte simulierte Antworten; kein echtes Echo-/ACK-/Overflow-Modell.
- **Auswirkung:** Replay-Erfolg kann Concurrency- und Echtzeitfehler nicht aufdecken.

#### M-5: `createSprite`-Ergebnisse werden nicht geprüft

- **Evidenzklasse:** potenzielles Risiko
- **Datei:** `Dashboard.cpp:71–110`
- **Funktion:** `Dashboard::begin`
- **Problem:** Mehrere große PSRAM-Sprites werden angelegt, `_spritesCreated` danach ungeprüft gesetzt.
- **Auswirkung:** bei Speicherknappheit undefiniertes/fehlerhaftes Rendering oder Bibliotheksfehler; kein direkter RX-Fehler belegt, aber mögliche lange Fehlerpfade.

#### M-6: Kein formales Deadline-, Füllstands- oder Drop-Monitoring

- **Evidenzklasse:** nachgewiesene Observability-Lücke
- **Dateien:** gesamter Live-Pfad
- **Problem:** keine Messung von ACK-Latenz, maximaler Loop-Lücke, Ring-High-Water-Mark, RX-Drops, TX-Fehlern oder Queue-Overruns.
- **Auswirkung:** Normalfalltests können Sicherheitsmargen und seltene Aussetzer nicht quantifizieren.

### LOW

#### L-1: Bibliothekskapselung wird per Präprozessor aufgehoben

- **Evidenzklasse:** nachgewiesenes Architekturproblem
- **Datei:** `UsbCdcLink.h:3–8`; Nutzung `UsbCdcLink.cpp:49–59`
- **Problem:** `#define private public` für nichtöffentliche Vendor-Control-API.
- **Auswirkung:** Updatefragilität, schwer belegbare Thread-Sicherheit, potenzielle Seiteneffekte auf alle Deklarationen im Header. Im aktuellen Build offenbar kompilierbar.

#### L-2: `sendBlockWithHandshake` besitzt keine lokale Payloadgrenzenprüfung

- **Evidenzklasse:** potenzielles Risiko
- **Datei:** `EcuInitTester.cpp:111–123`
- **Problem:** `txBuf[32]`, aber beliebiges `payloadLen` in der privaten Signatur.
- **Auswirkung:** künftige Aufrufstelle mit mehr als 28 Nutzbytes könnte den Stack überschreiben. Aktuelle Aufrufe verwenden höchstens ein Byte; daher heute kein erreichbarer Fehler.

#### L-3: Kommentare und Bezeichner sind teilweise veraltet

- **Evidenzklasse:** nachgewiesenes Dokumentationsproblem
- **Dateien:** `EcuInitTester.h:8–20`, Konstantante `kBaud9600`; `SimulatedLink.h:40`
- **Problem:** Header beschreibt noch 5-Baud-via-Baudrate/9600-Fallback, aktuelle Implementierung bitbangt und nutzt verifiziert 1200 Baud; 25 ms werden als „10 Hz“ kommentiert.
- **Auswirkung:** Fehlinterpretation bei Wartung, keine unmittelbare Laufzeitwirkung.

#### L-4: Dashboard-/Console-Globalzustand erschwert Testbarkeit

- **Evidenzklasse:** Verbesserungsvorschlag / strukturelle Schwäche
- **Dateien:** `Console.cpp:7`, `Dashboard.cpp:6`, direkte Nutzungen in Parser/Links
- **Auswirkung:** versteckte Seiteneffekte und schwierige Unit-Tests; im aktuellen statischen Lebenszyklus kein Lifetimefehler.

## 11. Empfohlene Zielarchitektur

### 11.1 Prinzipien

1. **Single Owner für den Link:** Genau eine dedizierte KWP-I/O-Task besitzt `UsbCdcLink`, Baud-/Break-Steuerung, RX-Ringzugriff, Echoerkennung, TX und Blockcounter.
2. **Hohe Priorität, kurze gebundene Arbeit:** Diese Task macht ausschließlich das, was für Protokollfortschritt notwendig ist. Kein `Serial`, kein Display, kein `String`, keine allgemeine Heapallokation, keine Messwertformel.
3. **Framekopie statt Pointer-Sharing:** Nach vollständiger Validierung wird ein kleines wertbasiertes Objekt in eine vorallozierte SPSC-/FreeRTOS-Queue kopiert, z. B. Sequenz, Timestamp, Länge, Counter, Titel und bis zu 61 Payloadbytes.
4. **Verlustpolicy je Datenklasse:** Rohframes für Logging möglichst lossless bis zu einer dimensionierten Grenze; UI explizit latest-value-wins; Debuglogs best effort/drop-first.
5. **Keine Backpressure in Richtung KWP-I/O:** Eine volle Consumer-Queue darf die I/O-Task nicht blockieren. Sie muss einen Dropzähler erhöhen, gegebenenfalls einen definierten Frame verwerfen und den ECU-Handshake fortsetzen.
6. **Snapshots für UI:** Decoder publiziert atomare/queuebasierte immutable Messwertsnapshots. Die UI liest eine Kopie und darf beliebig lange rendern.

### 11.2 Ziel-Datenfluss

```text
ESP-IDF USB / EspUsbHost
        |
        v
Bibliotheks-CDC-Ring (kurzer Hardware-Jitterpuffer)
        |
        v
KWP I/O Task, hohe Priorität, exklusiver Linkbesitz
  - drain RX
  - Echo / ~b / Counter / Timeout
  - Framegrenzen und minimale Validierung
  - keinerlei UI/Serial/Decoder
        |
        +-- nonblocking copy --> Frame Queue --> Parser/Decoder Task
                                      |
                                      +--> Latest Measurement Snapshot --> UI Task
                                      |
                                      +--> Logging Queue --> Storage/Serial Task
                                      |
                                      +--> Health counters / diagnostics
```

### 11.3 I/O-Task

- Priorität oberhalb UI, Logging und Decoder; konkrete Zahl anhand der tatsächlichen ESP-IDF-/USB-Taskprioritäten festlegen.
- Core-Affinität nur nach Messung wählen. Entweder nahe am USB-Client für Cache/Latenz oder getrennt zur Vermeidung langer gegenseitiger Verdrängung; der ESP32-P4-Aufbau muss verifiziert werden.
- Alle Wartephasen als Zustandsautomat mit Deadlines statt sekundenlangen synchronen Funktionsschleifen.
- TX-Rückgaben zwingend auswerten; Counter nur nach definiert erfolgreichem Turn fortschreiben.
- Vorallozierte TX-Ressourcen/Async-Queue der Bibliothek prüfen, damit kein `malloc` pro ACK-Byte nötig ist.
- Empfangsringsize nicht blind erhöhen: zuerst ACK-Deadline isolieren, dann anhand Burst- und Schedulingmessungen dimensionieren.

### 11.4 Framequeue und Dimensionierung

Die Queue muss anhand gemessener Producer-Rate und maximal zulässiger Consumerpause dimensioniert werden:

`Kapazität >= Peak-Frames/s × maximale Consumer-Stallzeit + Burstreserve`.

Ein Queueelement sollte den Frame **vollständig besitzen**. Pointer in `_rxBlockBuf` sind ungeeignet, weil der I/O-Producer diesen sofort wiederverwendet. Bei maximal rund 65 Bytes pro Frame ist die deterministische Kopie klein und beseitigt Lifetime-/Ownership-Probleme.

Bei voller Queue darf die I/O-Task nicht warten. Sinnvolle Policy:

- Protokollsteuerungs-/Fehlerereignisse separat reservieren oder priorisieren.
- Messframes je nach Aufzeichnungsziel entweder drop-newest mit Lückenzähler oder in einem größeren vorallokierten Ring halten.
- UI niemals direkt aus dieser Queue blockieren lassen; UI bekommt nur letzten dekodierten Snapshot.
- Jeder Drop enthält Zähler und Sequenzlücke, damit Datenqualität sichtbar bleibt.

### 11.5 Parser-/Decoder-Task

- Besitzt die Framequeue als Consumer.
- Validiert anwendungsnahe Inhalte und erzeugt typisierte Messwertsnapshots mit Timestamp, Gruppe, Sequenz und Gültigkeitsflags.
- Darf keine KWP-Antwort auslösen; Requests/Steuerkommandos gehen als kleine Control-Messages zur I/O-Task, die sie nur an sicheren Protokollturns ausführt.
- Keine direkten globalen UI-Aufrufe.

### 11.6 Logging und UI

- Logging erhält strukturierte Events über eigene bounded Queue. Bei Vollstand Debug zuerst verwerfen; nie die I/O-Task blockieren.
- Formatierung (`printf`, Hexdump) erst im Logging-Consumer.
- USB-Lifecycle-Callbacks kopieren nur kleine Events in eine threadsichere Queue; sie rufen keine globale Console auf.
- UI arbeitet mit Latest Snapshot/Double Buffer. Ein Draw darf 100 ms oder länger dauern, ohne RX zu beeinflussen.
- Scope bekommt Zeitstempel der Messung, nicht den zufälligen 100-ms-UI-Samplingzeitpunkt. Anzeige darf downsamplen; Aufzeichnung behält die echte Sequenz.

### 11.7 Health- und Echtzeitnachweis

Mindestens folgende Metriken sind für einen belastbaren Nachweis nötig:

- maximale und Histogramm-/Perzentil-ACK-Latenz,
- maximale Lücke zwischen RX-Service-Aufrufen,
- CDC-Ring-Füllstand/High-Water und Overflow-Drops,
- Framequeue-Füllstand und Drops,
- TX-Submit-/Allokationsfehler,
- ungültige Länge, Endbyte, Counter, Echo-Timeout, Sessionrestart je Ursache,
- Task-Stack-High-Water-Marks und Heap-Minimum,
- maximale Parser-, Logger- und Drawzeit.

Erst Stressläufe mit aktivem Tabwechsel, maximalem Logging, blockiertem/abgezogenem Serial-Host, USB-Reconnects und langen Fahrzyklen können die Architekturannahmen quantitativ bestätigen.

## 12. Offene Fragen und Unsicherheiten

1. **Exakte ECU-Deadlines:** Der Workspace nennt ungefähr 20–35 ms bzw. 25–40 ms, enthält aber keine normative Digifant-Spezifikation oder Oszilloskopmessung für jedes Byte-ACK.
2. **Reale Draw-Latenzverteilung:** Es gibt Performance-Logs, aber keinen im Workspace auswertbaren Maximal-/Perzentildatensatz unter allen drei Tabs und gleichzeitiger Live-Kommunikation.
3. **Serial-Blocking:** Nicht belegt ist, ob und wann ESP32-P4 HW-CDC `Serial.println` bei nicht lesendem Host blockiert oder nur Daten verwirft/puffert.
4. **Arduino-Loop-Priorität/Core:** Nicht explizit im Projekt festgelegt; die genaue Core-Version und deren Main-Task-Konfiguration müssten gegen den installierten Arduino-ESP32-Core verifiziert werden.
5. **USB-Client-Taskdetails:** EspUsbHost 2.7.8 wurde lokal eingesehen, aber eine vollständige formale Thread-Sicherheitsanalyse aller Host-/ESP-IDF-Interna war nicht Scope der Anwendungscodeanalyse.
6. **RX-Overflow-Telemetrie:** Die verwendete CDC-Klasse exponiert im betrachteten Pfad keinen Dropzähler. Daher ist unbekannt, ob in historischen Läufen Drops vorkamen.
7. **Lokales Echo:** Captures bestätigen das normale Echoverhalten des konkreten K409, aber nicht dessen Garantie bei USB-Paketgrenzen, Störungen oder Reconnect.
8. **Disconnect während TX/RX:** Es fehlen gezielte Tests für Abziehen/Reset innerhalb eines Byte-Handshakes.
9. **Worst-Case-Producer-Rate:** Die reale Blockfolge wird durch KWP-Turn-Taking begrenzt; maximaler Burst inklusive Echo und USB-Paketierung wurde nicht gemessen.
10. **Aufzeichnungsanforderung:** Unklar ist, ob zukünftiges CSV-Logging jeden ECU-Frame lückenlos sichern muss oder ob explizit markiertes Downsampling zulässig ist. Davon hängt die Queue-/Storage-Dimensionierung ab.
11. **PSRAM-Fehlerverhalten:** Freier PSRAM, Sprite-Allokationserfolg und Verhalten der Grafikbibliothek bei `createSprite`-Fehler sind im Workspace nicht protokolliert.
12. **Langzeitstabilität:** Die dokumentierten 180-s- und 120-s-Läufe belegen Funktion, nicht Heap-/USB-/Counterverhalten über Stunden oder Tage.
13. **Prioritätsziel:** „Unter allen Umständen“ ist auf einem nicht hart-echtzeitqualifizierten USB-/FreeRTOS-System wörtlich nicht beweisbar. Operational sollte es als klar definierte maximale ACK-Latenz plus spezifizierte Last-/Fehlerbedingungen formuliert werden.

## Schlussfolgerung

Der aktuelle Stand besitzt einen brauchbaren Prototypenpfad und mehrere sinnvolle lokale Schutzmaßnahmen, aber die entscheidende Grenze liegt an der falschen Stelle: Der USB-Ring entkoppelt nur die physische Transferannahme. Der KWP1281-Protokollfortschritt einschließlich Byte-ACK bleibt an denselben Loop gekoppelt, der Decoder, Logging und Display bedient. Deshalb kann ein langsamer nachgelagerter Verbraucher weiterhin den funktionskritischen Empfang indirekt stoppen. Die robuste Zielrichtung ist ein exklusiver, hochpriorisierter KWP-I/O-Owner mit nichtblockierender, kopierender Frame-Übergabe und getrennten Verluststrategien für Messdaten, UI und Logs.
