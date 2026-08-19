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
// Wähle hier bequem den Betriebsmodus:
//   MODE_SIMULATION: Läuft ohne Hardware mit simulierter ECU (Desktop/Trockentest)
//   MODE_HARDWARE_M2_TEST: Hardware-Rohdatentest (Sendet Testbytes 55 01 00)
//   MODE_HARDWARE_ECU_INIT: M3-Initialisierung am Fahrzeug (5-Baud-Init -> 9600 Baud)
// -----------------------------------------------------------------------------
#define MODE_SIMULATION        1
#define MODE_HARDWARE_M2_TEST  2
#define MODE_HARDWARE_ECU_INIT 3

// >> HIER DEN AKTIVEN MODUS EINSTELLEN (oder per Compiler-Flag überschreiben):
#ifndef APP_MODE
  #define APP_MODE MODE_SIMULATION
#endif

// Abwärtskompatibilität für Compiler-Flags
#if (APP_MODE == MODE_SIMULATION)
  #define SIMULATION_MODE true
  #define ECU_INIT_TEST   false
#elif (APP_MODE == MODE_HARDWARE_M2_TEST)
  #define SIMULATION_MODE false
  #define ECU_INIT_TEST   false
#elif (APP_MODE == MODE_HARDWARE_ECU_INIT)
  #define SIMULATION_MODE false
  #define ECU_INIT_TEST   true
#else
  #error "Ungültiger APP_MODE ausgewählt! Bitte MODE_SIMULATION, MODE_HARDWARE_M2_TEST oder MODE_HARDWARE_ECU_INIT wählen."
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
#if (APP_MODE == MODE_SIMULATION)
  console.println("Modus: SIMULATION (Trockentest ohne HW)");
  dashboard.setMode("SIMULATION (Replay-Daten)");
#elif (APP_MODE == MODE_HARDWARE_M2_TEST)
  console.println("Modus: HARDWARE M2 (Rohdaten-Verbindungstest)");
  dashboard.setMode("HARDWARE M2 (AutoDia K409, Rohdatentest)");
#elif (APP_MODE == MODE_HARDWARE_ECU_INIT)
  console.println("Modus: HARDWARE ECU INIT (M3 Fahrzeugtest 5-Baud)");
  dashboard.setMode("HARDWARE (AutoDia K409, KWP1281)");
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
