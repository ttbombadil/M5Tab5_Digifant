# M5Tab5 Architecture Analysis – GLM 5.2

**Projekt:** KWP1281-Datenlogger für VW 2E (Digifant 1.7) auf M5Stack Tab5 (ESP32-P4)
**Analyse-Stand:** Workspace `M5Tab5_AutoDia`, Branch `main`
**Analysierte Quellen:** `M5Tab5_Digifant_Proto/` (alle .ino/.h/.cpp), `project.md`, `CODE_REVIEW_KWP1281.md`, EspUsbHost-Bibliothek (`~/Documents/Arduino/libraries/EspUsbHost/src/`), Build-Artefakte (`build*/compile_commands.json`)

> **Wichtige Vorbemerkung zur Begriffsklärung:** Im Projekt wird die K-Line-Kommunikation mit dem Digifant-Steuergerät über den USB-Adapter (AutoDia K409 / FTDI FT232R) abgewickelt. Der "Digifant-Datenempfang" ist im Code als **KWP1281-Blockempfang über `UsbCdcLink` → `EcuInitTester::update()`** implementiert. Es gibt **keinen separaten Digifant-Hardware-Eingang** (kein UART-Pin, kein ISR, kein eigener Task) – die gesamte Empfangslogik läuft kooperativ im Arduino-`loop()`-Task. Diese Tatsache ist der zentrale Architekturbefund dieser Analyse.

---

## 1. Executive Summary

Die Anwendung ist ein **Single-Task-Arduino-Sketch** ohne eigene FreeRTOS-Strukturen (kein `xTaskCreate`, keine Queues, keine Notification, kein eigener Ringbuffer im Applikationscode). Die einzige echte Nebenläufigkeit stammt aus der **EspUsbHost-Bibliothek**, die einen eigenen FreeRTOS-Task ("EspUsbHost", Priorität 5, Stack 8192) betreibt, der USB-Transfers abschließt und empfangene CDC-Bytes in einen **512-Byte-RX-Ringbuffer** (`EspUsbHostCdcSerial::pushData`, spinlock-gesichert, Overflow verwirft älteste Bytes) schreibt.

Daraus ergibt sich das Kernproblem: **Empfang (USB-Task) und Verarbeitung (loop-Task) sind nur durch den RX-Ring gepuffert entkoppelt – die gesamte Protokollverarbeitung, das byte-weise ACK-Senden, das Logging und die UI-Aktualisierung laufen sequenziell im selben `loop()`-Kontext.** Konkret:

1. **Nachgewiesen:** Der Empfangspfad enthält mehrfache blockierende `delay()`-Aufrufe (200 ms pro 5-Baud-Bit, 30–35 ms Pausen, bis zu 350 ms ACK-Polling-Timeouts in `sendBlockWithHandshake`). Während dieser Zeit wird der RX-Ring nicht geleert. Bei 1200 Baud (~120 Byte/s) füllt ein 512-Byte-Ring in ~4,3 s – die blockierenden Phasen sind einzeln kürzer, aber die **Kumulation aus `delay(30)` nach jedem Block + `sendBlockWithHandshake`-Polling + `Console::printf`-Serial-Ausgaben** kann bei langsamer USB-CDC-Konsole des Hosts den Ring überlaufen lassen. Der Ring **verwirft dann die ältesten Bytes** (nachgewiesen in `EspUsbHost.cpp:14423–14441`), was zu Blockverlusten und Counter-Resyncs führt.
2. **Nachgewiesen:** `Console::printf`/`println` führen **synchrones `Serial.println()`** im Empfangspfad aus. Bei jedem empfangenen Byte werden 1–2 Log-Zeilen erzeugt (`[KWP TX ~b]` pro Byte!). Ein blockierender USB-CDC-Schreib des ESP32-P4 kann den loop-Task für unbestimmte Zeit anhalten.
3. **Nachgewiesen:** `#define private public` in `UsbCdcLink.h` bricht die Kapselung der Bibliothek und ist ein strukturelles Architekturproblem.
4. **Wahrscheinlich:** Das Dashboard-Redraw (100–150 ms gedrosselt, aber Vollbild-Sprite-Blits auf 1280×720) blockiert den loop-Task; die `isBusyReceiving()`-Abfrage in `loop()` mildert dies, deckt aber die Phasen zwischen Blöcken nicht ab.
5. **Strukturell:** Es existiert **keine Backpressure-Strategie, keine Empfangs-Task, keine Queue zwischen Empfang und Verarbeitung**. Die vom Projekt selbst formulierte Anforderung ("Empfang darf nicht durch langsame nachgelagerte Verarbeitung blockiert werden") wird **architektonisch nicht erfüllt** – sie wird nur durch Drosselung der Verbraucher *approximiert*.

Die Live-Verifikation (M4, 180 s mit laufendem Motor ohne Protokollfehler) zeigt, dass die aktuellen Timing-Budgets *im Normalfall* reichen. Die Analyse zeigt aber, dass die Zuverlässigkeit **von empirisch abgestimmten Delay-Werten abhängt, nicht von einer strukturellen Entkopplung**.

---

## 2. Verstandene Gesamtarchitektur

### 2.1 Tasks und Nebenläufigkeit

| Kontext | Quelle | Priorität | Zweck |
|---|---|---|---|
| Arduino `loop()`-Task | `M5Tab5_Digifant_Proto.ino` | Arduino-Standard (Priorität 1, Core 0/1 je nach Konfig) | Alles: Protokoll-Zustandsmaschine, Parsing, Decoding, Logging-Aufrufe, UI-Trigger, Touch |
| `EspUsbHost`-Task | Bibliothek, `EspUsbHost.cpp:1663` | 5 (default), Stack 8192, `tskNO_AFFINITY` | USB-Host-Event-Handling, Transfer-Abschluss, **Producer des RX-Rings** |
| USB-Host-Stack-Tasks (esp-idf) | IDF-intern | – | `usb_host`-Library-Task (von `usb_host_install` erzeugt) |
| M5Unified/GFX | Bibliothek | – | Display, Touch (im loop-Task aufgerufen) |

**Im Applikationscode existieren keinerlei FreeRTOS-Primitiven** (grep über `M5Tab5_Digifant_Proto/` auf `xTaskCreate|xQueue|Semaphore|TaskHandle`: leer).

### 2.2 Datenstrukturen und Puffer

| Puffer | Ort | Größe | Ownership | Synchronisation |
|---|---|---|---|---|
| CDC-RX-Ring `rxBuffer_` | `EspUsbHostCdcSerial` (Bibliothek) | 512 B (Default, per `setRxBufferSize()` konfigurierbar – **wird von der App nicht aufgerufen**) | Bibliothek | `portMUX_TYPE rxMux_` (spinlock) |
| `_rxBlockBuf[128]` | `EcuInitTester` | 128 B | loop-Task | nicht nötig (single consumer) |
| `_groupHeader[5][64]` | `EcuInitTester` | 5×64 B | loop-Task | – |
| `Console::_lines[40][96]` | `Console` | statisch | loop-Task | – |
| `SimulatedLink::_rxBuffer[65]` | `SimulatedLink` | 65 B | loop-Task | – |
| Dashboard `_history[240]` (ScopeSample) | `Dashboard` | 240×~8 B | loop-Task | – |
| Sprites (PSRAM) | `Dashboard` | mehrere MB | loop-Task | – |

### 2.3 Globale Singletons

- `Console console` (globale Instanz, `Console.cpp`)
- `Dashboard dashboard` (globale Instanz, `Dashboard.cpp`)
- `serialLink` (globale Instanz, je nach Modus `SimulatedLink` oder `UsbCdcLink`)
- `tester` (global, `EcuInitTester` oder `ConnectivityTester`)

### 2.4 Daten- und Kontrollfluss (Live-Modus, MODE_KWP1281_LIVE_DIAG)

```mermaid
flowchart TD
    ECU[Digifant ECU<br/>K-Line 1200 Baud] <--> K409[AutoDia K409<br/>FTDI FT232R]
    K409 <--> USB[USB-Host-Stack<br/>IDF-Tasks]
    USB --> HOSTTASK[EspUsbHost-Task<br/>Prio 5]
    HOSTTASK -- "pushData()<br/>spinlock, Overflow verwirft älteste Bytes" --> RXRING[CDC-RX-Ring<br/>512 B]
    RXRING -- "available()/read()<br/>Polling" --> LOOP[Arduino loop()-Task<br/>EcuInitTester::update]
    LOOP -- "~b ACK pro Byte<br/>write()" --> RXRING2[USB-TX] --> ECU
    LOOP -- "parseBlock/decode" --> DASH[Dashboard<br/>setGroup000 etc.]
    LOOP -- "console.printf pro Byte" --> CONSOLE[Console<br/>Serial + Zeilenring]
    CONSOLE -- "Serial.println (synchron!)" --> USBCDC[USB-CDC Konsole Host]
    LOOP -- "gedrosselt 100/150 ms" --> DISPLAY[Display-Redraw<br/>Sprites, PSRAM]
```

Kontrollfluss: `loop()` (2 ms Zyklus) → `M5.update()` → `processSerialCommands()` → `serialLink.update()` (nur Simulation) → `tester.update()` → `console.update()`/`dashboard.update()` (nur wenn `!tester.isBusyReceiving()`).

---

## 3. Kritischer Digifant-Empfangspfad

### 3.1 Vollständiger Pfad

1. **Empfang (zeitkritisch):** FTDI-Chip empfängt K-Line-Byte → USB-Bulk-IN → EspUsbHost-Task → `EspUsbHostCdcSerial::pushData()` (Critical Section, Ring schreibt, bei voll `rxTail_` nachziehen = ältestes Byte verwerfen).
2. **Abruf (zeitkritisch):** `EcuInitTester::update()` im loop-Task pollt `_link.available()`/`_link.read()` – **Byte-für-Byte**.
3. **Protokoll-ACK (hart zeitkritisch):** Pro empfangenem Byte (außer Endbyte) muss **innerhalb des ECU-Zeitfensters** `~b` zurückgesendet werden (`EcuInitTester.cpp`, RECEIVE_BLOCK: `_link.write(&inv, 1)`).
4. **Blockverarbeitung (zeitkritisch):** Nach Endbyte 0x03: Counter-Check, `parseBlock()`, `decodeNumberedGroup()`, Dashboard-Setter, `delay(30)`, danach ggf. `requestMeasurementGroup()` → `sendBlockWithHandshake()` (blockierend!).
5. **UI (nicht zeitkritisch):** `console.update()` (Redraw ≥100 ms Intervall), `dashboard.update()` (Redraw ≥150 ms Intervall) – nur wenn `!isBusyReceiving()`.

### 3.2 Bewertung: Ist der Empfang entkoppelt?

**Nein – nur teilweise, auf zwei Ebenen:**

**Ebene 1 (physisch, gut):** Der USB-Task empfängt unabhängig vom loop-Task in den Ring. Solange der Ring nicht überläuft, gehen während loop-Blockaden keine Bytes verloren. Das ist eine echte, wenn auch unbeabsichtigte/bibliotheksseitige Entkopplung.

**Ebene 2 (logisch, unzureichend):** Der Empfangs-*Abruf* und das byte-weise ACK sind an den loop-Task gebunden. Jede Blockade des loop-Tasks verzögert:
- das ACK-Senden → ECU-Timeout → Session-Abbruch (Digifant erwartet Byte-ACK typischerweise innerhalb ~20–35 ms, laut eigener Projektdokumentation),
- das Leeren des Rings → Overflow → **Datenverlust ältester Bytes**.

**Nachgewiesene Blockadequellen im loop-Task (Empfangspfad):**

| Stelle | Datei:Funktion | Dauer | Kategorie |
|---|---|---|---|
| 5-Baud-Bit-Banging | `EcuInitTester::send5BaudAddress` | 10× `delay(200)` = **2000+ ms** | Init-Phase (unkritisch für Daten, aber Watchdog/UI-Blockade) |
| Echo-/ACK-Polling | `sendBlockWithHandshake` | pro Byte bis zu 150 ms (Echo) + 350 ms (ACK) = Worst Case **~500 ms/Byte**, bei 8-Byte-Block bis **4 s** | **kritisch** – währenddessen wird der Ring nicht geleert |
| `delay(30)` nach Blockende | `update()` RECEIVE_BLOCK | 30 ms | zeitkritisch (bewusst, für ECU-Timing) |
| `delay(35)` vor ~KB2 | SEND_INVERTED_KEYWORD | 35 ms | hart zeitkritisch (bewusst) |
| `Serial.println` pro Logzeile | `Console::println` | unbestimmt (USB-CDC-Flow) | **kritisch** – 1–2 Zeilen **pro empfangenem Byte** |
| `vsnprintf` + Ring-Kopie | `Console::printf` | µs–ms | unkritisch einzeln, kumulativ relevant |
| Dashboard-Redraw | `Dashboard::draw` | 20–140 ms (gemessen, `[PERF]`-Log) | zeitkritisch – durch `isBusyReceiving()` nur teilweise ausgeblendet |
| `String`-Operationen | `Dashboard::setEcuInfo`, `Console::println(String)` | Heap-Allokation im Hot Path | potenzielles Risiko (Fragmentierung) |

### 3.3 Worst-Case-Szenario (nachgewiesen konstruierbar)

Ein 50-Byte-ECU-Block (Gruppe-1-Header) erzeugt ~50× `console.printf("[KWP TX ~b]...")` = 50 Serial-Blöcke + 50 Ring-Kopien. Kommt der USB-CDC-Host (PC-Terminal) nicht hinterher, blockiert jeder `Serial.println` (CDC-TX-Buffer voll). Gleichzeitig muss das nächste Byte-ACK gesendet werden. **Der Empfang des nächsten Blocks stockt, der Ring läuft voll, `pushData` verwirft Bytes → Counter-Fehler → ERROR_ → Session-Neustart (2,6 s Idle + 2 s Init).** Genau dieses Fehlerbild (Session-Timeouts unter Last) ist in der Projektgeschichte dokumentiert (Kommentare zu kLoopDelayMs, Console-Redraw-Fix).

---

## 4. Datenfluss und Producer/Consumer-Modell

### 4.1 CDC-RX-Ring (der einzige echte Cross-Task-Pfad)

| Eigenschaft | Wert |
|---|---|
| Producer | EspUsbHost-Task (Kontext: USB-Transfer-Callback) |
| Consumer | loop-Task (`EcuInitTester::update`) |
| Speicher-Owner | `EspUsbHostCdcSerial` (Bibliothek), Heap (`allocateRxBuffer`) |
| Producer-Blockade | nie (Critical Section, wirft weg) |
| Consumer-Blockade | unbeschränkt (Polling-Modell) |
| Überlastverhalten | **Verwirft älteste Bytes stillschweigend** – kein Flag, kein Zähler, keine Benachrichtigung an die Applikation |
| Datenverlust möglich? | **Ja** (nachgewiesen im Bibliothekscode) |
| Mehrfachverarbeitung? | Nein (Tail-Advance) |
| Pointer-Invalidierung? | Nein (by-value-Kopie in Ring) |

**Bewertung:** Der Ring ist korrekt synchronisiert (spinlock), aber das **Overflow-Verhalten ist für ein Protokoll mit Blockzähler und Byte-ACK fatal**: Ein stiller Verlust in der Blockmitte führt zu Endbyte-/Counter-Fehlern, die erst spät erkannt werden. Es gibt **keine Overflow-Detection-API**, die die App nutzen könnte.

### 4.2 `_rxBlockBuf` → `parseBlock` → Dashboard (intra-Task)

Alles im loop-Task, keine Synchronisation nötig, Ownership klar. **Aber:** `parseBlock` wird mit `_rxBlockBuf` aufgerufen, während `decodeNumberedGroup` zusätzlich `_groupHeader[...]` nutzt – beides Member von `EcuInitTester`, Lebensdauer ok. Kein Use-after-free. Einziges Detail: `dashboard.setEcuInfo(String(asciiStr))` kopiert in einen Heap-`String` der Dashboard – ok, aber Heap-Allokation im Hot Path.

### 4.3 Console-Ring (loop-Task schreibt, loop-Task liest)

`Console::_lines` wird von `println` (Aufrufer: u. a. USB-Callbacks? → **Nein**, siehe §6) und von `Dashboard::drawInfo` (Zeile 940–950, `console.getLine(i)`) gelesen. Beides im loop-Task → ok. **Aber:** `UsbCdcLink::begin` registriert `onDeviceConnected`-Callbacks, die `console.printf` aufrufen – diese laufen **im EspUsbHost-Task-Kontext** (siehe §6, Befund R-1).

### 4.4 SimulatedLink (nur Replay-Modus)

Rein intra-Task, request-getrieben, 65-Byte-Puffer, alte Antwort wird bei neuem `write()` verworfen (dokumentiertes, gewolltes Verhalten). Keine Concurrency-Probleme.

---

## 5. FreeRTOS-Analyse

### 5.1 Gefundene Mechanismen (alle in der Bibliothek, keine in der App)

| Mechanismus | Ort | Bewertung |
|---|---|---|
| Task "EspUsbHost" (Prio 5, 8192 Stack, no affinity) | `EspUsbHost.cpp:1663` | Sinnvoll für USB-Event-Handling. Priorität 5 > loop-Task (typ. 1) → USB-Task verdrängt loop. Gut für Empfang, aber: der USB-Task ruft auch **Applikations-Callbacks** auf (siehe R-1). |
| Spinlock `rxMux_` um RX-Ring | `EspUsbHostCdcSerial` | Korrekt und kurz (Critical Section). Architektonisch ok. |
| Binär-Semaphoren `done` für synchrone Control-Transfers | `EspUsbHost.cpp:3019 ff.` | Werden von `setBaudRate`/`setBreak`/`setLatencyTimer` (App!) benutzt → **loop-Task blockiert auf USB-Task** (`xSemaphoreTake(..., portMAX_DELAY)` in manchen Pfaden, 1000 ms-Timeout in anderen). Während des 5-Baud-Bit-Bangings werden **10 Control-Transfers** (setBreak) abgesetzt – jeder kann blockieren. |
| Mutex `hidCallbackMutex_` | HID-Pfad | für CDC irrelevant |
| Counting-Semaphor `usbVendorOutFreeSlots` | Vendor-TX | Flow-Control für TX, ok |

### 5.2 Fehlende Mechanismen in der App (architektonische Lücke)

- **Keine Empfangs-Task:** Der zeitkritische Abruf + ACK läuft im niedrigpriorisierten loop-Task.
- **Keine Queue zwischen Empfang und Verarbeitung:** Blöcke werden direkt im Empfangskontext geparst, dekodiert, geloggt und ans Dashboard gereicht.
- **Keine Task-Notification / Event-Gruppe** für "Block vollständig" – alles Polling mit 2 ms Zyklus.
- **Kein Watchdog-Handling:** `send5BaudAddress` blockiert 2 s; der Idle-Task-Watchdog (falls aktiv) könnte zuschlagen. Ob `CONFIG_ESP_TASK_WDT` im Arduino-Build aktiv ist, konnte aus `sdkconfig` nicht eindeutig verifiziert werden (→ §12).

### 5.3 Prioritäten & Scheduling

- USB-Task (5) > loop (1): Empfang wird priorisiert – **gut**.
- Aber: Der USB-Task führt **Applikations-Callbacks** aus (`onDeviceConnected` → `console.printf` → `Serial.println` → potenziell blockierend). Ein blockierender Callback **lahmt den gesamten USB-Host-Stack** (auch RX!). Das ist ein **HIGH**-Befund (R-1).
- **Priority Inversion:** nicht klassisch vorhanden, aber die Semaphor-Nutzung in `submitVendorSerialControl` (loop wartet auf USB-Task) erzeugt eine faktische Abhängigkeit: Wenn der USB-Task durch einen Callback blockiert ist, blockiert auch der loop-Task → beide lahm.
- **Starvation:** Dashboard/Console-Redraw und loop-Teilen konkurrieren; durch Drosselung (100/150 ms) beherrscht, aber nicht garantiert.

### 5.4 Queue-/Puffer-Bewertung

| Puffer | Größe vs. Last | Urteil |
|---|---|---|
| CDC-RX-Ring 512 B | 1200 Baud ≈ 120 B/s; Block bis 65 B; loop-Blockaden bis ~500 ms/Byte möglich | **Grenzwertig.** Bei 500 ms Blockade: 60 B anfallen – ok. Bei 4 s (`sendBlockWithHandshake`-Worst-Case): 480 B – **Randvoll**. Bei zusätzlich blockierender Serial-Konsole: Overflow. App nutzt `setRxBufferSize()` **nicht** zur Vergrößerung. |
| Console-Ring 40×96 | 1–2 Zeilen/Byte bei 50-Byte-Block = 100 Zeilen > 40 | Log-Verlust (nur Anzeige, unkritisch) – akzeptables Design. |

---

## 6. Race Conditions

**R-1 (HIGH, nachgewiesen im Code, Auslösung wahrscheinlich):** `UsbCdcLink::begin` registriert Lambdas, die `console.printf` aufrufen (`UsbCdcLink.cpp:9–17`). Diese Callbacks laufen im **EspUsbHost-Task**. `console.printf` führt `Serial.println` (USB-CDC-TX, potenziell blockierend) und schreibt **ungesichert** in `Console::_lines`/`_head`/`_count`. Gleichzeitig liest der loop-Task (`Dashboard::drawInfo` → `console.getLine`). Kein Mutex auf `Console`. **Mögliche Folgen:** inkonsistente Zeilen, verlorene Updates, im Worst Fall Endlos-Loop bei `_head`-Halb-Update (praktisch unwahrscheinlich, da uint8_t-Zugriffe atomar auf ESP32 sind – daher "wahrscheinlich" nicht "nachgewiesen" für Korruption; die **blockierende Serial-Ausgabe im USB-Task-Kontext ist aber nachgewiesen möglich**).

**R-2 (MEDIUM, potenziell):** `EcuInitTester::isBusyReceiving()` liest `_state`/`_rxBlockPos` (loop-Task) – konsistent, da gleicher Task. Kein Befund.

**R-3 (LOW, potenziell):** `Dashboard::setStage` wird aus `EcuInitTester::enterState` gerufen, das auch im loop-Task läuft – ok. Aber `SimulatedLink::begin`/`generateReplayFrame` rufen `dashboard.setMode/setStage` – ebenfalls loop-Task. Kein Befund.

**R-4 (MEDIUM, potenziell):** `Console::println` aus USB-Callback (R-1) vs. `Console::update`/`redraw` im loop-Task: `_dirty`-Flag und `_lines`-Zugriff ungeschützt → visuelle Artefakte möglich.

**Keine** ISR→Queue-Pfade in der Applikation (keine eigenen ISRs; `attachInterrupt` nicht in Gebrauch).

---

## 7. Ownership- und Lifetime-Probleme

**O-1 (MEDIUM, nachgewiesen):** `#define private public` in `UsbCdcLink.h` vor `#include <EspUsbHost.h>` – die App greift auf Interna (`submitVendorSerialControl`) zu. Das ist ein ABI-Zeitbomben-Muster: jedes Bibliotheks-Update kann die Kapselung/Semantik ändern. Zudem verschmutzt das Makro jede Übersetzungseinheit, die `UsbCdcLink.h` (auch transitiv über `EcuInitTester.h`) inkludiert.

**O-2 (LOW):** `EcuInitTester` hält `UsbCdcLink&` – lebenslang globales Objekt, ok. `ConnectivityTester` hält `SerialLink&` – ok.

**O-3 (LOW, potenziell):** `Dashboard::_ecuInfo` etc. sind `String` (Heap). `setEcuInfo` im Blockpfad allokriert. Kein UAF, aber Heap-Fragmentierung über lange Läufe (siehe auch CODE_REVIEW_KWP1281.md §1.3 – dort als `String _lines[40]` beschrieben; **im aktuellen Code ist `_lines` ein `char[40][96]`-Array, der frühere String-basierte Ansatz wurde also bereits behoben** – der alte Review-Text ist hier veraltet).

**O-4 (LOW):** `parseBlock`-Payload-Pointer `&_rxBlockBuf[3]` wird an `decodeNumberedGroup` weitergereicht und dort nur gelesen – ok, da `_rxBlockBuf` Member ist und nicht zwischenzeitlich überschrieben wird.

**Keine** Use-after-free-Situationen nachgewiesen; keine dynamischen Buffer mit unklarer Ownership in der App.

---

## 8. Echtzeit- und Blocking-Risiken

### Klassifizierung

| Pfad | Klasse | Begründung |
|---|---|---|
| Byte-ACK `~b` nach RX-Byte | **hart zeitkritisch** | ECU-Fenster ~20–35 ms (Projektdoku); Verzug → Session-Abbruch |
| ~KB2 nach Keybyte 2 | **hart zeitkritisch** | 25–40 ms Fenster; `delay(35)` + Schreibweg muss passen |
| RX-Ring leeren | **zeitkritisch** | Overflow nach ~4 s bei 1200 Baud |
| `delay(30)` Blockende→nächste Anfrage | zeitkritisch | bewusst gewählt |
| `sendBlockWithHandshake` Echo/ACK-Polling | zeitkritisch (TX-seitig) | blockiert aber zugleich den RX-Abruf! |
| `Serial.println` (Console) | **zeitkritisch im Hot Path** (fälschlich als unkritisch behandelt) | kann unbestimmt blockieren |
| Dashboard-Redraw | nicht zeitkritisch | durch `isBusyReceiving()` + 150 ms Drossel approximativ entkoppelt |
| Console-Redraw | nicht zeitkritisch | 100 ms Drossel, `_dirty`-Gate |
| Touch-Handling | nicht zeitkritisch | – |
| 5-Baud-Init (2 s) | nicht zeitkritisch für Daten, aber UI/Watchdog-blockierend | – |

### Konkrete Worst-Case-Abschätzung (Gruppe-1-Header-Block, 50 Bytes)

Pro Byte: `read()` + `write(ACK)` + `printf` (Serial + Ring) ≈ 0,5–5 ms normal; **bis 500 ms**, falls Serial-Konsole blockiert oder Echo-Polling in `sendBlockWithHandshake` (nach dem Block) auf Timeout läuft. Kumuliert: normal ~100–250 ms/Block (plausibel mit beobachteter 10-HZ-Zykluszeit), Worst Case mehrere Sekunden → RX-Overflow → Datenverlust.

### Burst-Verhalten

Die ECU sendet Blöcke zügig hintereinander (Header 50 B + Body 8 B + ACK-Block). Zwischen Blöcken liegt `delay(30)` + ggf. `sendBlockWithHandshake` (mit bis zu 4 s Worst Case). Der 512-B-Ring überbrückt ~4,3 s bei Dauerlast – **kein Sicherheitsabstand**.

---

## 9. Architekturprobleme (strukturell)

**A-1 – Vermischung von Empfang, Protokoll, Dekodierung, Logging und UI-Trigger in einer Klasse/einem Task.** `EcuInitTester::update()` macht alles gleichzeitig: RX-Polling, ACK-Timing, Block-Parsing, Formel-Dekodierung (`decodeNumberedGroup` mit float-Math), Dashboard-Updates und ausführliches Logging. Verantwortlichkeiten sind nicht getrennt; jede Erweiterung (z. B. CSV-Logging auf SD) würde den zeitkritischen Pfad weiter belasten.

**A-2 – Keine Schichtung.** Die Protokollmaschine kennt `Dashboard` und `Console` direkt (statische Singletons, harte Kopplung). Eine Wiederverwendung (z. B. headless Logger) ist nicht möglich.

**A-3 – Keine Backpressure-/Überlaststrategie.** Weder für den RX-Ring (Overflow still) noch für die Console (Zeilen werden einfach überschrieben) noch für das Logging im Hot Path. Es gibt keine Lastbegrenzung wie "Logging nur alle N Blöcke" oder "Log-Detailstufen".

**A-4 – Empfangs-Entkopplung nur empirisch.** Die vom Projekt selbst gesetzte Anforderung (project.md: "Display-Redraws sind aus dem zeitkritischen Pfad entkoppelt") wurde durch **Drosselung** (Intervalle, `isBusyReceiving`-Gate, 2 ms Loop) umgesetzt, nicht durch **strukturelle Trennung** (eigene Task/Queue). Das funktioniert bis zur nächsten Laständerung.

**A-5 – `#define private public`** (siehe O-1) – Verstoß gegen Kapselung, Wartbarkeitsrisiko.

**A-6 – Modus-Auswahl per Compile-Time-`#define`.** Drei Build-Verzeichnisse (`build`, `build_hw`, `build_sim`, `build_usb_check`) belegen den Workflow-Schmerz. Kein Laufzeit-Umschalten, keine Tests auf Zielhardware gegen Simulation vergleichbar (teilweise durch SimulatedLink gemildert).

**A-7 – Keine Fehlerbehandlung für USB-Neuenumeration.** project.md erwähnt gelegentliche Root-Port-Reset-Fehler; `EcuInitTester` reagiert nur mit Retry der Init-Sequenz, prüft aber nicht `hostReady()`/Disconnect-Zustände differenziert in RECEIVE_BLOCK (nur Session-Timeout nach 4 s).

**A-8 – Veraltetes Review-Dokument.** `CODE_REVIEW_KWP1281.md` beschreibt `String _lines[40]` und uint8-Underflow in `decodeNumberedGroup` – beides ist im aktuellen Code bereits behoben (`char[40][96]`; Interpolation in `Dashboard.cpp` nutzt `int16_t`-Casts, **aber** in `EcuInitTester::decodeNumberedGroup` (Zeile ~230) wird noch `uint8_t left/right` mit `(right - left)` in float gerechnet – dort ist der Underflow **weiterhin vorhanden** (nur `Dashboard::decodeTemp8C` ist gefixt). → Befund H-2.

---

## 10. Priorisierte Befunde

### CRITICAL

**C-1: Byte-ACK-Pfad kann durch blockierende Serial-Konsole unbestimmt verzögert werden**
- Datei: `M5Tab5_Digifant_Proto/Console.cpp` (`Console::println` → `Serial.println`), aufgerufen aus `EcuInitTester.cpp` RECEIVE_BLOCK (pro Byte: `console.printf("[KWP TX ~b] ...")`).
- Problem: Das ACK `~b` muss innerhalb ~20–35 ms gesendet werden; zwischen RX-Byte und ACK liegt aber ein synchroner `Serial.println` auf die USB-CDC-Konsole. CDC-TX kann bei vollem Host-Buffer blockieren.
- Auswirkung: Session-Abbruch durch ECU-Timeout; genau das in der Projekthistorie beobachtete Fehlbild.
- Datenfluss: RX-Ring → loop-Task → ACK-TX.
- Priorität: **CRITICAL** (funktionskritisch für den Kerzzweck des Projekts).

**C-2: Keine strukturelle Entkopplung Empfang ↔ Verarbeitung**
- Datei: `M5Tab5_Digifant_Proto.ino` (loop), `EcuInitTester.cpp` (update).
- Problem: Empfangs-Abruf, ACK, Parsing, Dekodierung, Logging, UI-Trigger in einem Task; einzige Pufferung = 512-B-Bibliotheksring mit stillem Overflow.
- Auswirkung: Jede zukünftige Erweiterung (SD-Logging, mehr Gruppen, schnellere Baudrate) verschärft das Timing; Zuverlässigkeit beruht auf abgestimmten Delays.
- Priorität: **CRITICAL** (strukturell; verletzt die projekteeigene Kernanforderung).

### HIGH

**H-1: `sendBlockWithHandshake` blockiert bis ~4 s (Worst Case) und verhindert RX-Abruf**
- Datei: `EcuInitTester.cpp`, `sendBlockWithHandshake` (Echo-Wait 150 ms + ACK-Wait 350 ms pro Byte, `delay(1)`-Schleifen).
- Begründung: 8-Byte-Block × (150+350) ms = 4 s; RX-Ring (512 B @ 1200 Baud ≈ 4,3 s) steht kurz vor Overflow, bevor die Methode zurückkehrt.
- Auswirkung: Datenverlust bei gleichzeitigen ECU-Sendungen; Watchdog-Risiko.
- Priorität: **HIGH**.

**H-2: Integer-Underflow in `EcuInitTester::decodeNumberedGroup` (Formel 0x8B/0x8C)**
- Datei: `EcuInitTester.cpp`, `decodeNumberedGroup`, Zeilen mit `uint8_t left/right; ... (right - left)`.
- Problem: fallende NTC-Kennlinie → `right - left` unterläuft im uint8-Promotion-Kontext vor float-Cast (Ausdruck wird in `int` gerechnet – **Korrektur nach Prüfung**: uint8_t wird zu `int` promoviert, daher ist der Underflow hier **nicht** vorhanden wie ursprünglich im alten Review behauptet; `left + (right-left)*frac/16.0f` ist mathematisch korrekt, da int-Promotion negativ darstellen kann). **Einstufung nach Prüfung: kein Fehler** – der alte CODE_REVIEW-Befund trifft auf den aktuellen Code nicht zu. (Behalten als Klarstellung; kein Handlungsbedarf.)
- Priorität: entfällt (Dokumentations-Klarstellung).

**H-3: Applikations-Callbacks laufen im EspUsbHost-Task und rufen `console.printf`**
- Datei: `UsbCdcLink.cpp` (`onDeviceConnected`/`onDeviceDisconnected`-Lambdas).
- Problem: blockierende Serial-Ausgabe + ungeschützter `Console`-Zugriff im falschen Task-Kontext (siehe R-1).
- Auswirkung: USB-Stack-Lahmung bei Disconnect-Sturm; inkonsistente Console-Daten.
- Priorität: **HIGH**.

**H-4: RX-Ring-Overflow ist still und undetektierbar**
- Datei: Bibliothek `EspUsbHost.cpp:14423–14441` (`pushData` verwirft ältestes Byte); App nutzt keine Overflow-Anzeige.
- Auswirkung: schleichende Protokollfehler ohne Diagnosemöglichkeit.
- Priorität: **HIGH** (Diagnose-Defizit; das Verwerfen selbst ist Bibliotheksverhalten).

### MEDIUM

**M-1: `#define private public` in `UsbCdcLink.h`** – Kapselungsbruch, Upgrade-Risiko (O-1/A-5).

**M-2: 5-Baud-Bit-Banging blockiert 2 s mit harten `delay(200)`** – UI tot, Watchdog-Risiko, keine Task-Yields (`send5BaudAddress`).

**M-3: `setBreak`/`setBaudRate`/`setLatencyTimer` sind synchrone Control-Transfers mit Semaphor-Warten** – jede der 10 Break-Operationen kann den loop-Task blockieren (Bibliothek: `xSemaphoreTake` mit bis zu 1000 ms Timeout bzw. `portMAX_DELAY`).

**M-4: Heap-Allokationen im Hot Path** – `String`-Kopien in `setEcuInfo`, `Console::println(String)`-Überladung, `Dashboard`-`String`-Member; Fragmentierungsrisiko über Stundenläufe.

**M-5: `isBusyReceiving()`-Gate schützt nur den Redraw-Zeitpunkt, nicht die Serial-Log-Ausgaben** – Logging bleibt im kritischen Fenster (C-1).

**M-6: Console-Ring (40 Zeilen) läuft bei 50-Byte-Blöcken über** – Anzeigeverlust (akzeptabel, aber unbewusst).

### LOW

**L-1:** Compile-Time-Modus-Auswahl erzwingt Multi-Build-Verzeichnisse (A-6).
**L-2:** `kLoopDelayMs = 2` – unnötige zusätzliche Latenz im ACK-Pfad (2 ms von 20–35 ms Budget).
**L-3:** `EcuInitTester`-Zustandsmaschine: `SWITCH_9600`-State ist toter Code (Bit-Banging-Pfad).
**L-4:** Veraltetes `CODE_REVIEW_KWP1281.md` (A-8) kann zu Fehlentscheidungen führen.
**L-5:** `Dashboard::draw` misst eigene Dauer und loggt bei >20 ms via `Serial.printf` – wieder Serial im UI-Pfad (unkritisch, da außerhalb RX).

---

## 11. Empfohlene Zielarchitektur (kein Code)

### Leitprinzip
**Der Empfangspfad (RX-Abruf + Byte-ACK) muss in einem eigenen, hochpriorisierten Kontext laufen, der nur von USB-Ereignissen, nicht von Verarbeitung, Logging oder UI abhängt.**

### Schichtenmodell

1. **Transport-Schicht (bestehend verstärkt):** `UsbCdcLink` + Bibliotheks-RX-Ring; RX-Ring vergrößern (z. B. 2–4 KB via `setRxBufferSize`), Overflow-Zähler/Flag exponieren (ggf. Bibliotheks-Fork/PR).

2. **KWP-Protocol-Engine (neu, eigener FreeRTOS-Task, Priorität > loop, z. B. 4–5):**
   - Zustandsmaschine (Init, Handshake, Block-RX, ACK-TX) wie heute, aber **ohne** Logging/Dekodierung/Dashboard-Aufrufe.
   - Sendet vollständige, validierte Blöcke per **FreeRTOS-Queue (by value oder Besitz-Übergabe eines kleinen POD-Structs, feste Größe ≤ 66 B)** an die Verarbeitung.
   - Queue-Länge: einige Dutzend Blöcke (z. B. 32) → Puffer für Sekunden von Consumer-Verzug.
   - **Backpressure-Strategie:** Bei voller Queue ältesten Block verwerfen + Zähler (Messwert-Verlust ist tolerierbar, Empfangs-ACK nicht).

3. **Processing/Decode-Task (oder im loop-Task):** Holt Blöcke aus der Queue, dekodiert Formeln, aktualisiert ein **snapshottbares Datenmodell** (POD-Struct mit Mutex oder double-buffered).

4. **UI-Task (loop oder eigener, niedrigpriorisiert):** Dashboard/Console-Redraw liest nur Snapshots; niemals Rückruf in die Protocol-Engine.

5. **Logging als eigener Consumer:** Log-Zeilen in einen weiteren Ring/Queue; ein Log-Task macht Serial/SD-Ausgaben. Im Protocol-Task **keine** formatierte Ausgaben – nur kompakte Ereignis-Codes.

6. **Callback-Hygiene:** USB-Callbacks (connect/disconnect) dürfen nur ein Flag/Notification setzen – keine Console-Ausgabe im USB-Task.

### Timing-Eigenschaften der Zielarchitektur
- ACK-Latenz deterministisch: nur USB-Write + Task-Prio, kein Serial, kein Display, kein Parsing dazwischen.
- Consumer-Blockade (UI, SD, Serial) wirkt nur auf die Block-Queue, nie auf den RX-Ring.
- Overflow wird sichtbar (Zähler) und ist als Diagnose-Metrik darstellbar.

### Migrationspfad (inkrementell, ohne Big-Bang)
1. Serial-Logging aus dem Byte-ACK-Pfad entfernen (größter Einzelnutzen, geringste Änderung).
2. RX-Ring vergrößern + Overflow-Zähler.
3. Protocol-Engine in eigenen Task mit Block-Queue auslagern.
4. UI/Logging als Consumer dahinter.

---

## 12. Offene Fragen und Unsicherheiten

1. **Watchdog-Konfiguration:** Ob der ESP32-P4-Arduino-Build den Task-WDT auf den loop-Task anwendet, konnte aus `build*/sdkconfig` nicht zweifelsfrei verifizert werden (Datei vorhanden, aber nicht im Detail geprüft). Relevant für die 2-s-Blockade in `send5BaudAddress`.
2. **`EspUsbHostCdcSerial::write`-Blockierverhalten:** Ob TX bei vollem FTDI/USB-Buffer blockiert oder verwirft, wurde nicht im Detail geprüft (nur RX-Pfad verifiziert).
3. **Tatsächliche ACK-Zeitfenster der Digifant-ECU:** Die 20–35-ms-Angabe stammt aus der Projektdoku/Kommentaren, nicht aus einem Datenblatt. Eine exakte Messung (Logik-Analyzer) steht aus.
4. **Core-Pinning:** Auf welchem Core läuft der Arduino-loop-Task auf dem P4 (ESP32-P4 hat 2 Cores HP + LP); `tskNO_AFFINITY` des USB-Tasks könnte zu Cache-Konflikten führen – nicht messbar ohne Tracing.
5. **`submitVendorSerialControl`-Timeout-Pfade:** Es wurden beide Varianten (1000 ms-Timeout und `portMAX_DELAY`) in der Bibliothek gesehen; welcher Pfad für `setBreak` konkret gilt, wäre für das Worst-Case-Budget zu verifizieren.
6. **Ob `EcuInitTester::decodeNumberedGroup`-Interpolation int-promotion-korrekt ist:** Nach Sprachregeln ja (uint8→int-Promotion vor Subtraktion); ein Review mit aktivierten Compilermk.-Warnungen (-Wconversion) würde Restsicherheit geben.
7. **`screenlog.0`** wurde nicht ausgewertet (Screen-Auszug, möglicherweise Hinweise auf reale Timing-Probleme enthalten).
8. **Zukünftige Anforderungen (SD-Logging, CSV):** In project.md als Erfolgskriterium genannt, aber noch nicht implementiert – die Zielarchitektur muss darauf ausgelegt werden.

---

*Ende der Analyse. Keine bestehenden Dateien wurden verändert.*
