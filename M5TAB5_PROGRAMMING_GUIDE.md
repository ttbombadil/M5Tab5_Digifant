# M5Stack Tab5 Engineering Handbook

*Subsysteme, Integrationsmuster und Lessons Learned für Arduino und ESP32-P4*

Dieses Handbook bündelt wiederverwendbare Erkenntnisse aus realen
M5Stack-Tab5-Projekten. Es soll verhindern, dass bekannte Probleme bei Touch,
Display, Audio, microSD, IMU, USB und Multitasking in jedem Projekt erneut
analysiert werden müssen.

Die Inhalte sind keine offizielle Herstellerdokumentation. Sie beziehen sich
auf den jeweils genannten Hardware- und Softwarestand. Abweichende
Boardrevisionen oder Bibliotheksversionen müssen erneut geprüft werden.

**Letzter eingearbeiteter Praxisstand:** 30. August 2026

---

## Schnellstart für neue Projekte

1. Mit `esp32:esp32:m5stack_tab5` bauen.
2. Display, Touchbindung und benötigte Geräte direkt beim Boot prüfen.
3. Jedem Hardwarepfad genau einen Owner zuweisen.
4. Touch und Diagnose vor potenziell langsamen Diensten bedienen.
5. Audio, SD und USB in begrenzten, nichtblockierenden Schritten verarbeiten.
6. Subsysteme einzeln zuschalten und jede Integrationsstufe real testen.

> [!IMPORTANT]
> GPIO23 ist beim verifizierten Tab5-Aufbau die aktive-low Interruptleitung
> des ST7123-Touchcontrollers und kein freier Ausgang. Ein Softwarestatus wie
> `touch=enabled` beweist außerdem keine aktuelle I²C-Erreichbarkeit.

---

## So ist dieses Handbook zu lesen

### Evidenzklassen

Jede wesentliche Aussage wird einer dieser Klassen zugeordnet:

| Status | Bedeutung |
|---|---|
| **Verifiziert** | Auf realem Tab5 reproduzierbar erfolgreich geprüft |
| **Empirisch** | Im konkreten Aufbau beobachtet; nicht als allgemeiner Grenzwert zu verstehen |
| **Versionsabhängig** | Von Board-, Arduino-ESP32-, M5Unified- oder M5GFX-Version abhängig |
| **Offen** | Nicht abschließend untersucht oder in diesen Projekten nicht behandelt |

„Verifiziert“ bedeutet nicht automatisch „für jede Revision gültig“.
Pinbelegungen können beispielsweise real bestätigt und zugleich bei einem
anderen Boardstand erneut prüfpflichtig sein.

### Aufbau der Subsystemkapitel

Jedes Kapitel trennt möglichst konsequent:

- **Reifegrad** des bisherigen Wissens;
- **gesicherte Fakten und Messwerte**;
- **Lessons Learned** mit stabiler Kennung;
- **bewährte Implementierungsmuster**;
- **Fehlermuster und Abnahmebedingungen**;
- **offene Punkte**.

Die Kennungen wie `TOUCH-03` oder `SD-02` können in Issues, Reviews und neuen
Projektdokumentationen referenziert werden.

---

## Inhaltsverzeichnis

1. [Systemübersicht und Reifegrad](#1-systemübersicht-und-reifegrad)
2. [Plattform und Toolchain](#2-plattform-und-toolchain)
3. [Systemarchitektur](#3-systemarchitektur)
4. [Boot, Reset und Versorgung](#4-boot-reset-und-versorgung)
5. [Display und Rendering](#5-display-und-rendering)
6. [Touch](#6-touch)
7. [Audioeingang und Mikrofon](#7-audioeingang-und-mikrofon)
8. [Audioausgang und Lautsprecher](#8-audioausgang-und-lautsprecher)
9. [microSD und Dateisystem](#9-microsd-und-dateisystem)
10. [IMU](#10-imu)
11. [USB-Host](#11-usb-host)
12. [FreeRTOS, Speicher und Ressourcenbesitz](#12-freertos-speicher-und-ressourcenbesitz)
13. [Abhängigkeiten zwischen Subsystemen](#13-abhängigkeiten-zwischen-subsystemen)
14. [Observability und Fehlersuche](#14-observability-und-fehlersuche)
15. [Test- und Inbetriebnahmestrategie](#15-test--und-inbetriebnahmestrategie)
16. [Technische Referenz](#16-technische-referenz)
17. [Offene und noch nicht untersuchte Bereiche](#17-offene-und-noch-nicht-untersuchte-bereiche)
18. [Pflege und Erweiterung des Handbooks](#18-pflege-und-erweiterung-des-handbooks)

---

## 1. Systemübersicht und Reifegrad

### 1.1 Abgedeckte Subsysteme

| Bereich | Reifegrad | Bisherige Evidenz |
|---|---|---|
| ESP32-P4 und Arduino-Toolchain | Verifiziert | wiederholte Builds und Uploads |
| Boot und Reset | Verifiziert / Versionsabhängig | CPU-, Panel- und Touch-Reset untersucht |
| Versorgung/Brownout | Empirisch / Offen | Brownout beobachtet, elektrische Ursache nicht isoliert |
| Display | Verifiziert / Versionsabhängig | Vollbild, Teilupdates und Panel-Recovery getestet |
| Touch/ST7123 | Verifiziert / Empirisch | Kurz-, Langzeit- und Recoverytests |
| Mikrofon | Verifiziert | Stereoaufnahme bis 30 s |
| Lautsprecher | Verifiziert, Basisfunktion | Tonfeedback getestet |
| microSD/SDMMC | Verifiziert | Schreiben, Flush und Rücklesen |
| IMU | Verifiziert | kontinuierlicher 25-Hz-Loggerlauf |
| USB-Host/CDC | Verifiziert | EspUsbHost-basierter Betrieb |
| FreeRTOS-Integration | Teilverifiziert / offen | mehrere Tasks stabil; seltener Assert ungeklärt |
| Netzwerk- und weitere Gerätefunktionen | Offen | in diesen Projekten nicht untersucht |

### 1.2 Wichtigste systemweite Erkenntnis

Die meisten schwer reproduzierbaren Fehler entstanden nicht innerhalb eines
isolierten Subsystems, sondern an dessen Grenzen: Touch plus Redraw, Touch plus
Audio, SD plus Task-Synchronisation oder USB plus Runtime-Diagnose.

Die dauerhaft tragfähige Gegenmaßnahme ist eine Architektur mit eindeutigen
Ownern, kurzen Service-Schritten und messbaren Übergängen zwischen den
Subsystemen.

### 1.3 Index bekannter Fehlversuche

| Fehlversuch oder falsche Annahme | Beobachtung | Bewährte Lösung | Status |
|---|---|---|---|
| Touchkoordinaten etwa alle 5 ms lesen | Datenquelle verstummte nach 51 Events | GPIO23-gesteuert und höchstens 50 Hz lesen | Empirisch |
| Nur CPU-/Flash-Reset ausführen | ST7123 blieb in undefiniertem Zustand | `TP_RST` beim Boot definiert pulsen | Verifiziert |
| `enabled`/`driver=present` als Liveness werten | ST7123 antwortete trotzdem nicht | Firmware-Register über I²C prüfen | Verifiziert |
| Mehrere konkurrierende Touch-Lesewege | Fehlerursache nicht mehr eindeutig isolierbar | ein M5Unified-Touch-Owner | Verifiziert |
| Audioaufnahme aktiv abwarten | Stop-Touch blieb ohne Wirkung | Aufnahme in 250-ms-Serviceblöcken | Verifiziert |
| Leise Aufnahme als Pegelreserveproblem werten | falsche Warnung in ruhiger Umgebung | Pegelreserve nur aus Near-Full-Scale/Clipping ableiten | Verifiziert |
| Kartenstatus nur aus aktivem Mount ableiten | Entfernen konnte verborgen bleiben | kontrollierter Idle-Remount | Empirisch |

---

## 2. Plattform und Toolchain

**Reifegrad:** Verifiziert für den genannten Softwarestand.

### 2.1 Verifizierte Umgebung

- M5Stack Tab5 mit ESP32-P4
- Arduino-ESP32 3.3.11 auf Basis von ESP-IDF 5.5.5
- FQBN `esp32:esp32:m5stack_tab5`
- M5Unified und M5GFX aus der installierten Arduino-Umgebung
- Arduino CLI für reproduzierbare Builds und Uploads
- USB-CDC-Diagnose mit 115200 Baud

Unter macOS erschien das Gerät beispielsweise als
`/dev/cu.usbmodem1101`. Der Name kann sich nach Reset, Upload oder erneutem
Verbinden ändern.

### 2.2 Standardbefehle

```sh
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 MyTab5Project

arduino-cli upload --fqbn esp32:esp32:m5stack_tab5 \
  --port /dev/cu.usbmodemXXXX MyTab5Project

arduino-cli monitor --port /dev/cu.usbmodemXXXX \
  --config baudrate=115200
```

### 2.3 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `PLAT-01` | Verifiziert | FQBN und Core-Version im Projekt dokumentieren; Build-Erfolg allein ist kein Hardwaretest. |
| `PLAT-02` | Verifiziert | Port nach Upload oder Reset neu ermitteln statt einen Gerätenamen dauerhaft vorauszusetzen. |
| `PLAT-03` | Empirisch | Das Öffnen des Monitors kann über DTR/RTS neu starten; Laufzeit und Resetursache mitloggen. |

---

## 3. Systemarchitektur

**Reifegrad:** Verifiziert in Touch-, Audio- und Diagnoseprojekten.

### 3.1 Ereignisse, Zustand und Hardwareeffekte

```text
TouchAdapter ──┐
SerialAdapter ─┼─> UiEvent ─> UiController ─> UiState
TestAdapter ───┘                    │
                                    └─> EffectRequest

EffectRunner ──> Renderer
             ├─> AudioService
             ├─> StorageService
             └─> TouchRecovery
```

Der Zustandsautomat enthält keine Hardwareaufrufe. Er entscheidet nur über
`UiState` und `EffectRequest`. Touch und serielle Befehle erzeugen dieselben
`UiEvent`s. Damit bleibt die Logik host-testbar, während physische
Hardwarepfade separat geprüft werden.

### 3.2 Nichtblockierender Hauptablauf

```cpp
void loop() {
  serviceTouch();
  serviceTouchHealth();
  pollSerial();
  serviceAudio();
  serviceStorage();
  heartbeat();
  M5.delay(1);
}
```

Jeder Service erledigt pro Aufruf nur einen begrenzten Arbeitsschritt.

### 3.3 Ressourcenbesitz

| Ressource | Verbindlicher Owner |
|---|---|
| `M5.update()` und `M5.Touch` | ein gemeinsamer Touch-/Displaypfad |
| Display | ein Renderer |
| `SD_MMC` und offene `File`-Objekte | ein Storage-Service oder eine Task |
| Mikrofon-Lebenszyklus | ein Audio-Service |
| USB-Callbacks | nur kurze Produzenten für begrenzte Queues |
| Hardware-Recovery | zentraler, fehlergetriggerter Pfad |

### 3.4 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `ARCH-01` | Verifiziert | Hardwareeffekte nie direkt im Hit-Test oder Zustandsautomaten ausführen. |
| `ARCH-02` | Verifiziert | Serielle Ereignisinjektion und Touch müssen denselben Eventpfad nutzen. |
| `ARCH-03` | Verifiziert | Ein Owner pro Hardwareobjekt verhindert konkurrierende Treiberzugriffe. |
| `ARCH-04` | Verifiziert | Warteschleifen durch tick-basierte Services ersetzen. |

---

## 4. Boot, Reset und Versorgung

**Reifegrad:** Boot-/Resetpfade verifiziert; Versorgung nur teilweise
untersucht.

### 4.1 M5Unified-Baseline

```cpp
#include <M5Unified.h>

void setup() {
  Serial.begin(115200);
  delay(500);

  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.internal_imu = false;
  M5.begin(cfg);

  M5.Touch.begin(M5.Display.touch() ? &M5.Display : nullptr);

  Serial.printf("display=%dx%d rotation=%u touch=%s driver=%s\n",
                M5.Display.width(), M5.Display.height(),
                static_cast<unsigned>(M5.Display.getRotation()),
                M5.Touch.isEnabled() ? "ready" : "missing",
                M5.Display.touch() ? "present" : "missing");
}
```

Beim Boot zusätzlich Pufferstatus, freien PSRAM und die Ergebnisse aller
aktivierten Geräteinitialisierungen protokollieren.

### 4.2 Unterschiedliche Resetdomänen

Ein CPU- oder Flash-Reset setzte den separat versorgten ST7123 nicht immer
zurück. Der Touchcontroller benötigt deshalb bei jedem Programmstart einen
eigenen, definierten Reset. Der genaue Ablauf steht in [Kapitel 6.4](#64-definierter-touch-reset).

### 4.3 Brownout-Befund

In früheren, stark gekoppelten Builds trat `E BOD: Brownout detector was
triggered` während Touch-/Display- und Audioabläufen auf. Nach Trennung der
Pfade und blockweiser Audioverarbeitung trat in den finalen Abnahmen kein
Brownout mehr auf.

**Offen:** Die elektrische Ursache wurde nicht isoliert. Aus den Tests folgt
nicht, dass Audio allein der Auslöser war.

### 4.4 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `BOOT-01` | Verifiziert | CPU-Reset und Peripherie-Reset als getrennte Vorgänge behandeln. |
| `BOOT-02` | Verifiziert | Boot-Selbsttest vor Anwendungstasks und Kommunikationspfaden ausführen. |
| `BOOT-03` | Empirisch | Bei `E BOD` zuerst Versorgung und gekoppelte Lastpfade untersuchen. |
| `BOOT-04` | Offen | Ursache der früheren Brownouts elektrisch reproduzieren und messen. |

---

## 5. Display und Rendering

**Reifegrad:** Verifiziert; Panel-Recovery ist versionsabhängig.

### 5.1 Geometrie und Rotation

Im getesteten Portraitbetrieb meldete das Display 720 × 1280 Pixel. Geometrie
und Rotation trotzdem immer zur Laufzeit abfragen.

```cpp
if (M5.Display.height() > M5.Display.width()) {
  M5.Display.setRotation((M5.Display.getRotation() + 1) & 3);
}
```

### 5.2 Renderingmuster

- Vollbild nur beim Start oder Layoutwechsel, danach Teilupdates.
- Kleine wiederverwendbare PSRAM-Sprites statt eines Vollbild-Sprites.
- Touch vor dem Rendern lesen und nie aus dem Redraw neu initialisieren.
- Aus einem konsistenten UI-Modell zeichnen und Renderdauer messen.

**Empirische Messwerte:** ungefähr 39 ms für den initialen Vollbildaufbau und
5 bis 11 ms für typische Teilupdates.

### 5.3 Fehlgeschlagener Panel-Probe

Sind Breite oder Höhe nach `M5.begin()` null, wurde das Panel nicht erkannt.
Als begrenzte Recovery wurden höchstens ein früher Neustart und danach wenige
`M5.Display.init()`-Versuche verwendet – vor anderen Tasks und ohne
Rebootschleife.

Diese Recovery ist **versionsabhängig** und nur bei nachgewiesenem Panel-Probe-
Fehler einzusetzen.

### 5.4 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `DISPLAY-01` | Verifiziert | Displaygröße und Rotation nicht hart voraussetzen. |
| `DISPLAY-02` | Verifiziert | Teilupdates verkürzen den kritischen Bedienpfad deutlich. |
| `DISPLAY-03` | Verifiziert | Redraw und Touch-Reinitialisierung strikt trennen. |
| `DISPLAY-04` | Versionsabhängig | Panel-Recovery hart begrenzen und vor allen Anwendungstasks ausführen. |

---

## 6. Touch

**Reifegrad:** Verifiziert mit empirischen Timingwerten und Langzeitevidenz.

### 6.1 Hardwarepfad

| Funktion | Verifizierter Wert |
|---|---|
| Touchcontroller | ST7123 |
| ST7123-I²C-Adresse | `0x55` |
| Firmware-Register | `0x0000` |
| interner IO-Expander | I²C `0x43` |
| IO-Expander-Ausgangsregister | `0x05` |
| `TP_RST` | Bit 5 |
| Touch-Interrupt | GPIO23, active low |

### 6.2 Press, Hold und Release

Nur die erste Flanke eines Fingerdrucks erzeugt ein Ereignis. Release gibt das
Gate für den nächsten Touch frei.

```cpp
bool touchWasDown = false;

void pollTouch() {
  if (M5.Touch.getCount() == 0) {
    touchWasDown = false;
    return;
  }

  const auto detail = M5.Touch.getDetail(0);
  if (!detail.isPressed()) {
    touchWasDown = false;
    return;
  }

  if (touchWasDown) return;
  touchWasDown = true;

  Serial.printf("TOUCH x=%d y=%d size=%u\n",
                detail.x, detail.y,
                static_cast<unsigned>(detail.size));
  // Nur ein UiEvent erzeugen.
}
```

Hit-Tests verwenden die aktuelle Displaygröße. M5Unified und direkter
M5GFX-Zugriff laufen nicht als konkurrierende Touch-Lesewege.

### 6.3 Interrupt und Pollingrate

Robuster Ablauf:

1. GPIO23 in jedem Loop-Durchlauf lesen.
2. Koordinaten nur bei GPIO23 Low abfragen.
3. Zwischen Reads mindestens 20 ms warten.
4. Bei GPIO23 High das Press-Gate nach etwa 30 ms freigeben.

**Empirischer A/B-Befund:** Bei ungefähr 200 Koordinatenreads pro Sekunde
verstummte die Datenquelle nach 51 Ereignissen. Mit maximal 50 Hz wurden 213
Touches ohne Ausfall erkannt. Diese Werte sind keine garantierten Grenzwerte
anderer Revisionen.

### 6.4 Definierter Touch-Reset

```cpp
constexpr uint8_t kIoExpander = 0x43;
constexpr uint8_t kOutputRegister = 0x05;
constexpr uint8_t kTouchResetBit = 1U << 5;

M5.Touch.end();
M5.In_I2C.bitOff(kIoExpander, kOutputRegister,
                 kTouchResetBit, 100000);
M5.delay(20);
M5.In_I2C.bitOn(kIoExpander, kOutputRegister,
                kTouchResetBit, 100000);
```

Danach bis zu etwa 700 ms auf eine gültige Firmwareantwort von `0x55` warten
und erst dann Display-Touchtreiber und `M5.Touch` neu initialisieren.

> [!NOTE]
> Der vollständige Treiber-Rebind ist von der installierten M5Unified- und
> M5GFX-Version abhängig.

### 6.5 Healthcheck und Recovery

Der ST7123 fiel in Langzeittests auch ohne Koordinatenzugriffe aus. Deshalb
wird er alle zwei Sekunden geprüft – nur ohne aktiven Finger und bei GPIO23
High. Ein Reset erfolgt ausschließlich nach I²C-Fehler oder ungültiger
Firmwareantwort, nie vorsorglich nach Redraw, Audio oder SD.

Nach einer Recovery getrennt bestätigen:

1. ST7123 antwortet auf I²C.
2. Display-Touchtreiber initialisiert erfolgreich.
3. `M5.Touch.isEnabled()` bestätigt die Bindung.
4. Ein realer Touch liefert wieder Koordinaten.

**Langzeitevidenz:** 34 Recoveries in 13,6 Stunden ohne CPU-Neustart; ein
weiterer Lauf erkannte nach 54 Minuten und zwei Recoveries erneut Touch.

### 6.6 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `TOUCH-01` | Verifiziert | GPIO23 ausschließlich als Touch-Interrupt behandeln. |
| `TOUCH-02` | Verifiziert | Nur ein Owner liest M5Unified-Touchdaten. |
| `TOUCH-03` | Empirisch | Koordinatenreads auf höchstens 50 Hz begrenzen. |
| `TOUCH-04` | Verifiziert | ST7123 beim Boot unabhängig vom CPU-Reset zurücksetzen. |
| `TOUCH-05` | Verifiziert | Recovery nur nach realem Liveness-Fehler ausführen. |
| `TOUCH-06` | Verifiziert | Softwarebindung, I²C-Antwort und reale Koordinaten getrennt prüfen. |

---

## 7. Audioeingang und Mikrofon

**Reifegrad:** Verifiziert für blockweise 16-kHz-Stereoaufnahme bis 30 s.

### 7.1 Konfiguration

```cpp
auto mic = M5.Mic.config();
mic.sample_rate = 16000;
mic.input_channel = m5::input_channel_t::input_stereo;
mic.over_sampling = 1;
mic.magnification = 2;
mic.noise_filter_level = 0;
M5.Mic.config(mic);
```

### 7.2 Speicherplanung

```text
Bytes = Samplerate × Sekunden × Kanäle × Bytes_pro_Sample
16000 × 30 × 2 × 2 = 1.920.000 Byte
```

Ein 250-ms-Stereo-Arbeitsblock benötigt 16.000 Byte. Zielpuffer und
Arbeitsblock wurden erfolgreich im PSRAM reserviert.

### 7.3 Nichtblockierender Aufnahme-Service

1. Beim Eintritt einmal `M5.Mic.begin()` aufrufen.
2. 250-ms-Blöcke mit `M5.Mic.record(...)` anfordern und später über
   `M5.Mic.isRecording()` abschließen.
3. Zwischen den Blöcken Touch, Serial und Healthcheck bedienen.
4. Fertige Daten kopieren und Statistiken inkrementell aktualisieren.
5. Bei Stop `M5.Mic.end()` aufrufen; laufenden Teilblock verwerfen.

> [!IMPORTANT]
> Keine Schleife wie `while (M5.Mic.isRecording())` verwenden. Sie blockiert
> Touch und UI genau dann, wenn der Benutzer stoppen möchte.

### 7.4 Pegelauswertung

Pro Kanal wurden Minimum, Maximum, Peak, RMS, DC-Anteil,
Near-Full-Scale-Samples und zusammenhängende Clipping-Ereignisse erfasst.

Eine leise Aufnahme bedeutet nicht „zu wenig Pegelreserve“. Die Warnung wurde
ausschließlich aus Near-Full-Scale- und Clippingdaten abgeleitet.

### 7.5 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `AUDIO-IN-01` | Verifiziert | Mikrofonstart und -stop als zentrale Effekte modellieren. |
| `AUDIO-IN-02` | Verifiziert | Aufnahme in kurze Blöcke zerlegen; niemals aktiv warten. |
| `AUDIO-IN-03` | Verifiziert | Statistiken beim Kopieren inkrementell berechnen. |
| `AUDIO-IN-04` | Verifiziert | Lautstärke, Aufnahmedauer und Pegelreserve getrennt bewerten. |

---

## 8. Audioausgang und Lautsprecher

**Reifegrad:** Grundfunktion verifiziert; komplexe Audio-Koexistenz offen.

```cpp
M5.Speaker.begin();
M5.Speaker.setVolume(8);
M5.Speaker.tone(1500, 70);
```

Für geringe wahrgenommene Latenz den Ton vor dem Display-Teilupdate starten.
Bei der Fehlersuche den Lautsprecher als eigenen Hardwareeffekt zuschalten.

| ID | Status | Lesson |
|---|---|---|
| `AUDIO-OUT-01` | Verifiziert | Kurzes Tonfeedback funktioniert als unabhängiger Effekt. |
| `AUDIO-OUT-02` | Offen | Gleichzeitiger komplexer Aufnahme-/Wiedergabebetrieb wurde nicht untersucht. |

---

## 9. microSD und Dateisystem

**Reifegrad:** Verifiziert für 4-Bit-SDMMC, chunkweises Schreiben und
Rückprüfung.

### 9.1 Pinbelegung und Initialisierung

| Signal | GPIO |
|---|---:|
| CLK | 43 |
| CMD | 44 |
| D0 | 39 |
| D1 | 40 |
| D2 | 41 |
| D3 | 42 |

```cpp
#include <FS.h>
#include <SD_MMC.h>

if (!SD_MMC.setPins(43, 44, 39, 40, 41, 42)) {
  // Pin-Konfiguration fehlgeschlagen
}

if (!SD_MMC.begin("/sdcard", false)) {
  // Mount fehlgeschlagen
}
```

> [!NOTE]
> In Arduino-ESP32 3.3.11 lautet die Argumentreihenfolge
> `clk, cmd, d0, d1, d2, d3`. Die Signatur ist versionsabhängig.

### 9.2 Storage-Muster

- Ein Owner für `SD_MMC` und alle `File`-Objekte.
- Große Dateien schrittweise schreiben, etwa 16 KiB pro Loop-Durchlauf.
- Mit `flush()` und `close()` abschließen.
- Kritische Dateien erneut öffnen und Größe sowie Header prüfen.
- Fehler als Zustand melden; UI und Touch bleiben bedienbar.
- `SD_MMC.end()` nur außerhalb aktiver Schreibvorgänge aufrufen.

Das Entfernen einer Karte kann bei aktivem Mount verborgen bleiben. Für eine
belastbare Anwesenheitsprüfung kann im Idle ein kontrolliertes
`SD_MMC.end()`/`begin()` erforderlich sein.

### 9.3 Verifizierter WAV-Test

| Prüfung | Ergebnis |
|---|---|
| PCM-Daten | 304.000 Byte |
| WAV-Header | 44 Byte |
| Gesamtgröße | 304.044 Byte |
| Header-Rückprüfung | erfolgreich |
| Touch nach Schreiben | erreichbar |

### 9.4 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `SD-01` | Verifiziert | Storagezugriffe einem Owner zuordnen. |
| `SD-02` | Verifiziert | Große Writes in 16-KiB-Schritte zerlegen. |
| `SD-03` | Verifiziert | Datei nach dem Schreiben strukturell zurückprüfen. |
| `SD-04` | Empirisch | Ein aktiver Mount kann physisches Entfernen der Karte verdecken. |
| `SD-05` | Offen | Vollständige Hotplug-Semantik während aller Betriebszustände ist nicht untersucht. |

---

## 10. IMU

**Reifegrad:** Verifiziert für kontinuierliches Sampling und Loggerübergabe.

### 10.1 Aktivierung und Lesen

```cpp
auto cfg = M5.config();
cfg.internal_imu = true;
M5.begin(cfg);

const auto available = M5.Imu.update();
const bool haveAccel =
    (available & m5::IMU_Class::sensor_mask_accel) != 0;
const bool haveGyro =
    (available & m5::IMU_Class::sensor_mask_gyro) != 0;
const auto& data = M5.Imu.getImuData();

if (haveAccel) {
  const float ax = data.accel.x;
  const float ay = data.accel.y;
  const float az = data.accel.z;
}
```

Produzenten warten nicht auf langsame Display- oder SD-Consumer. Ein
begrenzter Ringpuffer und Drop-Zähler machen Überlast sichtbar.

**Verifiziert:** Ein realer Loggerlauf erreichte etwa 25 Hz mit lückenlosen
IMU-Sequenzen.

### 10.2 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `IMU-01` | Verifiziert | IMU explizit vor `M5.begin()` aktivieren. |
| `IMU-02` | Verifiziert | Gültigkeitsmasken für Beschleunigung und Gyro getrennt auswerten. |
| `IMU-03` | Verifiziert | Sampling über begrenzten Ringpuffer vom Logger entkoppeln. |

---

## 11. USB-Host

**Reifegrad:** Verifiziert für einen EspUsbHost-basierten USB-CDC-Pfad.

### 11.1 Ablauf

1. Callbacks für Connect, Disconnect, RX und Completion registrieren.
2. Host starten und CDC-Serial öffnen.
3. Zustand, Geräteadresse und Verbindungsgeneration verwalten.
4. RX über einen begrenzten Ingress-Pfad führen.
5. Disconnect, Cancel und verspätete Completion als normale Zustände
   modellieren.

USB-Callbacks führen keine Protokollverarbeitung, Displayausgabe oder
SD-Zugriffe aus. Sie übergeben nur Daten und Ereignisse an begrenzte Queues.

### 11.2 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `USB-01` | Verifiziert | Callbacks kurz halten und Verarbeitung entkoppeln. |
| `USB-02` | Verifiziert | Jede Verbindung mit einer Generation kennzeichnen. |
| `USB-03` | Verifiziert | Disconnect und verspätete Completion als reguläre Zustände behandeln. |
| `USB-04` | Offen | Andere USB-Geräteklassen als der verwendete CDC-Pfad sind nicht bewertet. |

---

## 12. FreeRTOS, Speicher und Ressourcenbesitz

**Reifegrad:** Grundmuster verifiziert; ein seltener Target-Assert bleibt
offen.

### 12.1 Task- und Speicherregeln

- Ownership folgt [Kapitel 3.3](#33-ressourcenbesitz).
- Produzenten blockieren nicht an langsamen Consumern.
- Queues und Ringpuffer sind begrenzt; Drops werden gezählt.
- Shared State läuft über Snapshots, atomare Werte oder definierte Queues.
- Tasks melden Heartbeat, Loopdauer und Stack-High-Water-Mark.
- Große Audio- und Displaypuffer werden bewusst im PSRAM reserviert.

### 12.2 Offener `xTaskPriorityDisinherit`-Befund

In einem Stresslauf mit Arduino-ESP32 3.3.11 trat selten auf:

```text
assert failed: xTaskPriorityDisinherit tasks.c:5156
(pxTCB == pxCurrentTCBs[xPortGetCoreID()])
```

Die Ursache ließ sich nicht abschließend reproduzieren. Bei ähnlichen Fehlern
Taskhandle, Core, Mutex-Owner, Stackreserve, Taskphase, letzte SD-/USB-Operation
sowie Heap und PSRAM aufzeichnen. Fachlich unabhängige Protokolle oder
Dateiformate dabei nicht verändern.

### 12.3 Lessons Learned

| ID | Status | Lesson |
|---|---|---|
| `RTOS-01` | Verifiziert | Queuekapazität, Drops und Stackreserve sind Teil des Laufzeitvertrags. |
| `RTOS-02` | Verifiziert | Shared Hardware darf nicht implizit mehreren Tasks gehören. |
| `RTOS-03` | Offen | Ursache des seltenen `xTaskPriorityDisinherit`-Asserts ist ungeklärt. |

---

## 13. Abhängigkeiten zwischen Subsystemen

### 13.1 Abhängigkeitskarte

| Ausgangspunkt | Abhängigkeit | Mögliche Auswirkung |
|---|---|---|
| serieller Monitor | DTR/RTS → CPU-Reset | Messlauf oder Controllerzustand wird verändert |
| CPU-Reset | separat versorgter ST7123 | Touch kann alten Zustand behalten |
| Displayinitialisierung | M5GFX-Geräteerkennung | Touchbindung kann fehlen |
| Display-Redraw | Loop-Latenz | Touchreaktion verzögert sich |
| Audioaufnahme | Scheduler und PSRAM | Touch/Serial werden bei Blockierung unbedienbar |
| SD-Schreiben | Scheduler, Dateisystem, Locks | UI-Latenz oder Taskfehler |
| IMU-Sampling | Queue und Logger | Drops bei langsamem Consumer |
| USB-Callbacks | Queue und Protokolltask | Backpressure oder verlorene Ereignisse |
| Versorgung | alle Subsysteme | Brownout, Reset oder Peripherieausfall |

### 13.2 Integrationsregeln

1. Änderungen an einem Subsystem immer mit seinen direkten Abhängigkeiten
   testen.
2. Recovery nie aus einem fachlich fremden Pfad auslösen.
3. Puffer-, Queue- und Zeitbudgets vor der Integration festlegen.
4. Fehlerzustände als Daten an UI und Diagnose melden, nicht durch versteckte
   Neustarts kaschieren.
5. Bei einem neuen Fehler zur letzten bestandenen Integrationsstufe
   zurückkehren.

### 13.3 Wiederverwendbare Integrations-Lessons

| ID | Status | Lesson |
|---|---|---|
| `INT-01` | Verifiziert | „Touchfehler nach Aktion“ bedeutet nicht automatisch Fehler im Hit-Test. |
| `INT-02` | Verifiziert | Display-, Audio- und Storageeffekte schrittweise und fehlertolerant ausführen. |
| `INT-03` | Verifiziert | Recovery gehört zum fehlerhaften Subsystem, nicht in den normalen UI-Pfad. |
| `INT-04` | Empirisch | Langzeitausfälle können trotz stabiler Kurztests auftreten. |

---

## 14. Observability und Fehlersuche

### 14.1 Mindestumfang eines Heartbeats

Ein Heartbeat alle zwei bis fünf Sekunden sollte zusammenfassen:

- Laufzeit und Resetursache;
- UI-Zustand und Auswahl;
- Touch-/ST7123-Zähler und GPIO23;
- Renderdauer;
- Audio- und SD-Fortschritt;
- Heap, PSRAM, Task-Heartbeat und Stackreserve.

### 14.2 Fehlerklassifikation

| Symptom | Zuerst prüfen |
|---|---|
| Keine Touchdaten, ST7123 antwortet nicht | I²C, Versorgung, Controllerzustand, `TP_RST` |
| Keine Touchdaten, ST7123 antwortet | Treiberbindung, GPIO23, Pollingpfad |
| Touchdaten vorhanden, keine Aktion | Press-Gate, Hit-Test, UI-Zustand |
| Serielle Zustände funktionieren, Finger nicht | physischer Touchpfad |
| Störung nur nach Displayupdate | Redrawdauer, konkurrierender Touchzugriff |
| Störung nur während Aufnahme | blockierende Audiooperation |
| Störung nur während SD-Zugriff | Schreibgröße, Storage-Owner, Mountzustand |
| Neustart mit `E BOD` | Versorgung und Brownout |
| USB bleibt enumeriert, App antwortet nicht | Task-, Lock-, Scheduler- oder CDC-Pfad |
| Heap stabil, Funktion hängt | Deadlock, blockierender Treiber, Taskzustand |
| PSRAM sinkt zyklisch | verlorene Puffer/Sprites, wiederholte Allokation |
| SD entfernt, Status bleibt „vorhanden“ | stale Mount; Idle-Remount prüfen |
| Displaygröße ist `0x0` | fehlgeschlagener Panel-Probe |

### 14.3 Aussagekraft von Tests

| Test | Beweist | Beweist nicht |
|---|---|---|
| erfolgreicher Compile | Syntax, Typen, Linkage | Hardwareverhalten |
| Host-Unit-Test | deterministische Fachlogik | reale Controllerantwort |
| serielles UI-Ereignis | Zustands- und Effektpfad | Fingerkontakt |
| `touch=enabled` | Softwarebindung | I²C-Liveness, Koordinaten |
| separater Probe-Sketch | grundlegender Hardwarepfad | Gesamtanwendung |
| physischer Langzeittest | getestetes reales Gesamtsystem | andere Revisionen |

---

## 15. Test- und Inbetriebnahmestrategie

### 15.1 Stufengates

| Stufe | Aktivierte Funktion | Abnahmekriterium |
|---:|---|---|
| 1 | reiner Zustandsautomat | vollständige Hosttests |
| 2 | Display und Touch | wiederholte physische Zustandswechsel |
| 3 | vollständiger Renderer | Teilupdates ohne Touchverlust |
| 4 | IMU oder Mikrofon nur konfigurieren | unveränderte Touchstabilität |
| 5 | Start-/Stop-Lebenszyklus | wiederholte Zyklen ohne Reset |
| 6 | kurze blockweise Verarbeitung | Stop während laufender Operation möglich |
| 7 | vollständige Laufzeit und Auswertung | Ergebnis mehrfach reproduzierbar |
| 8 | SD-Schreiben | Datei schreiben und zurückprüfen |
| 9 | Gesamtsystem | physischer Langzeit- und Fehlertest |

### 15.2 Vorgehen beim ersten Fehler

1. Keine weitere Funktion zuschalten.
2. Fehler mit identischer Bedienfolge reproduzieren.
3. Letzte bestandene und erste fehlerhafte Stufe vergleichen.
4. Nur den neu hinzugekommenen Effekt instrumentieren.
5. Nach der Korrektur die gesamte Stufe erneut abnehmen.

### 15.3 Testebenen

- **Host:** C++ für Logik, Python für Formate, Statistik und Werkzeuge.
- **Seriell:** UI-Ereignisse und Hardwareeffekte reproduzierbar auslösen.
- **Target:** Touch, Audio-/SD-Zyklen und erzeugte Dateien real prüfen.
- **Robustheit:** Langzeit- und Fehlerfälle mit Speicher-, Stack-, Drop- und
  Recovery-Zählern testen.

Hosttests ersetzen keine physischen Touch-, Audio- oder Verbindungstests.

---

## 16. Technische Referenz

### 16.1 Pins und Adressen

| Ressource | Wert | Einordnung |
|---|---:|---|
| Touch-Interrupt | GPIO23 | verifiziert; active low |
| ST7123 | I²C `0x55` | verifiziert; Firmware `0x0000` |
| beobachtete ST7123-Firmwareantwort | `0x03` | empirisch im getesteten Gerät |
| IO-Expander | I²C `0x43` | verifiziert |
| `TP_RST` | Register `0x05`, Bit 5 | verifiziert |
| SDMMC CLK | GPIO43 | verifiziert mit Arduino-ESP32 3.3.11 |
| SDMMC CMD | GPIO44 | verifiziert mit Arduino-ESP32 3.3.11 |
| SDMMC D0 | GPIO39 | verifiziert mit Arduino-ESP32 3.3.11 |
| SDMMC D1 | GPIO40 | verifiziert mit Arduino-ESP32 3.3.11 |
| SDMMC D2 | GPIO41 | verifiziert mit Arduino-ESP32 3.3.11 |
| SDMMC D3 | GPIO42 | verifiziert mit Arduino-ESP32 3.3.11 |

### 16.2 Bewährte Zeit- und Blockgrößen

| Funktion | Wert | Einordnung |
|---|---:|---|
| Touch-Koordinatenintervall | mindestens 20 ms | empirisch stabil |
| Touch-Release-Fenster | etwa 30 ms | verifiziert im Projekt |
| Touch-Healthcheck | 2 s | Langzeittest bestanden |
| Touch-Boot-Wartefenster | bis etwa 700 ms | verifiziert im Projekt |
| Audio-Arbeitsblock | 250 ms | verifiziert |
| SD-Schreibblock | 16 KiB | WAV-Test bestanden |
| Diagnose-Heartbeat | 2–5 s | empfohlenes Muster |

### 16.3 Referenzmessungen

| Messung | Ergebnis | Status |
|---|---:|---|
| Display Portrait | 720 × 1280 Pixel | verifiziert im Aufbau |
| Vollbildaufbau | etwa 39 ms | empirisch |
| Display-Teilupdate | etwa 5–11 ms | empirisch |
| Touch-A/B, schnell | Ausfall nach 51 Events bei ca. 200 Reads/s | empirisch |
| Touch-A/B, begrenzt | 213 Touches bei max. 50 Hz | empirisch |
| Touch-Langzeit | 34 Recoveries in 13,6 h | verifiziert |
| Audiozielpuffer | 1.920.000 Byte für 30 s Stereo | verifiziert |
| Audio-Arbeitsblock | 16.000 Byte für 250 ms Stereo | verifiziert |
| WAV-Datei | 304.000 Byte PCM + 44 Byte Header | verifiziert |
| IMU | etwa 25 Hz, Sequenz lückenlos | verifiziert |

---

## 17. Offene und noch nicht untersuchte Bereiche

Offene Punkte werden bewusst festgehalten, statt aus fehlenden Tests eine
Scheinsicherheit abzuleiten.

### 17.1 Bekannte offene Befunde

| ID | Bereich | Offene Frage |
|---|---|---|
| `OPEN-01` | Versorgung | Was verursachte die früheren Brownouts elektrisch? |
| `OPEN-02` | FreeRTOS/SD | Was verursacht den seltenen `xTaskPriorityDisinherit`-Assert? |
| `OPEN-03` | Display | Wie allgemein ist die Panel-Probe-Recovery über Versionen und Revisionen? |
| `OPEN-04` | Touch | Gelten 20-ms-Polling und 2-s-Healthcheck für andere Revisionen unverändert? |
| `OPEN-05` | microSD | Wie verhält sich Hotplug in allen aktiven Betriebszuständen? |
| `OPEN-06` | Audio | Wie verhält sich komplexer gleichzeitiger Aufnahme-/Wiedergabebetrieb? |
| `OPEN-07` | USB | Verhalten weiterer USB-Geräteklassen wurde nicht bewertet. |

### 17.2 Noch nicht durch diese Projekte untersucht

- Wi-Fi- und Bluetooth-Betrieb;
- Low-Power-, Batterie- und Ladeverhalten;
- Multi-Touch-Gesten;
- weitere externe GPIO- und Busszenarien;
- zusätzliche integrierte oder externe Gerätefunktionen außerhalb des hier
  beschriebenen Aufbaus.

Diese Punkte sind keine bekannten Fehler, sondern fehlende Evidenz.

---

## 18. Pflege und Erweiterung des Handbooks

### 18.1 Wann eine Erkenntnis aufgenommen wird

Eine neue Aussage sollte enthalten:

1. betroffene Hardware- und Softwareversion;
2. reproduzierbare Ausgangslage;
3. beobachtetes Fehlverhalten oder Ziel;
4. isolierte Änderung;
5. Messwerte und Abnahmekriterium;
6. Evidenzklasse;
7. verbleibende Unsicherheit.

### 18.2 Vorlage für eine neue Lesson

```markdown
| ID | Status | Lesson |
|---|---|---|
| `SUBSYSTEM-NN` | Verifiziert/Empirisch/Versionsabhängig/Offen | Kurze, wiederverwendbare Aussage. |
```

Ergänzende Details gehören in das fachlich zuständige Subsystemkapitel. Neue
Messwerte werden zusätzlich in Kapitel 16 referenziert; subsystemübergreifende
Effekte in Kapitel 13.

### 18.3 Reviewregeln

- Bestehende Messwerte nicht ohne neue Evidenz verallgemeinern.
- Workarounds mit Version, Trigger und Abbruchgrenze dokumentieren.
- Gelöste Befunde nicht löschen, sondern als Lesson erhalten.
- Offene Punkte erst nach realer Abnahme auf „verifiziert“ setzen.
- Wiederholungen vermeiden: Detail im Subsystem, Kurzreferenz in Kapitel 16.

---

## Projektstart-Checkliste

### Plattform und Boot

- [ ] FQBN und Core-Version dokumentieren.
- [ ] Display, Touchbindung, Puffer und Geräte beim Boot prüfen.
- [ ] Resetursache und Laufzeit ausgeben.

### Architektur

- [ ] Owner für Touch, Display, Audio, Storage und USB festlegen.
- [ ] Zustand und Hardwareeffekte trennen.
- [ ] Alle langen Vorgänge als begrenzte Services implementieren.
- [ ] Queues, Puffer und Drop-Verhalten festlegen.

### Integration

- [ ] GPIO23 ausschließlich für Touch verwenden.
- [ ] Hardwarefunktionen stufenweise zuschalten.
- [ ] Statusbefehl und Heartbeat vor der Integration bereitstellen.
- [ ] Dateien zurücklesen und Hardwarepfade physisch testen.
- [ ] Langzeit- und Fehlerfälle abnehmen.

---

## Fazit

Das Tab5 lässt sich zuverlässig als integrierte Plattform betreiben, wenn
Subsystemgrenzen als Teil des Designs behandelt werden. Die entscheidenden
Muster sind eindeutiger Ressourcenbesitz, nichtblockierende Services,
explizite Resetdomänen, messbare Laufzeitverträge und stufenweise reale
Abnahme. Dieses Handbook hält sowohl bestätigte Lösungen als auch offene
Fragen fest und kann damit projektübergreifend weiterwachsen.
