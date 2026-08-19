#include <M5Unified.h>

#include "Console.h"
#include "ConnectivityTester.h"
#include "Dashboard.h"
#include "EcuInitTester.h"
#include "SerialLink.h"
#include "SimulatedLink.h"
#include "UsbCdcLink.h"

// =============================================================================
// KONFIGURATION & BETRIEBSMODUS
// =============================================================================
// Wähle hier den Betriebsmodus:
//   MODE_SIMULATION_REPLAY   (1): Autonome Simulation mit Replay-Daten (Trockentest ohne Hardware)
//   MODE_RAW_LOOPBACK_TEST   (2): Hardware-Rohdaten-/Verbindungstest (Sendet Testbytes über USB/K-Line)
//   MODE_KWP1281_LIVE_DIAG   (3): Echte Fahrzeugdiagnose via KWP1281 (5-Baud-Init, Handshake, Messwerte)
// -----------------------------------------------------------------------------
#define MODE_SIMULATION_REPLAY 1
#define MODE_RAW_LOOPBACK_TEST 2
#define MODE_KWP1281_LIVE_DIAG 3

// Aliase für Abwärtskompatibilität bestehender Compiler-Flags / Skripte
#define MODE_SIMULATION        MODE_SIMULATION_REPLAY
#define MODE_HARDWARE_M2_TEST  MODE_RAW_LOOPBACK_TEST
#define MODE_HARDWARE_ECU_INIT MODE_KWP1281_LIVE_DIAG

// >> HIER DEN AKTIVEN MODUS EINSTELLEN (oder per Compiler-Flag überschreiben):
#ifndef APP_MODE
  #define APP_MODE MODE_KWP1281_LIVE_DIAG
#endif

// Abwärtskompatibilität für Compiler-Flags
#if (APP_MODE == MODE_SIMULATION_REPLAY)
  #define SIMULATION_MODE true
  #define ECU_INIT_TEST   false
#elif (APP_MODE == MODE_RAW_LOOPBACK_TEST)
  #define SIMULATION_MODE false
  #define ECU_INIT_TEST   false
#elif (APP_MODE == MODE_KWP1281_LIVE_DIAG)
  #define SIMULATION_MODE false
  #define ECU_INIT_TEST   true
#else
  #error "Ungültiger APP_MODE ausgewählt! Bitte MODE_SIMULATION_REPLAY (1), MODE_RAW_LOOPBACK_TEST (2) oder MODE_KWP1281_LIVE_DIAG (3) wählen."
#endif

#if SIMULATION_MODE
  SimulatedLink serialLink;
#else
  UsbCdcLink serialLink;
#endif

#if ECU_INIT_TEST
  // EcuInitTester arbeitet direkt auf UsbCdcLink, nicht auf SerialLink&,
  // weil er Baudratenwechsel und Signalsteuerung (5-Baud-Init) durchführt.
  EcuInitTester tester(serialLink);
#else
  ConnectivityTester tester(serialLink);
#endif

namespace {
constexpr uint32_t kSerialBaud     = 115200;
constexpr uint8_t  kConsoleTextSize = 2;
// War vorher 50ms - viel zu grob fuer die KWP1281 ~KB2-Antwort, die
// innerhalb von ca. 30-40ms nach dem Keybyte gesendet werden muss:
// tester.update() wurde dadurch nur alle 50ms+ ueberhaupt aufgerufen, was
// allein schon das Zeitfenster sprengen konnte (unabhaengig vom Console-
// Redraw-Fix). 2ms erlaubt eine viel feinere Pollingrate; das teure
// Display-Redraw ist inzwischen ueber Console::update() entkoppelt/
// gedrosselt und blockiert diese schnellere Schleife nicht mehr.
constexpr uint32_t kLoopDelayMs    = 2;
constexpr uint32_t kSetupBootMs    = 1000;
constexpr uint32_t kUsbWaitMsgMs  = 1000;
constexpr const char* kBannerSep  =
    "========================================";

// Wird in setup() auf false gesetzt, falls die Link-Initialisierung
// fehlschlaegt. loop() prueft das Flag und laesst in dem Fall alle
// periodischen Aktionen aus, statt mit uninitialisiertem Link weiter
// zu laufen.
bool g_linkReady = false;
}  // namespace

static void printBanner() {
  console.println(kBannerSep);
  console.println("M5Tab5 KWP1281 Logger / Tester");
#if (APP_MODE == MODE_SIMULATION_REPLAY)
  console.println("Modus: SIMULATION (Replay-Daten ohne Hardware)");
  dashboard.setMode("SIMULATION (Replay)");
#elif (APP_MODE == MODE_RAW_LOOPBACK_TEST)
  console.println("Modus: ROHDATEN-TEST (AutoDia K409 Loopback)");
  dashboard.setMode("ROHDATEN-TEST (K409)");
#elif (APP_MODE == MODE_KWP1281_LIVE_DIAG)
  console.println("Modus: KWP1281 LIVE (Fahrzeugdiagnose 1200 Baud)");
  dashboard.setMode("KWP1281 LIVE (AutoDia K409)");
#endif
  console.println(kBannerSep);
}

static void printWaitMessageThrottled(uint32_t nowMs) {
  static uint32_t lastWaitMsg = 0;
  // Differenzvergleich statt ">"-Vergleich mit absolutem Zeitstempel -
  // robust gegen den millis()-Ueberlauf (ca. alle 49 Tage).
  if (nowMs - lastWaitMsg >= kUsbWaitMsgMs) {
    lastWaitMsg = nowMs;
    console.println("[USB] host ready, waiting for USB serial device...");
  }
}

void setup() {
  Serial.begin(kSerialBaud);
  delay(kSetupBootMs);

  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.internal_imu  = false;
  M5.begin(cfg);

  // Ab hier laufen alle Ausgaben ueber console.*, nicht mehr direkt ueber
  // Serial oder M5.Display - console.begin() richtet Textgroesse, Farben
  // und die Zeilenzahl passend zur tatsaechlichen Displaygroesse ein.
  console.begin(kConsoleTextSize);
  console.setDisplayEnabled(false);
  dashboard.begin();

  printBanner();

  if (!serialLink.begin(kSerialBaud)) {
    console.println("[LINK] begin() failed");
    dashboard.setStage(ConnStage::FEHLER, "Fehler: Link-Initialisierung fehlgeschlagen");
    g_linkReady = false;
    return;
  }
  console.println("[LINK] ready");
#if !SIMULATION_MODE && !ECU_INIT_TEST
  dashboard.setStage(ConnStage::BEREIT, "Verbindungstest aktiv");
#endif
  g_linkReady = true;
}

void loop() {
  M5.update();

  // Wenn die Link-Initialisierung in setup() gescheitert ist, nichts mehr
  // tun - ein uninitialisiertes SerialLink duerfen wir nicht ansteuern.
  if (!g_linkReady) {
    console.update();
    delay(kLoopDelayMs);
    return;
  }

  const uint32_t now = millis();

#if SIMULATION_MODE
  // Liefert eine faellige simulierte Antwort aus, falls die simulierte
  // Antwortzeit seit dem letzten write() abgelaufen ist. Reagiert somit
  // auf Anfragen statt autonom zu senden.
  serialLink.update();
#else
  // Die "waiting for device"-Meldung erscheint nur, solange noch keine
  // Verbindung steht - ohne blockierendes delay() in jeder Iteration,
  // damit das Timeout im ConnectivityTester nicht verfaelscht wird.
  if (!serialLink.isConnected() && serialLink.hostReady()) {
    printWaitMessageThrottled(now);
  }
#endif

  tester.update();

  // Display-Redraw bewusst NACH tester.update() und gedrosselt (siehe
  // Console::update()), damit das teure SPI-Fullscreen-Redraw niemals die
  // zeitkritische KWP1281-Handshake-Verarbeitung verzoegert.
  console.update();
  dashboard.update();
  delay(kLoopDelayMs);
}
