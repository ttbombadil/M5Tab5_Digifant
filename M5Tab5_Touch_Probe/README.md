# M5Tab5 Touch Probe

Eigenstaendiger Minimaltest fuer die Touchfunktion des M5Stack Tab5. Das
Projekt verwendet keine Audioaufnahme, SD-Karte, USB-Kommunikation oder
Anwendungslogik aus den anderen Projekten.

## Erwartetes Verhalten

Nach dem Start zeigt der Tab5 einen schwarzen Testbildschirm. Jeder neue
Fingerdruck erzeugt gleichzeitig:

- einen gruenen bzw. gelben Kreis mit Fadenkreuz an der erkannten Position,
- eine Erhoehung des Touch-Zaehler und die Anzeige der Koordinate,
- einen kurzen Ton ueber den Lautsprecher.

Ein Finger wird erst nach dem Loslassen erneut gezaehlt. Die serielle Ausgabe
laeuft mit 115200 Baud. Bei einem erkannten Touch erscheint zum Beispiel:

```text
TOUCH_EVENT number=1 x=640 y=360 size=...
```

Die Rueckmeldung startet den Ton vor dem Display-Refresh. Fuer eine kurze
Reaktionszeit wird kein kompletter Bildschirm neu gezeichnet, sondern nur der
Touchmarker und der dynamische Statusbereich aktualisiert.

Beim Start bestaetigt diese Zeile die Initialisierung:

```text
TOUCH_READY enabled=yes driver=present display=... rotation=...
```

## Build und Upload

Im Projektverzeichnis:

```text
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 M5Tab5_Touch_Probe
arduino-cli upload --fqbn esp32:esp32:m5stack_tab5 --port <serieller-port> M5Tab5_Touch_Probe
```

Der Sketch bindet den Touchtreiber nach `M5.begin()` explizit an das erkannte
Display. Damit kann der Test den reinen Touch-/Displaypfad getrennt von der
komplexen `M5Tab5_Audio_Probe` untersuchen.
