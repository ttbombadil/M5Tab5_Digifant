# M5Tab5 Audio-Probe – Touch-only-Baseline

Diese Zwischenstufe isoliert die Touchbedienung der Audio-Probe. Sie verwendet
bereits den vorgesehenen Zustandsautomaten und die Sechs-Zeilen-Oberflaeche,
fuehrt aber noch keine Audio- oder Speicherfunktion aus.

Bewusst deaktiviert sind Lautsprecher, Mikrofon, SD und ein direkter
Touch-Fallback. Der separat versorgte ST7123 erhaelt beim Boot einmal einen
definierten TP_RST-Impuls. Koordinaten werden nur bei aktivem GPIO23-Interrupt
und hoechstens alle 20 ms gelesen. Ein Firmware-Liveness-Check laeuft alle
fuenf Sekunden; nur nach NACK oder ungueltiger Antwort wird TP_RST ausgeloest.

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
