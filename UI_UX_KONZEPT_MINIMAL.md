# UI/UX-Konzept: Minimalistisches Automotive Design System für M5Stack Tab5

**Projekt:** KWP1281-Diagnoselogger für VW 2E (Digifant 1.7)  
**Display:** 5" IPS LCD (1280 × 720 Pixel, kapazitiver Touchscreen, M5Stack Tab5 / ESP32-P4)  
**Leitmotiv:** *"Monochrom mit gezielten Signalakzenten"* – Höchste Ablesbarkeit bei Tag und Nacht, null Ablenkung, professioneller OEM-/Werkstatt-Look.

---

## 1. Gestaltungsphilosophie: Sparsamer Farbeinsatz

Im Cockpit- und Motorsportbereich gilt: **Farbe ist Information, kein Dekor.**
1. **Neutraler Basiszustand (90 % der Fläche):**
   - Hintergrund, Kacheln, Skalen, Ziffern und Achsenbeschriftungen bestehen ausschließlich aus **Schwarz, Anthrazit, Weiß und dezentem Grau**.
   - Werte im Normalbereich werden **monochrom (Weiß auf dunklem Grund)** dargestellt.
2. **Akzente nur bei Zustandsänderung oder Grenzwerten (10 % der Fläche):**
   - **Signal-Grün:** Bestätigter Normalbetrieb / aktiver Zustand (z. B. Leerlauf, Lambda = 1.0 OK, Verbunden).
   - **Signal-Blau:** Kaltbetrieb / Schubabschaltungsphase (Schubbetrieb).
   - **Signal-Gelb (Amber):** Übergangsbereich / Warnung (z. B. Teillast, Spannung < 12.4 V).
   - **Alarm-Rot:** Kritischer Grenzwert (Überhitzung > 105 °C, Unterspannung < 11.5 V, Fehler).
3. **Keine bunten Kachelrahmen:**
   - Alle Kacheln haben denselben dezenten dunkelgrauen Rahmen. Ein Rahmen wechselt **nur dann auf Rot**, wenn in dieser Kachel ein echter kritischer Fehler vorliegt.

---

## 2. Reduzierte Farbpalette (Farbpalette auf 5 Töne reduziert)

| Farbrolle | Hex-Wert | RGB565 | Verwendung |
|---|---|---|---|
| **Canvas Background** | `#000000` | `0x0000` (`TFT_BLACK`) | Gesamter Displayhintergrund |
| **Card Fill** | `#141414` | `0x10A2` | Kachel-Füllung (ruhiges, tiefes Anthrazit) |
| **Card Border & Grid** | `#2C2C2C` | `0x2965` (`TFT_DARKGREY`) | Inaktive Rahmen, Hilfsgitter, Trennlinien |
| **Primary Text & Gauges** | `#FFFFFF` | `0xFFFF` (`TFT_WHITE`) | Messwertziffern, Zeigernadeln, Titel |
| **Secondary Text / Units** | `#888888` | `0x8410` (`TFT_LIGHTGREY`)| Einheiten, Gebernamen, Fußzeilen-Rohwerte |
| **Accent Green (OK)** | `#00E676` | `0x072E` | Aktiver Leerlauf, $\lambda = 1.0$ im Toleranzfenster, Verbunden |
| **Accent Blue (Schub/Kalt)**| `#2979FF`| `0x2BCF` | Schubabschaltung, Warmlaufphase Kühlmittel |
| **Accent Amber (Achtung)** | `#FFB300` | `0xFD60` | Teillast / Beschleunigung, $\lambda$ magert/fettet an |
| **Accent Red (Kritisch)** | `#FF1744` | `0xF8A4` (`TFT_RED`) | Überhitzung, Unterspannung, Verbindungsfehler |

---

## 3. Detailkonzept für Tab 1 (WERTE: 8 Kacheln)

Alle Kacheln nutzen denselben neutralen Hintergrund (`#141414`) und denselben $1\,\text{px}$ Rahmen (`#2C2C2C`).

### 1. Motorstatus (oben links)
- Matrix in neutralem Grau.
- Nur der **eine aktive Zustand** wird invertiert hinterlegt:
  - `STOPP`: Neutral Grau / Weiß
  - `LEERLAUF`: Dezent Grün hinterlegt
  - `TEILLAST` / `VOLLLAST`: Dezent Amber hinterlegt
  - `SCHUB`: Dezent Blau hinterlegt

### 2. Drehzahl (Rundinstrument)
- Zifferblatt & Skala: Weiß und neutral Grau.
- Nadel & Ziffern: Reinweiß.
- Roter Skalenbereich: Erst ab $5500\,\text{RPM}$ (Drehzahlgrenze).

### 3. Batteriespannung (Rundinstrument)
- Zifferblatt & Skala: Neutral Grau.
- Nadel & Ziffern: Reinweiß.
- Nur die Ziffer wechselt auf **Rot**, wenn $U < 11.5\,\text{V}$ oder $U > 15.2\,\text{V}$.

### 4. Drosselklappe G69
- Ansaugrohr und Klappe: Weiß / Grau auf Schwarz.
- Gradwert & Rohwert: Reinweiß.
- Keine bunten Zierkreise.

### 5. Kühlmitteltemperatur & 6. Ansaugluft (IAT)
- Neutrales Thermometer (Schlanke Röhre).
- Füllung:
  - Kühlmittel $< 70\,^\circ\text{C}$: Blau (Warmlauf)
  - Kühlmittel $70\text{–}100\,^\circ\text{C}$: Neutral Weiß / Grün
  - Kühlmittel $> 105\,^\circ\text{C}$: Rot (Überhitzung)
- Ziffern: Reinweiß mit dezentem `°C`.

### 7. Lambda (O2S)
- Großer Wert in Weiß (z. B. `1.008`).
- Horizontaler Balken mit Mittenlinie (128).
- Zeigerpunkt: Nur bei starker Abweichung farbig (Grün bei $0.95\text{–}1.05$, sonst Amber).

### 8. Einspritzzeit / Last ($t_i$)
- Große Zahl in Reinweiß (`4 raw`).
- Schlanker Lastbalken in einfarbig Weiß/Grau.
- Text darunter neutral: `LEERLAUFLAST`, `TEILLAST`, `SCHUB / AUS`.

---

## 4. Detailkonzept für Tab 3 (MESSSCHRIEB)

Um Verwirrung durch zu viele bunte Linien zu vermeiden:

1. **Hintergrund:** Tiefschwarz mit dezentem $1\,\text{px}$ Punktraster / Hilfslinien in Dunkelgrau.
2. **Hauptkanal (Fokus):**
   - **Drehzahl (RPM):** Solide weiße Linie ($2\,\text{px}$) mit höchstem Kontrast.
3. **Sekundärkanäle (subtil unterschieden):**
   - **Drosselklappe (G69):** Cyan-Linie ($1.5\,\text{px}$).
   - **Lambda:** Gelbe Linie ($1\,\text{px}$).
   - **Last ($t_i$):** Magenta-Linie ($1\,\text{px}$).
4. **Schub- und Leerlaufzonen:**
   - Nur als sehr dezente, halbtransparente dunkle Streifen im Hintergrund (Dunkelblau für Schub, Dunkelgrün für Leerlauf), sodass die Signal-Linien immer im Vordergrund dominieren.

---

## 5. Tab- und Statusleiste (Oben)

- **Tabs 1, 2, 3:**
  - Inaktiv: Dunkelgrau mit weißem Text.
  - Aktiv: Dunkles Anthrazit mit dezentem weißem Unterstrich ($2\,\text{px}$).
- **Statusleiste (5 Stufen):**
  - Durchlaufende neutrale Punkte/Kästchen.
  - Nur die aktive Stufe leuchtet weiß; bei Fehler (`FEHLER`) leuchtet die Leiste rot.
