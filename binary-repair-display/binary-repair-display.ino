// =====================================================================
//  Binaeranzeige fuer den Reparaturrekord NRW
// =====================================================================
//
//  Holt die aktuelle Anzahl Reparaturen von
//  https://reparatur.fab-bergisch.org/api-doku
//  und zeigt sie binaer auf WS28xx-LED-Streifen an.
//
//  Jede LED = ein Bit. Zwei Streifen a 8 LEDs = 16 Bit (bis 65535),
//  drei Streifen = 24 Bit. Die Anzahl steht in der Konfiguration,
//  weil LED-Streifen ihre Laenge nicht selbst melden koennen.
//
//  Beim ersten Start (oder wenn kein WLAN erreichbar ist, oder nach
//  langem Druck auf den BOOT-Taster) macht der ESP32 ein eigenes WLAN
//  "Repair-Display" auf, in dem sich die Konfigurationsseite von selbst
//  oeffnet.
//
//  Benoetigte Bibliotheken (Bibliotheksverwalter der Arduino IDE):
//    - FastLED      (Daniel Garcia)
//    - ArduinoJson  (Benoit Blanchon), Version 7
//  Board: "ESP32 Dev Module" aus dem esp32-Paket von Espressif.
//
//  Verkabelung:
//    ESP32 GPIO 5  --[330 Ohm]--  DIN des ersten Streifens
//    ESP32 GND     ------------   GND (gemeinsame Masse, wichtig!)
//    5 V Netzteil  ------------   5 V der Streifen
//    ausserdem ~1000 uF Elko zwischen 5 V und GND direkt am Streifen.
//    Der ESP32 kann die LEDs NICHT mitversorgen -- 16 weisse LEDs
//    ziehen schon fast ein Ampere.
//
#include "config.h"
#include "display.h"
#include "api.h"
#include "portal.h"
#include "button.h"
#include <ESPmDNS.h>

// Der Kampagnenstatus aendert sich hoechstens zweimal im Jahr --
// oefter als alle fuenf Minuten muss man da nicht nachfragen.
#define CAMPAIGN_POLL_MS  300000UL

uint32_t lastPollMs   = 0;    // wann zuletzt die Zahl geholt wurde
uint32_t lastCampMs   = 0;    // wann zuletzt der Kampagnenstatus geholt wurde
uint32_t lastRenderMs = 0;    // wann zuletzt die LEDs aktualisiert wurden
uint32_t lastRetryMs  = 0;    // wann zuletzt ein Reconnect versucht wurde
uint32_t lostSinceMs  = 0;    // seit wann das WLAN weg ist (0 = verbunden)
uint32_t shownValue   = 0;    // zuletzt angezeigter Wert
uint8_t  wifiSlot     = 0;    // welcher der gespeicherten Zugaenge funktioniert hat
bool     wifiWasUp    = false;// fuer die Animation beim Verbinden
bool     goalCelebrated = false; // Feier-Animation schon gelaufen?

// ---------------------------------------------------------------------
//  WLAN verbinden
// ---------------------------------------------------------------------
//  Probiert die gespeicherten Zugaenge der Reihe nach durch. Waehrend
//  der Suche laeuft ein gelber Punkt durch die Kette.
//
bool wifiConnect(uint32_t timeoutPerNet) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // sonst ruckelt die Anzeige

  for (uint8_t i = 0; i < WIFI_SLOTS; i++) {
    if (strlen(cfg.ssid[i]) == 0) continue;

    Serial.printf("Verbinde mit '%s' ", cfg.ssid[i]);
    WiFi.begin(cfg.ssid[i], cfg.pass[i]);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutPerNet) {
      displayConnectingPattern();
      delay(30);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("-> ok");
      wifiSlot = i;
      return true;
    }
    Serial.println("-> nichts");
    WiFi.disconnect();
  }
  return false;
}

// ---------------------------------------------------------------------
//  Ziel erreicht?
// ---------------------------------------------------------------------
bool goalReached() {
  return apiGoal > 0 && apiValue >= apiGoal;
}

// ---------------------------------------------------------------------
//  Ein neuer Wert ist da
// ---------------------------------------------------------------------
void noteNewValue() {
  if (apiValue != shownValue) {
    updateHue += HUE_STEP_PER_UPDATE;   // Effekt "neue Farbe bei jedem Update"
    valueStore(apiValue);               // Stand ueber den naechsten Stromausfall retten
    if (cfg.flashOnChange && apiValue > shownValue) displayFlash();
    shownValue = apiValue;
  }

  // Ziel geknackt: einmal feiern, danach glimmen die Nullen dauerhaft
  // in der Zielfarbe mit (siehe offColor in display.h).
  if (goalReached() && !goalCelebrated) {
    goalCelebrated = true;
    goalFlagStore(true);
    Serial.println("Ziel erreicht!");
    if (cfg.goalAnim) displayGoalCelebration();
  }
  // Ziel doch nicht (mehr) erreicht, z. B. weil ein anderer Wert
  // angezeigt wird -- dann darf spaeter nochmal gefeiert werden.
  else if (!goalReached() && goalCelebrated) {
    goalCelebrated = false;
    goalFlagStore(false);
  }
}

// ---------------------------------------------------------------------
//  LEDs neu zeichnen
// ---------------------------------------------------------------------
void render() {
  // Kampagne hat noch nicht begonnen: ein ruhiges Wartemuster statt
  // einer nichtssagenden 0. Im Portal-Modus hat der Hinweis auf der
  // ersten LED Vorrang, dann zeigen wir wie gewohnt die Zahl.
  if (cfg.useCampaign && apiCampaign == CAMPAIGN_BEFORE
      && !portalApMode && !testActive()) {
    FastLED.setBrightness(cfg.brightness);
    displayWaitingPattern();
    return;
  }

  // Testanzeige aus dem Webinterface hat Vorrang
  uint32_t value = testActive() ? testValue : apiValue;

  uint8_t bright = cfg.brightness;

  // Kampagne beendet: der Endstand bleibt stehen und atmet ganz langsam.
  if (cfg.useCampaign && apiCampaign == CAMPAIGN_AFTER)
    bright = scale8(bright, beatsin8(6, 90, 255));

  // Wenn die API gerade nicht antwortet: letzten Wert stehen lassen,
  // aber alle 3 Sekunden kurz dunkel schalten als Hinweis.
  if (apiError.length() > 0 && (millis() % 3000) < 120)
    bright = 0;

  FastLED.setBrightness(bright);

  // Steht das Konfigurations-WLAN offen, pulsiert die erste LED als
  // Hinweis darauf. Die Zahl bleibt dabei die ganze Zeit ablesbar.
  displayFrame(value, apiGoal, goalReached(), portalApMode);
}

// ---------------------------------------------------------------------
//  Setup
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Binaeranzeige Reparaturrekord NRW ===");

  configLoad();
  displayBegin();
  buttonBegin();

  // Letzten Stand aus dem Flash holen. Nach einem Stromausfall steht
  // damit sofort wieder die richtige Zahl da statt einer 0. Derselbe
  // Speicherplatz haelt auch einen von Hand eingetippten Wert.
  apiValue       = valueLoad();
  shownValue     = apiValue;
  updateHue      = (uint8_t)(apiValue * HUE_STEP_PER_UPDATE);
  goalCelebrated = goalFlagLoad();

  Serial.printf("LEDs: %u  Chip: %s %s  Pin: %u\n",
                cfg.numLeds, CHIPSET_NAMES[cfg.chipset],
                ORDER_NAMES[cfg.colorOrder], LED_PIN);
  Serial.printf("Anzeigeart: %s  Effekt: %s\n",
                MODE_NAMES[cfg.displayMode], EFFECT_NAMES[cfg.effect]);
  Serial.printf("Gespeicherter Wert: %u\n", apiValue);

  if (cfg.startupAnim) displayStartupSweep();

  if (cfg.offlineMode)
    Serial.println("Offline-Modus: die API wird nicht abgefragt.");

  if (wifiConnect(12000)) {
    displayConnectedSweep();
    wifiWasUp = true;

    Serial.print("IP-Adresse: http://");
    Serial.println(WiFi.localIP());
    if (MDNS.begin("repair-display"))
      Serial.println("auch erreichbar unter http://repair-display.local/");
    portalRoutes();

    if (!cfg.offlineMode) {
      if (cfg.useCampaign && apiFetchCampaign())
        Serial.printf("Kampagne: %s\n", CAMPAIGN_NAMES[apiCampaign]);
      if (apiFetch()) noteNewValue();
    }
    lastPollMs = millis();
    lastCampMs = millis();
  } else {
    portalStartAp();            // Konfigurationsportal aufmachen
  }
}

// ---------------------------------------------------------------------
//  Hauptschleife
// ---------------------------------------------------------------------
void loop() {
  portalLoop();                 // Webinterface bedienen
  buttonLoop();                 // Taster abfragen

  if (!portalApMode && !cfg.offlineMode) {

    // WLAN weggebrochen? alle 30 s neu versuchen
    if (WiFi.status() != WL_CONNECTED) {
      apiError  = "WLAN verloren";
      wifiWasUp = false;
      if (lostSinceMs == 0) lostSinceMs = millis();

      if (millis() - lastRetryMs > 30000) {
        lastRetryMs = millis();
        WiFi.disconnect();
        WiFi.begin(cfg.ssid[wifiSlot], cfg.pass[wifiSlot]);
      }
      // Nach 5 Minuten ohne WLAN neu starten. Dabei werden wieder alle
      // gespeicherten Netze durchprobiert, und falls keines geht, geht
      // das Konfigurationsportal auf -- sonst kaeme man an ein Display
      // mit falschem WLAN-Passwort nie wieder heran.
      if (millis() - lostSinceMs > 300000) {
        Serial.println("5 Minuten ohne WLAN -- Neustart");
        ESP.restart();
      }
    }

    // WLAN da
    else {
      lostSinceMs = 0;

      // Gerade erst wieder verbunden? Einmal gruen durchwischen.
      if (!wifiWasUp) {
        wifiWasUp = true;
        Serial.println("WLAN wieder da");
        displayConnectedSweep();
        lastPollMs = 0;                 // gleich die aktuelle Zahl holen
      }

      // Kampagnenstatus -- selten, aendert sich kaum
      if (cfg.useCampaign && millis() - lastCampMs > CAMPAIGN_POLL_MS) {
        lastCampMs = millis();
        if (apiFetchCampaign())
          Serial.printf("Kampagne: %s\n", CAMPAIGN_NAMES[apiCampaign]);
      }

      // die eigentliche Zahl
      if (millis() - lastPollMs > (uint32_t)cfg.pollSeconds * 1000UL) {
        lastPollMs = millis();
        if (apiFetch()) {
          Serial.printf("%s = %u\n", METRIC_KEYS[cfg.metric], apiValue);
          noteNewValue();
        } else {
          Serial.printf("Abfrage fehlgeschlagen: %s\n", apiError.c_str());
        }
      }
    }
  }

  // Anzeige mit rund 25 Bildern pro Sekunde auffrischen
  if (millis() - lastRenderMs > 40) {
    lastRenderMs = millis();
    render();
  }
}
