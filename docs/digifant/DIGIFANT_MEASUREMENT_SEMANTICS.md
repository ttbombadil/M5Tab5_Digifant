## Digifant 1.7 – Semantik der Messgruppen 000–004

### Evidenzklassen

| Evidenzgrad | Bedeutung                                                                                |
| ----------- | ---------------------------------------------------------------------------------------- |
| `OFFICIAL`  | Feldbedeutung durch VW/VAG-Unterlagen belegt                                             |
| `REFERENCE` | Formel/Interpretation durch funktionierende Referenz bzw. bestehenden Decoder gestützt   |
| `INFERRED`  | Formel aus Grenzwerten/Captures plausibel hergeleitet, aber nicht offiziell dokumentiert |
| `UNKNOWN`   | Bedeutung oder Umrechnung derzeit nicht sicher bekannt                                   |

---

### Gruppe 000 – Sonderformat ohne Formelheader

| Feld | Snapshot-Zone | Bedeutung                                            | Rohwert im Capture | Berechnung / Formel                               | Einheit | Semantik   | Formelstatus |
| ---- | ------------: | ---------------------------------------------------- | -----------------: | ------------------------------------------------- | ------- | ---------- | ------------ |
| F1   |            Z0 | Ansauglufttemperatur                                 |              54…55 | VW-Grenzwerte bekannt, exakte Kennlinie unbekannt | °C      | `OFFICIAL` | `UNKNOWN`    |
| F2   |            Z1 | Versorgungsspannung Motorsteuergerät                 |                146 | `24 × raw / 256` passt zu VW-Grenzwerten          | V       | `OFFICIAL` | `INFERRED`   |
| F3   |            Z2 | Kühlmitteltemperatur                                 |                7…9 | VW-Grenzwerte bekannt, exakte Kennlinie unbekannt | °C      | `OFFICIAL` | `UNKNOWN`    |
| F4   |            Z3 | Motorlast                                            |              53…98 | ca. `raw × 0,39 %`, nicht offiziell belegt        | %       | `OFFICIAL` | `INFERRED`   |
| F5   |            Z4 | Lambda-Sondenspannung                                |            118…148 | inverse Kennlinie, exakte Gleichung unbekannt     | V       | `OFFICIAL` | `UNKNOWN`    |
| F6   |            Z5 | Zeitzähler: Lambdaregelung zeitweise inaktiv         |                  0 | Rohzähler                                         | count   | `OFFICIAL` | —            |
| F7   |            Z6 | Zähler für nicht verwertbares/fehlendes Sondensignal |                  0 | Rohzähler                                         | count   | `OFFICIAL` | —            |
| F8   |            Z7 | Drosselklappenpotentiometer-Spannung                 |             37…112 | `raw × 0,02 V` passt zu VW-Grenzwerten            | V       | `OFFICIAL` | `INFERRED`   |
| F9   |            Z8 | Einspritzzeit, interner Rechenwert                   |                3…5 | `raw × 0,5`                                       | ms      | `OFFICIAL` | `INFERRED`   |
| F10  |            Z9 | Motordrehzahl                                        |            121…209 | `163840 / raw` plausibel                          | rpm     | `OFFICIAL` | `INFERRED`   |

### Wesentliche Korrektur zu bisherigem Prototyp

| Bisherige Interpretation               | Offizielle Bedeutung                              |
| -------------------------------------- | ------------------------------------------------- |
| Gruppe 000 Z3 = RPM                    | **falsch** → Motorlast                            |
| Gruppe 000 Z9 = anderer/raw Wert       | **falsch** → Motordrehzahl                        |
| Gruppe 000 Z1 = CO-Potentiometer       | **falsch** → ECU-Versorgungsspannung              |
| Gruppe 000 Z5 = allgemeiner Status     | **falsch** → Lambda-Regelungs-Zeitzähler          |
| Gruppe 000 Z6 = Motorlauf-Flag         | **falsch** → Sondensignal-Zähler                  |
| Gruppe 000 Z7 = unspezifischer Rawwert | **falsch** → Drosselklappenpotentiometer-Spannung |

---

## Gruppen 001–004

| Gruppe | Zone | Formel / NWB | Berechnung                  | Bedeutung                                             | Einheit         | Semantik   | Formelstatus        |
| -----: | ---: | ------------ | --------------------------- | ----------------------------------------------------- | --------------- | ---------- | ------------------- |
|    001 |    1 | `0x8B / 26`  | `interp(T,raw) × 26`        | Motordrehzahl                                         | rpm             | `OFFICIAL` | `REFERENCE`         |
|    001 |    2 | `0x8C / 40`  | `interp(T,raw) - 40`        | Kühlmitteltemperatur                                  | °C              | `OFFICIAL` | `REFERENCE`         |
|    001 |    3 | `0x85 / 2`   | `2 × raw / 256`             | Lambda-Sondenspannung                                 | V               | `OFFICIAL` | `REFERENCE`         |
|    001 |    4 | `0x88 / 255` | `raw & 0xFF`                | Einstellbedingungen                                   | Bitfeld         | `OFFICIAL` | Codierung `UNKNOWN` |
|    002 |    1 | `0x8B / 26`  | `interp(T,raw) × 26`        | Motordrehzahl                                         | rpm             | `OFFICIAL` | `REFERENCE`         |
|    002 |    2 | `0x89 / 50`  | `raw × 0,5`                 | Einspritzzeit                                         | ms              | `OFFICIAL` | `REFERENCE`         |
|    002 |    3 | `0x85 / 24`  | `24 × raw / 256`            | Versorgungsspannung Motorsteuergerät                  | V               | `OFFICIAL` | `REFERENCE`         |
|    002 |    4 | `0x8C / 40`  | `interp(T,raw) - 40`        | Ansauglufttemperatur                                  | °C              | `OFFICIAL` | `REFERENCE`         |
|    003 |    1 | `0x8B / 26`  | `interp(T,raw) × 26`        | Motordrehzahl                                         | rpm             | `OFFICIAL` | `REFERENCE`         |
|    003 |    2 | `0x81 / 100` | Kandidat: `100 × raw / 255` | Motorlast                                             | %               | `OFFICIAL` | `INFERRED`          |
|    003 |    3 | `0x84 / 2`   | unbekannt                   | Drosselklappenwinkel G69                              | Grad            | `OFFICIAL` | `UNKNOWN`           |
|    003 |    4 | `0x81 / 100` | Prozentskalierung plausibel | Sollwert Tastverhältnis Leerlaufstabilisierungsventil | %               | `OFFICIAL` | `INFERRED`          |
|    004 |    1 | `0x8B / 26`  | `interp(T,raw) × 26`        | Motordrehzahl                                         | rpm             | `OFFICIAL` | `REFERENCE`         |
|    004 |    2 | `0x81 / 100` | Prozentskalierung plausibel | Motorlast                                             | %               | `OFFICIAL` | `INFERRED`          |
|    004 |    3 | `0x87 / 1`   | unbekannt                   | Geschwindigkeitssignal                                | vermutlich km/h | `OFFICIAL` | `UNKNOWN`           |
|    004 |    4 | `0x88 / 255` | `raw & 0xFF`                | Motorlast-Betriebszustand                             | Bitfeld/Enum    | `OFFICIAL` | Codierung `UNKNOWN` |

---

## Aktueller Datenmodell-Stand

Die V2-Implementierung hält jetzt **alle 26 beobachteten ECU-Felder** dauerhaft vor:

| Bereich    |        Anzahl |
| ---------- | ------------: |
| Gruppe 000 |     10 Felder |
| Gruppe 001 |       4 Zonen |
| Gruppe 002 |       4 Zonen |
| Gruppe 003 |       4 Zonen |
| Gruppe 004 |       4 Zonen |
| **Gesamt** | **26 Felder** |

Für jedes Feld werden mindestens Gruppe, Zone, Rawwert, Formel-ID, NWB, dekodierter Wert, Semantik/Einheit, Gültigkeitsstatus, Timestamp, Sequenz, Sessionepoch und Transportgeneration gespeichert. Unbekannte Werte bleiben bewusst Raw-only; es werden keine unbelegten Formeln erfunden.

## Noch gezielt zu verifizieren

Die folgenden Punkte sind der verbleibende fachliche Evidenzbedarf. Bereits
abgeschlossene Korrelationen (RPM, ECU-Spannung und G69-Monotonie) sind in
`M5Tab5_Digifant_Analyzer/verification.md` historisch dokumentiert und werden
hier nicht erneut als offen geführt.

| Priorität | Experiment                                      | Zweck                                              |
| --------- | ----------------------------------------------- | -------------------------------------------------- |
| 3         | Kaltstart / Warmlauf                            | Kühlmittel- und Ansauglufttemperatur prüfen        |
| 5         | Einspritzzeit bei Leerlauf / Drehzahlerhöhung   | `000/Z8` gegen `002/2` prüfen                      |
| 6         | Motorlast unter verschiedenen Betriebszuständen | Formel `0x81` prüfen                               |
| 7         | Warmer Motor / Lambdaregelung                   | `000/Z4` gegen `001/3` korrelieren                 |
| 8         | Fahrt / Rollenprüfstand                         | `004/3` Geschwindigkeitssignal herleiten           |

### Wichtigste fachliche Konsequenz

**Semantik und Umrechnungsformel müssen getrennte Evidenz besitzen.** Ein Feld kann offiziell als „Motorlast“ identifiziert sein, während die konkrete Raw→%-Formel noch `INFERRED` oder `UNKNOWN` bleibt.

## Primärquelle: VW/VAG-Prüfunterlage 04.1993

Die Feldbedeutungen und Prüfbereiche wurden direkt gegen folgende lokale VW/VAG-
Unterlage geprüft:

eine lokale, nicht versionierte VW/VAG-Prüfunterlage von 04/1993

Die Unterlage bestätigt die 1-basierte V.A.G.-1551-Anzeige als folgende
0-basierte Snapshot-Zonen:

| Snapshot-Zone | Offiziell bezeichnetes Feld |
|---:|---|
| 000/Z0 | Ansauglufttemperatur |
| 000/Z1 | Spannungsversorgung Motorsteuergerät |
| 000/Z2 | Kühlmitteltemperatur |
| 000/Z3 | Motorlast |
| 000/Z4 | Lambda-Sondenspannung |
| 000/Z5 | Zeitzähler der Lambdaregelung (aktiv/nicht aktiv) |
| 000/Z6 | Zähler für nicht verwertbares bzw. fehlendes Sondensignal |
| 000/Z7 | Spannung des Drosselklappenpotentiometers |
| 000/Z8 | Einspritzzeit, steuergeräteinterner Rechenwert |
| 000/Z9 | Motordrehzahl |

Ebenso bestätigt die Unterlage die Feldidentitäten der Gruppen 001–004. Für
Gruppe 003 nennt sie zusätzlich die Prüfbereiche für Drosselklappenwinkel und
Motorlast sowie die Bedeutung des internen Sollwert-Tastverhältnisses. Für
Gruppe 004 sind `255` im Stand und `0` im Fahrbetrieb als Prüfwerte des
Geschwindigkeitssignals angegeben. Die acht Zustandsbits von 004/Z4 werden als
Vollast, Teillast, Leerlauf und Schubabschaltung beschrieben.

Diese Quelle hebt die **Semantik-Evidenz** auf `OFFICIAL`; sie bestätigt nicht
automatisch jede konkrete Rawwert-Umrechnungsformel. Formelstatus und
Semantikstatus bleiben daher getrennt.
