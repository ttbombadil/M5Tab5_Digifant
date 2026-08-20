# UI/UX-Konzept: Automotive Design System für den M5Stack Tab5

**Projekt:** KWP1281-Diagnoselogger für VW 2E (Digifant 1.7)  
**Display:** 5" IPS LCD (1280 × 720 Pixel, kapazitiver Touchscreen, M5Stack Tab5 / ESP32-P4)  
**Ziel:** Einheitliche, kontrastreiche und automotive-gerechte Visualisierung ohne Farbüberladung. Optimale Ablesbarkeit im Fahrzeug (auch bei wechselnden Lichtverhältnissen / Sonneneinstrahlung).

---

## 1. Gestaltungsprinzipien (Automotive HMI)

1. **Funktionale Farbkodierung statt "Regenbogen-Design":**
   - Jede Farbe hat eine feste, unveränderliche semantische Bedeutung (z. B. Rot = kritischer Grenzwert/Gefahr, Grün = Nennbereich/OK, Gelb = Warnung/Übergang, Cyan = Primärer Messwert/Aktuator).
2. **Harmonische Farbwelten nach Signalgruppen:**
   - Signale werden nach ihrer physikalischen Domäne (Luft, Kraftstoff, Temperatur, Bordnetz, Zustand) farblich gegliedert und bleiben konsistent über alle 3 Tabs (Dashboard, Logs, Messschrieb).
3. **Reduktion von Linienstärken & Rauschen:**
   - Keine doppelten/dreifachen Schmucklinien mit wechselnden Dicken.
   - Einheitliche Linienhierarchie:
     - **Hauptkonturen/Kachelrahmen:** $2\,\text{px}$, abgerundet ($r=8\,\text{px}$).
     - **Signal-Kurven & Zeiger:** $2\text{–}3\,\text{px}$ für schnelle Erkennbarkeit.
     - **Gitter- & Hilfslinien:** $1\,\text{px}$, dezent abgedunkelt.
4. **Flächen- und Kontrasthierarchie:**
   - **Hintergrund (Canvas):** Tiefschwarz (`#000000` / `TFT_BLACK`) – maximiert den Kontrast und reduziert Blendung bei Nachtfahrten.
   - **Karten-Hintergrund (Card Fill):** Dunkles Anthrazit (`#1A1A1A` / `0x18E3`) – trennt Kacheln klar vom Hintergrund, ohne grell zu wirken.
   - **Karten-Kopfbereich & Beschriftung:** Weiß (`TFT_WHITE`) für Haupttitel, Hellgrau (`TFT_LIGHTGREY`) für Subtitel/Einheiten.

---

## 2. Semantische Farbpalette (Color Palette)

| Farbname | Hex / RGB565 | Semantische Bedeutung | Einsatzbereiche |
|---|---|---|---|
| **Anthrazit (Basis)** | `#1A1A1A` / `0x18E3` | Kachel-Hintergrund | Füllung aller Kacheln & Panels |
| **Rahmen-Grau** | `#333333` / `0x31A6` | Inaktive Rahmen & Gitter | Diagramm-Gitter, inaktive Kachelränder |
| **Signal-Grün** | `#00E676` / `0x072E` | Normalbetrieb / Drehzahl / OK | Drehzahl-Kurve & Zeiger, Betriebstemperatur OK, $\lambda=1.0$ |
| **Signal-Cyan** | `#00E5FF` / `0x073F` | Luftpfad & Mechanik | Drosselklappe G69, Ansaugluft (IAT), Luftstatus |
| **Signal-Gelb / Amber** | `#FFD600` / `0xFEA0` | Elektrik / Warnung / Gemisch | Batteriespannung, Übergangszustände, Lambdastatus |
| **Signal-Magenta / Violett** | `#E040FB` / `0xDF1F` | Kraftstoff / Einspritzung | Grundeinspritzzeit ($t_i$), Lastbalken |
| **Signal-Blau** | `#2979FF` / `0x2BCF` | Kalt / Warmlauf / Schub | Kühlmittel-Warmlauf, Schubabschaltungs-Band |
| **Alarm-Rot** | `#FF1744` / `0xF8A4` | Kritische Grenzwerte / Fehler | Drehzahlbegrenzer (>5800), Überhitzung (>105°C), Unterspannung (<11.8V) |

---

## 3. Typografie & Skalen-Design

### 3.1 Schriftgrößen (LGFX-Raster)
- **Große Messwerte (Primärfokus):** Textgröße `4` bis `5` (fett, gut aus Fahrerposition lesbar).
- **Einheiten & Zustände:** Textgröße `2` (unter oder neben dem Hauptwert).
- **Kachel-Header (Titel & Geber):** Textgröße `2` (Titel in Weiß, Geber-Kennung in Hellgrau).
- **Fußzeilen / Diagnose-Details:** Textgröße `1` bis `2` (Rohwerte, KWP-Formeln, Statusbytes).

### 3.2 Drehinstrumente (RPM & Batteriespannung)
- **Einheitlicher Radius:** $r = 90\,\text{px}$, zentriert in der Kachel.
- **Einheitlicher Bogenwinkel:** $270^\circ$ (von $135^\circ$ bis $405^\circ$).
- **Einheitliche Nadel:** Zentrale Nabe ($\varnothing 12\,\text{px}$) in Akzentfarbe, Nadelstrich $2\,\text{px}$ in Reinweiß.
- **Skalenstriche:** 
  - Hauptmarken (alle 1000 RPM bzw. 1 V): $10\,\text{px}$ Länge, mit Zahlenwert.
  - Zwischenmarken: $5\,\text{px}$ Länge, dezent abgetönt.

### 3.3 Balken- & Säuleninstrumente
- **Thermometer (Kühlmittel & Ansaugluft):**
  - Schlanke, abgerundete Röhre ($16\,\text{px}$ breit) mit Kugel unten.
  - Dynamischer Farbverlauf / Farbumschlag: Blau (Kalt/Warmlauf $<70^\circ\text{C}$), Grün (Betrieb $70\text{–}100^\circ\text{C}$), Rot (Überhitzung $>105^\circ\text{C}$).
- **Lambda-Regelbalken:**
  - Horizontaler Balken mit hervorgehobener Mittenkerbe bei $\lambda = 1.000$ (Rohwert $128$).
  - Farbkorridor: Cyan (Mager / Regelung fettet an), Grün ($\pm 5\%$ um 1.0), Amber (Fett / Regelung magert ab).
- **Einspritz-Lastbalken:**
  - Progressiver Füllbalken von links nach rechts in Magenta (Leerlauf $\le 6\,\text{raw}$, Teillast $7\text{–}15\,\text{raw}$, Volllast $>15\,\text{raw}$).

---

## 4. Struktur & Layout der 3 Tabs

### Tab 1: WERTE (8-Kachel-Matrix)
*Aufteilung: 4 Spalten × 2 Zeilen à ca. $305 \times 285\,\text{Pixel}$*

```
+-------------------+-------------------+-------------------+-------------------+
| 1. MOTORSTATUS    | 2. DREHZAHL       | 3. BATTERIE       | 4. DROSSELKLAPPE  |
|    Matrix (6-fach)|    Rundinstrument |    Rundinstrument |    Visu + Wert    |
|    Rahmen: Grün   |    Rahmen: Grün   |    Rahmen: Gelb   |    Rahmen: Cyan   |
+-------------------+-------------------+-------------------+-------------------+
| 5. KÜHLMITTEL     | 6. ANSAUGLUFT     | 7. LAMBDA (O2S)   | 8. EINSPRITZUNG   |
|    Thermometer    |    Thermometer    |    Regelbalken    |    Lastbalken     |
|    Rahmen: Blau   |    Rahmen: Cyan   |    Rahmen: Gelb   |    Rahmen: Magenta|
+-------------------+-------------------+-------------------+-------------------+
```

### Tab 2: ECU & LOG (Systemterminal)
- **Obere Hälfte (2 Kacheln):**
  - Links: ECU-Identifikation (`037906024AG`, SW-Stand `1576`, Motor `VW 2E`).
  - Rechts: Verbindungsstatistik (Modus, RX-Blöcke, K-Line Latenz).
- **Untere Hälfte (Live-Terminal):**
  - Dunkles Terminalfenster mit $1\,\text{px}$ Cyan-Rahmen.
  - Feste Schriftart mit max. 10 sichtbaren Log-Zeilen, automatisches flackerfreies Scrolling.

### Tab 3: MESSSCHRIEB (Live-Oszilloskop)
- **Farben der Zeitreihen (synchron zu Tab 1):**
  - **Drehzahl (RPM):** Grün (`#00E676`), $2\,\text{px}$ Liniendicke.
  - **Drosselklappe (G69):** Cyan (`#00E5FF`), $2\,\text{px}$ Liniendicke.
  - **Lambda-Regelung:** Gelb (`#FFD600`), $1.5\,\text{px}$ Liniendicke.
  - **Einspritzlast ($t_i$):** Magenta (`#E040FB`), $1.5\,\text{px}$ Liniendicke.
- **Hintergrund-Zonierung:**
  - Dunkelblaues Band: Phase **Schubabschaltung** aktiv.
  - Dunkelgrünes Band: Phase **Leerlauf** aktiv.
- **Statusanzeige oben:** `[LIVE 10Hz]` (Grün) bzw. `[PAUSE]` (Amber bei Touch-Freeze).

---

## 5. Implementierungs-Leitfaden für den Code

1. **Konstanten-Definition (z. B. in `Dashboard.h`):**
   ```cpp
   namespace UITheme {
     constexpr uint16_t kBgCanvas   = TFT_BLACK;
     constexpr uint16_t kCardFill   = 0x18E3; // Dark Anthracite (~#1A1A1A)
     constexpr uint16_t kCardBorder = 0x31A6; // Muted Grey
     constexpr uint16_t kColRpm     = 0x072E; // Signal Green
     constexpr uint16_t kColAir     = 0x073F; // Signal Cyan (G69 / IAT)
     constexpr uint16_t kColElec    = 0xFEA0; // Signal Yellow (Battery / Lambda)
     constexpr uint16_t kColFuel    = 0xDF1F; // Signal Magenta (Injection)
     constexpr uint16_t kColCool    = 0x2BCF; // Signal Blue (Coolant)
     constexpr uint16_t kColAlert   = 0xF8A4; // Alarm Red
   }
   ```
2. **Kachel-Rahmen-Rendering:**
   - Standardmäßig dezent in `kCardBorder` zeichnen; nur bei aktivem Alarm/Grenzwert in `kColAlert` umschalten.
3. **Zentrierungs-Regel:**
   - Alle Kachel-Elemente berechnen ihren Mittelpunkt dynamisch über `(w - elementWidth) / 2` und `(h - elementHeight) / 2`.
