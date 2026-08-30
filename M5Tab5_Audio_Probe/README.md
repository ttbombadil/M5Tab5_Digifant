# M5Tab5 Audio-Probe – Touch, Aufnahme und WAV

Der Sketch implementiert die vollständige Audio-Probe mit zentralem
Zustandsautomaten, nichtblockierender 20–30-s-Aufnahme, Pegelauswertung und
WAV-Ausgabe auf SD. Touch, Audio und Speicher werden als getrennte Services
im Arduino-`loop()` fortgeschrieben.

Der Lautsprecher und ein direkter Touch-Fallback bleiben deaktiviert. Der
separat versorgte ST7123 erhaelt beim Boot einen definierten TP_RST-Impuls.
Koordinaten werden nur bei aktivem GPIO23-Interrupt und hoechstens alle 20 ms
gelesen. Ein Firmware-Liveness-Check laeuft alle zwei Sekunden; nur nach NACK
oder ungueltiger Antwort wird TP_RST ausgeloest.

## Zustandsfolge

```text
LIST -> DETAIL -> CAPTURING_SIM -> STOP_CONFIRM -> RESULT_SIM
```

In `DETAIL` startet ein Druck auf die mittlere oder rechte Zone die Simulation.
In `CAPTURING_SIM` stoppt jede Zone. Danach setzt die linke Zone fort; Mitte
oder rechts bestaetigen den Stop. Im Ergebnis wiederholt die Mitte den Test,
links oder rechts fuehren zur Liste.

Touch und serielle Bedienung laufen durch dieselbe Ereignisfunktion.

## Serielle Befehle (115200 Baud)

```text
STATUS       aktuellen Zustand und Zaehler ausgeben
SELECT 1..6  einen Test auswaehlen
NEXT         den naechsten regulaeren Zustandsuebergang ausloesen
LIST         aus DETAIL oder RESULT zur Liste zurueckkehren
WAV          aktuelle Aufnahme als /audio_probe.wav speichern
```

Jeder erkannte Fingerdruck erzeugt `TOUCH_PRESS`, jeder Zustandsversuch
`EVENT` und anschliessend `STATUS`. Ein Heartbeat erscheint alle fuenf
Sekunden.

## Tests

```sh
c++ -std=c++17 -Wall -Wextra -pedantic host_tests/test_ui_state.cpp \
  -o /tmp/audio_probe_ui_state_test && /tmp/audio_probe_ui_state_test
python3 -m unittest discover -s host_tests -v
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 \
  --build-path ../build_touch_only .
```

Die physische Abnahme besteht aus mindestens 100 Einzeltouches beziehungsweise
20 vollstaendigen Zustandsrunden. Erst wenn diese Stufe stabil ist, wird der
vollstaendige Renderer und danach jede Hardwarefunktion einzeln zugeschaltet.

Verifizierter Stand vom 29.08.2026: 109 Touches ohne Ausfall, 106 angenommene
und drei zustandsbedingt abgewiesene UI-Ereignisse, stabiler Heap und kein
Reset in rund elf Minuten. Ein vorheriger 50-Hz-Test mit 213 Touches war
ebenfalls stabil; die Speaker-Initialisierung hatte keinen Einfluss.

Renderer-/Langzeitabnahme: 120 serielle Teil-Redraws ohne Vollbildaufbau nach
dem Boot sowie 118 physische Touches ueber rund 3,65 Stunden. Acht reale
Health-Ausfaelle des ST7123 wurden erkannt und automatisch wiederhergestellt.
Maximale Renderdauer 39,5 ms beim Boot, Teilupdates typischerweise etwa 5 bis
11 ms; kein CPU-Reset, Brownout oder Heapverlust.

Passive Audiostufe vom 30.08.2026: 16 kHz Stereo konfiguriert, 1.920.000 Byte
fuer 30 Sekunden PCM und 16.000 Byte fuer einen 250-ms-Block im PSRAM
reserviert. Nach 125 fehlerfreien seriellen Zustandswechseln lief das reale
Geraet rund 13,6 Stunden ohne CPU-Reset. Es verarbeitete 367 physische Touches;
alle 34 erkannten ST7123-Ausfaelle wurden automatisch wiederhergestellt. Heap
und freier PSRAM blieben konstant, die Mikrofon-Laufzeit blieb gestoppt.

Mikrofon-Lebenszyklus vom 30.08.2026: Start und Stop werden als zentrale
Effektanforderungen aus den UI-Uebergaengen abgeleitet. `record()` bleibt
weiterhin ausgeschlossen. 20 serielle und 12 physische Zyklen bestanden
vollstaendig; alle 32 Starts und Stopps waren erfolgreich. Die physischen
Zyklen lieferten 48 angenommene Touches ohne Ablehnung. Kein Reset, Brownout,
Touch-Recovery, Heap- oder PSRAM-Verlust trat auf.

Kurzaufnahme vom 30.08.2026: Der Audio-Service verarbeitet hoechstens vier
250-ms-Bloecke und enthaelt keine Warteschleife. Touch, Healthcheck und Serial
werden vor jedem Audio-Tick bedient. 20 automatische und fuenf physische
Aufnahmen bestanden mit 25/25 Starts und Stopps. 91 Bloecke wurden
abgeschlossen, drei laufende Bloecke durch Touch-Stop sauber verworfen. Alle
16 physischen Touches wurden angenommen; kein Audiofehler, Reset, Brownout,
ST7123-NACK oder Speicherverlust trat in der finalen Abnahme auf. Ein zuvor
beobachteter spontaner ST7123-Ausfall wurde ohne CPU-Reset wiederhergestellt;
die Liveness-Pruefung wurde danach von fuenf auf zwei Sekunden verkuerzt.

Vollaufnahme und Auswertung vom 30.08.2026: Bis zu 120 Bloecke beziehungsweise
30 Sekunden werden in den vorab reservierten PCM-Puffer kopiert. Min/Max,
Peak, RMS-Summe, DC, Near-Full-Scale und Clipping werden inkrementell
berechnet. Die Pegelreserve-Warnung haengt ausschliesslich von
Near-Full-Scale-/Clippingdaten ab. Ein ruhiger 30-s-Test lieferte 480.000
Frames bei 15.811,6 Hz und `level_warning=no`. Ein physischer Touch-Stop nach
20,25 Sekunden lieferte 324.000 Frames bei 15.813,5 Hz, RMS 51/58, null
Near-Full-Scale-Samples und null Clippingereignisse. Auch nach langer
Wartezeit blieben Stop-Bestaetigung und weitere Touchbedienung erreichbar.

SD-/WAV-Abnahme vom 30.08.2026: Das Schreiben erfolgt in 16-KiB-Schritten,
damit Touch und Healthcheck zwischen den Schreibzugriffen weiterlaufen. Zwei
reale Dateien mit 208.000 beziehungsweise 304.000 PCM-Datenbytes wurden
vollstaendig geschrieben. Die finale Firmware oeffnete die 304.044 Byte
grosse Datei erneut und verifizierte Dateigroesse und kompletten 44-Byte-
WAV-Header (`verified=yes`). Danach wechselte die UI seriell zur Liste und
erkannte nach rund 54 Minuten Leerlauf einen physischen Touch korrekt. Zwei
zwischenzeitliche ST7123-Ausfaelle wurden automatisch behoben; kein CPU-Reset,
Brownout, Speicherfehler oder dauerhafter Touchverlust trat auf.
