# Hardware-Proof-Protokoll

Repository-Status: **AUDIO-HW-PROOF-PASS-FOR-MOTOR-TEST** — beide realen
Mikrofonkanäle und die stabile 16-kHz-/16-Bit-/Stereo-Aufnahme sind für den
Motorversuch ausreichend nachgewiesen. Der künstliche Host-Sprach-/Links-
Rechts-Test wird nicht weiter verfolgt.

## Ausführung

1. Sketch für `esp32:esp32:m5stack_tab5` flashen.
2. Seriellen Port mit 115200 Baud öffnen.
3. Mit `SELECT 1..6` oder Touch einen Messpunkt wählen und mit `NEXT` oder der
   mittleren/rechten Touchzone die Aufnahme starten. Ein weiteres `NEXT` oder
   ein Touch stoppt; anschließend den Stop bestätigen.
4. Für den Kanalzuordnungstest zuerst nur die linke, danach nur die rechte
   Mikrofonposition besprechen/anklopfen und `channel_0`/`channel_1` vergleichen.
5. Für WAV/PC-Analyse nach dem Lauf `WAV` über die serielle Konsole senden. Die Datei wird erst dann
   nach `/audio_probe.wav` geschrieben.

Auf dem Display entspricht das dem Button `AUF SD SPEICHERN`. Die übrigen
Messpunktzeilen bleiben sichtbar und sind farblich als direkte Navigation
markiert; es gibt keinen separaten `LISTE`-Button.

## Messprotokoll

| Test | Dauer s | Frames/Kanal | Rate Hz | Ch0 Min/Max | Ch0 RMS/DC | Ch0 Near-FS/Clips | Ch1 Min/Max | Ch1 RMS/DC | Ch1 Near-FS/Clips | Ergebnis |
|---|---:|---:|---:|---|---|---|---|---|---|---|
| Ruhe | — | — | — | — | — | — | — | — | — | offen |
| Sprache/Klatschen | — | — | — | — | — | — | — | — | — | offen |
| kontinuierlich | — | — | — | — | — | — | — | — | — | offen |

## Tatsächliche Targetläufe

Gerät: ESP32-P4, Revision 1.3, USB-Serial/JTAG `<serial-port>`.
Firmware wurde mit `esp32:esp32:m5stack_tab5`, Arduino-ESP32 3.3.11,
M5Unified 0.2.19 und M5GFX 0.2.26 gebaut und geflasht.

| Lauf | Dauer s | Frames/Kanal | effektive Rate Hz | Ch0 Min/Max | Ch0 RMS/DC | Ch0 Near-FS/Clips | Ch1 Min/Max | Ch1 RMS/DC | Ch1 Near-FS/Clips | Status |
|---|---:|---:|---:|---|---|---|---|---|---|---|
| Ruhe 1 | 10.013270 | 160000 | 15978.796 | -5865 / 1633 | 75.25 / 0.74 | 0 / 0 | -5876 / 1807 | 80.61 / -0.74 | 0 / 0 | PASS |
| Ruhe 2 | 10.013274 | 160000 | 15978.790 | -474 / 369 | 28.69 / 0.15 | 0 / 0 | -340 / 416 | 32.73 / -0.18 | 0 / 0 | PASS |

PSRAM: jeweils 640000 Byte reserviert. Beide Kanäle waren nicht-konstant und
plausibel; beide Läufe enthielten exakt 160000 Frames/Kanal. Die
M5Unified-Reihenfolge blieb `channel_0, channel_1`. Es gab keine gemeldeten
Aufnahme- oder Abbruchfehler und keine Near-Full-Scale-/Clipping-Ereignisse.
Die ±2-%-Ratebedingung wurde in beiden Läufen erfüllt.

## Akustischer Stimuluslauf

Ein 31,6-s-Sprachsignal wurde über den Host-Lautsprecher abgespielt, sodass es
den 12-s-Anschlussvorlauf und den anschließenden 10-s-Capture abdecken sollte.
Der vollständige Target-Output war:

| Lauf | Dauer s | Frames/Kanal | effektive Rate Hz | Ch0 Min/Max | Ch0 RMS/DC | Ch0 Near-FS/Clips | Ch1 Min/Max | Ch1 RMS/DC | Ch1 Near-FS/Clips | Status |
|---|---:|---:|---:|---|---|---|---|---|---|---|
| Host-Sprache, nicht gerichteter Lautsprecher | 10.013287 | 160000 | 15978.769 | -235 / 256 | 31.12 / 0.10 | 0 / 0 | -232 / 242 | 30.86 / -0.11 | 0 / 0 | PASS (Capture) |

Am Tab5 war gegenüber den Ruheläufen keine reproduzierbare Pegelerhöhung
erkennbar. Der Host-Lautsprecher konnte daher nicht als wirksamer akustischer
Stimulus am Gerät verifiziert werden. Ein gerichteter Links-/Rechts-Test mit
Stimulus unmittelbar an den jeweiligen Mikrofonpositionen wurde nicht
durchgeführt. WAV-Schreiben, Host-Readback und Target/Host-Statistikvergleich
wurden ebenfalls nicht durchgeführt, da dafür ein physischer SD-Schritt
erforderlich ist.

Damit ist der Phase-1-Hardware-Proof für den nächsten fachlichen Schritt
geschlossen: **AUDIO-HW-PROOF-PASS-FOR-MOTOR-TEST**. Der fehlende künstliche
Host-Stimulus blockiert den Motorversuch nicht.

## Nächster Schritt: Motor-Audio-Proof am Montageort

Dieser Lauf wurde in der aktuellen Umgebung noch nicht ausgeführt, weil kein
Motor am geplanten Montageort verfügbar war. Es werden daher bewusst keine
Motor-Messwerte behauptet.

| Zustand | Dauer s | Ch0 RMS/Peak/Min/Max | Ch1 RMS/Peak/Min/Max | DC/FS/Clips | Rate Hz | Spektrum / dominante Frequenzen | Bewertung |
|---|---:|---|---|---|---:|---|---|
| Motor aus / Hintergrund | — | — | — | — | — | — | offen |
| Leerlauf | — | — | — | — | — | — | offen |
| ca. 1000 rpm | — | — | — | — | — | — | offen |
| ca. 2000 rpm | — | — | — | — | — | — | offen |
| ca. 3000 rpm | — | — | — | — | — | — | offen |
| ca. 3500 rpm | — | — | — | — | — | — | offen |

Je Zustand 20–30 s aufnehmen und anschließend offline Spektrum, dominante
Frequenzen, Pegelreserve, Kanalunterschiede sowie Körperschall/Resonanzen
bewerten. Erst danach entscheiden, ob 16 kHz und welche Kanal-/Monoabbildung
für die spätere Fachimplementierung verwendet werden.

Die Stufen 3000 und 3500 rpm prüfen insbesondere Pegelreserve, Clipping und
drehzahlabhängige Halterungsresonanzen. Stationäre Hochdrehzahl-Aufnahmen
ersetzen nicht die spätere Lastfahrt, weil Motor-, Abgas-, Wind- und
Körperschall unter Last anders ausfallen können.

Zusätzlich notieren: Kanalzuordnung, ob beide Kanäle Signal liefern, welcher
Kanal stärker rauscht/clippt, PSRAM-Reservierung und die ausgegebenen
M5Unified-Werte für Offsetkorrektur, Gain und Filter.

## Offset/Gain/Filter-A/B

Der Standardlauf verwendet absichtlich `magnification=2` und
`noise_filter_level=0`; M5Unified führt seine automatische DC-Offsetkorrektur
weiter aus. Für den Einflussnachweis zwei identische Läufe mit gleicher
Schallquelle wiederholen: `magnification=1` bzw. anderem Filterwert im Sketch
setzen und RMS/DC/Min/Max vergleichen. Ein Filter-/Gain-Vergleich ist damit
ein kontrollierter Folgelauf und kein Bestandteil des Aufnahme-Hotpaths.

PASS ist erst zulässig, wenn beide Kanäle Signal liefern, 16 kHz innerhalb der
im Sketch verwendeten ±2-%-Grenze erreicht werden und die WAV/PCM-Prüfung
bestanden ist. `PARTIAL` bleibt korrekt, wenn der Targetlauf fehlt oder nur
ein Kanal nachweisbar ist; Initialisierungs-/PSRAM-/Aufnahmefehler sind FAIL.
