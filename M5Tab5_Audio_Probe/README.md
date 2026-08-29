# M5Tab5 Audio-Probe

Eigenstaendiger Audio-Hardware-Proof fuer den M5Tab5. Die Anwendung verwendet
eine statische Testliste mit sechs unabhaengig waehlbaren Motor-Audio-Tests.
Sie ist nicht in den Analyzer eingebunden und veraendert keinen Analyzer-,
KWP-, IMU- oder DLOG-Code.

## Testliste und Bedienung

Nach dem Boot sind alle sechs Tests ohne Scrollen in **einer** Liste sichtbar.
Jede komplette Zeile ist genau ein Touchbutton; es gibt keine Unterbuttons.
Der erste Klick waehlt das Feld, weitere Klicks auf dasselbe Feld fuehren
durch Start, Stop, Stop-Bestaetigung, Ergebnis und Wiederholung. Alle anderen
Zeilen bleiben kompakt sichtbar. Ein anderes Feld darf den Fokus nur vor dem
Start oder nach Abschluss/Abbruch uebernehmen. Waehrend Capture,
Stop-Bestaetigung und Abbruchbestaetigung bleibt der Fokus gesperrt.

Serielle Konsole: 115200 Baud.

```text
SELECT 1 .. 6   Test auswaehlen
START           ausgewaehlten Test starten / fruehen STOP fortsetzen
STOP            stoppen; unter 20 s bestaetigen
LIST            statische Testliste ausgeben und anzeigen
REPEAT          ausgewaehlten Test wiederholen
WAV             letzte Aufnahme nach /audio_probe.wav schreiben
STATUS          Zustand, Test, Frames und PSRAM-Status
ABORT           Abbruchdialog oeffnen
ABORT CONFIRM   Abbruch bestaetigen
HELP            Befehle ausgeben
TOUCHMAP        Touchrechtecke und Feldmittelpunkte ausgeben
TOUCH x y       echten Feld-Hit-Test seriell injizieren
TOUCHDIAG       Controller-, Interrupt- und Ereigniszaehler ausgeben
TOUCHDIAG ON    5-s-Diagnose-Heartbeat aktivieren
TOUCHDIAG OFF   Diagnose-Heartbeat deaktivieren
TOUCHDIAG TONE  akustische Rueckmeldung separat testen
TOUCHDIAG BUS   Touchcontroller per I2C identifizieren und pruefen
```

Die Firmware wertet sowohl den von `M5.update()` gelieferten Touchzustand als
auch einen direkten M5GFX-Hardwarezugriff als Fallback aus. Jeder physisch
erkannte Touch erzeugt `TOUCH_RAW` und `TOUCH_HW` auf der seriellen Konsole,
einen sichtbaren Kreis an der erkannten Koordinate und ausserhalb einer
laufenden Aufnahme einen kurzen hohen Ton. Ein tiefer Ton kennzeichnet einen
wegen Fokus-Sperre abgewiesenen Feldwechsel. Der Heartbeat zeigt auch ohne
Touch, ob Treiber, Displayzuordnung und Touch-Interruptpin vorhanden sind.
Nach dem Start und nach jedem Ton, der den ST7123 auf diesem Tab5 in einen
nicht antwortenden Zustand versetzt, prueft die Anwendung den Controller und
setzt ausschliesslich dessen Resetleitung kontrolliert Low/High. Die serielle
Ausgabe `TOUCH_RECOVERY ... response=yes driver=ready` bestaetigt die
Wiederherstellung.

Die sechs Tests sind Motor aus, Leerlauf sowie etwa 1000, 1500, 2000 und
2500 rpm. Standarddauer ist 20 s, die maximale Dauer 30 s. Ein STOP unter
20 s oeffnet eine Bestaetigung; `START` setzt fort und ein zweites `STOP`
beendet mit einem ungueltigen Ergebnis. Nach Test 6 lautet die Anleitung:
`Motor in Leerlauf, dann ausschalten. WAV auf SD speichern?`

## Technische Umsetzung

- getestet mit M5Unified 0.2.21 und M5GFX 0.2.28; M5GFX 0.2.27 behebt die
  Tab5-Initialisierung durch explizites Warten auf den Touchcontroller
- 16 kHz, signed PCM 16 Bit, Stereo-Interleaving `channel_0, channel_1`
- 30-s-Stereo-Zielpuffer und 250-ms-Zwischenpuffer werden vorab in PSRAM reserviert
- Aufnahme erfolgt in kurzen Abschnitten; Touch und Konsole werden zwischen
  den Abschnitten gepollt, dadurch ist STOP moeglich
- keine Allokation und kein SD-Zugriff im Capture-Pfad
- Lautsprecher und Mikrofon laufen nie gleichzeitig; UI-Ton endet vor Mic-Start
- WAV wird erst nach der Auswertung ausserhalb des Capture-Pfads geschrieben
- Ergebnis wird `FERTIG` nur bei Signal auf beiden Kanaelen, Rate innerhalb
  +/-2 %, ohne Clipping/Near-Full-Scale und ohne Capture-Fehler

## Tests

Hosttests:

```text
python3 -m unittest discover -s host_tests -v
```

Firmware-Build:

```text
arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 --build-path build_static .
```

Ein Target-Smoketest umfasst `LIST`, `SELECT`, `START`, STOP unter 20 s mit
Fortsetzen, STOP ab 20 s, `REPEAT`, `WAV`, `ABORT CONFIRM` und die Statusliste.
