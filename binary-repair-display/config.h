// =====================================================================
//  config.h  --  Einstellungen: Defaults, Struktur, Speichern im Flash
// =====================================================================
//
//  Alles was zur Laufzeit aenderbar ist, steht in der Struktur "Config"
//  und wird im NVS-Flash des ESP32 gespeichert (ueberlebt Stromausfall).
//
//  Alles was der Compiler wissen muss (Pins, Puffergroesse), steht hier
//  als #define und laesst sich nur durch Neuflashen aendern.
//
#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ---------------------------------------------------------------------
//  Hardware -- muss beim Kompilieren feststehen
// ---------------------------------------------------------------------

// Datenleitung zum ERSTEN LED-Streifen. Alle weiteren Streifen werden
// einfach hinten angehaengt (DOUT -> DIN), nicht an eigene Pins.
#define LED_PIN        5

// Taster. GPIO 0 ist auf praktisch jedem ESP32-DevKit der BOOT-Taster,
// samt Pullup -- es muss also nichts angeloetet werden. Gedrueckt = LOW.
#define BUTTON_PIN     0

// Groesse des LED-Puffers. Bestimmt, wie viele LEDs man im Webinterface
// maximal einstellen kann. 64 = bis zu 8 Streifen a 8 LEDs.
#define MAX_LEDS       64

// Voreinstellung fuer die Anzahl LEDs: 2 Streifen a 8 LEDs = 16 Bit.
// Im Webinterface ueberschreibbar (24 Bit = 3 Streifen usw.).
#define DEFAULT_LEDS   16

// So viele WLAN-Zugaenge werden gespeichert und der Reihe nach probiert.
// Praktisch, wenn das Display zwischen Werkstatt und Veranstaltung wandert.
#define WIFI_SLOTS     3

// Name und Passwort des Konfigurations-WLANs (Captive Portal)
#define AP_SSID        "Repair-Display"
#define AP_PASS        "reparieren"   // mind. 8 Zeichen, "" = offenes Netz

// ---------------------------------------------------------------------
//  Auswahllisten fuer das Webinterface
// ---------------------------------------------------------------------

// Welcher Wert aus /api/stats angezeigt wird
const char* const METRIC_KEYS[]  = { "total", "attempted", "today", "pending" };
const char* const METRIC_NAMES[] = { "Erfolgreiche Reparaturen (total)",
                                     "Versuche gesamt (attempted)",
                                     "Heute (today)",
                                     "Offen / unbestaetigt (pending)" };
const uint8_t METRIC_COUNT = 4;

// Wie die Zahl auf die LEDs kommt
enum DisplayMode {
  MODE_BINARY = 0,    // jede LED ein Bit -- die eigentliche Idee
  MODE_BAR            // Balken: wie viel Prozent des Ziels sind erreicht
};
const char* const MODE_NAMES[] = { "Binaerzahl",
                                   "Fortschrittsbalken zum Ziel" };
const uint8_t MODE_COUNT = 2;

// Effekte. Sie gelten immer NUR fuer die eingeschalteten LEDs --
// die Nullen bleiben aussen vor und zaehlen bei Verlaeufen nicht mit.
enum Effect {
  EFFECT_SOLID = 0,   // eine feste Farbe
  EFFECT_RAINBOW,     // Farbverlauf ueber die aktiven LEDs, wandert
  EFFECT_UPDATE,      // eine Farbe, die bei jeder neuen Reparatur weiterspringt
  EFFECT_BREATHE,     // eine Farbe, die langsam heller und dunkler wird
  EFFECT_PARTY        // jede aktive LED eine eigene Farbe, harte Spruenge
};
const char* const EFFECT_NAMES[] = { "Solid - eine feste Farbe",
                                     "Rainbow - laufender Verlauf",
                                     "Neue Farbe bei jedem Update",
                                     "Atmen / Pulsieren",
                                     "Party - jede LED eine andere Farbe" };
const uint8_t EFFECT_COUNT = 5;

// LED-Chips. FastLED braucht den Typ zur Compile-Zeit, deshalb sind alle
// Varianten einkompiliert und es wird beim Start nur eine ausgewaehlt.
const char* const CHIPSET_NAMES[] = { "WS2812B", "WS2811", "WS2813", "WS2815", "SK6812" };
const uint8_t CHIPSET_COUNT = 5;

// Reihenfolge der Farbkanaele -- unterscheidet sich je nach Streifen.
// Wenn Rot und Gruen vertauscht sind: hier umstellen.
const char* const ORDER_NAMES[] = { "GRB", "RGB", "BRG" };
const uint8_t ORDER_COUNT = 3;

// ---------------------------------------------------------------------
//  Die eigentlichen Einstellungen
// ---------------------------------------------------------------------
struct Config {
  char     ssid[WIFI_SLOTS][33] = { "", "", "" };
  char     pass[WIFI_SLOTS][65] = { "", "", "" };

  char     apiUrl[128]   = "https://reparatur.fab-bergisch.org/api/stats";
  char     campUrl[128]  = "https://reparatur.fab-bergisch.org/api/campaign";

  uint8_t  numLeds       = DEFAULT_LEDS;
  uint8_t  brightness    = 64;        // 0-255, 64 ist schon recht hell
  uint16_t maxMilliamps  = 2000;      // Strombudget der LEDs, 0 = keine Begrenzung

  uint8_t  metric        = 0;         // Index in METRIC_KEYS
  uint8_t  displayMode   = MODE_BINARY;
  uint8_t  chipset       = 0;         // Index in CHIPSET_NAMES
  uint8_t  colorOrder    = 0;         // Index in ORDER_NAMES

  uint8_t  effect        = EFFECT_SOLID;
  uint8_t  effectSpeed   = 5;         // 1 (gemaechlich) bis 10 (hektisch)

  bool     msbFirst      = false;     // false = LED 0 ist Bit 0 (niedrigstes)
  bool     flashOnChange = true;      // kurz aufblitzen, wenn der Wert steigt
  bool     startupAnim   = true;      // Lauflicht beim Einschalten
  bool     offlineMode   = false;     // API ignorieren, nur den gespeicherten Wert zeigen
  bool     goalAnim      = true;      // Feier-Animation, wenn das Ziel geknackt ist
  bool     useCampaign   = true;      // /api/campaign abfragen und darauf reagieren

  uint16_t pollSeconds   = 60;        // Abfrageintervall der API

  uint32_t colorOn       = 0x00FF66;  // Farbe der eingeschalteten LEDs
  uint32_t colorOff      = 0x000000;  // Bit = 0, schwarz = wirkt wie ein Objekt
  uint32_t colorGoal     = 0xFF8000;  // Glimmen der Nullen, wenn das Ziel steht
};

Config cfg;                 // die aktuell gueltigen Einstellungen
Preferences prefs;          // Zugriff auf den Flash-Speicher

// ---------------------------------------------------------------------
//  Laden / Speichern der Einstellungen
// ---------------------------------------------------------------------
void configLoad() {
  char key[10];
  prefs.begin("bindisp", true);            // true = nur lesen

  for (uint8_t i = 0; i < WIFI_SLOTS; i++) {
    snprintf(key, sizeof(key), "ssid%u", (unsigned)i);
    prefs.getString(key, cfg.ssid[i], sizeof(cfg.ssid[i]));
    snprintf(key, sizeof(key), "pass%u", (unsigned)i);
    prefs.getString(key, cfg.pass[i], sizeof(cfg.pass[i]));
  }
  prefs.getString("apiurl",  cfg.apiUrl,  sizeof(cfg.apiUrl));
  prefs.getString("campurl", cfg.campUrl, sizeof(cfg.campUrl));

  cfg.numLeds       = prefs.getUChar ("numleds",  cfg.numLeds);
  cfg.brightness    = prefs.getUChar ("bright",   cfg.brightness);
  cfg.maxMilliamps  = prefs.getUShort("maxma",    cfg.maxMilliamps);
  cfg.metric        = prefs.getUChar ("metric",   cfg.metric);
  cfg.displayMode   = prefs.getUChar ("dispmode", cfg.displayMode);
  cfg.chipset       = prefs.getUChar ("chipset",  cfg.chipset);
  cfg.colorOrder    = prefs.getUChar ("order",    cfg.colorOrder);
  cfg.effect        = prefs.getUChar ("effect",   cfg.effect);
  cfg.effectSpeed   = prefs.getUChar ("espeed",   cfg.effectSpeed);
  cfg.msbFirst      = prefs.getBool  ("msbfirst", cfg.msbFirst);
  cfg.flashOnChange = prefs.getBool  ("flash",    cfg.flashOnChange);
  cfg.startupAnim   = prefs.getBool  ("startanim",cfg.startupAnim);
  cfg.offlineMode   = prefs.getBool  ("offline",  cfg.offlineMode);
  cfg.goalAnim      = prefs.getBool  ("goalanim", cfg.goalAnim);
  cfg.useCampaign   = prefs.getBool  ("usecamp",  cfg.useCampaign);
  cfg.pollSeconds   = prefs.getUShort("poll",     cfg.pollSeconds);
  cfg.colorOn       = prefs.getUInt  ("con",      cfg.colorOn);
  cfg.colorOff      = prefs.getUInt  ("coff",     cfg.colorOff);
  cfg.colorGoal     = prefs.getUInt  ("cgoal",    cfg.colorGoal);
  prefs.end();

  // Notbremse gegen unsinnige gespeicherte Werte
  if (cfg.numLeds < 1 || cfg.numLeds > MAX_LEDS)      cfg.numLeds = DEFAULT_LEDS;
  if (cfg.metric >= METRIC_COUNT)                     cfg.metric = 0;
  if (cfg.displayMode >= MODE_COUNT)                  cfg.displayMode = MODE_BINARY;
  if (cfg.chipset >= CHIPSET_COUNT)                   cfg.chipset = 0;
  if (cfg.colorOrder >= ORDER_COUNT)                  cfg.colorOrder = 0;
  if (cfg.effect >= EFFECT_COUNT)                     cfg.effect = EFFECT_SOLID;
  if (cfg.effectSpeed < 1 || cfg.effectSpeed > 10)    cfg.effectSpeed = 5;
  if (cfg.pollSeconds < 10)                           cfg.pollSeconds = 10;
}

void configSave() {
  char key[10];
  prefs.begin("bindisp", false);           // false = schreiben erlaubt

  for (uint8_t i = 0; i < WIFI_SLOTS; i++) {
    snprintf(key, sizeof(key), "ssid%u", (unsigned)i);
    prefs.putString(key, cfg.ssid[i]);
    snprintf(key, sizeof(key), "pass%u", (unsigned)i);
    prefs.putString(key, cfg.pass[i]);
  }
  prefs.putString("apiurl",  cfg.apiUrl);
  prefs.putString("campurl", cfg.campUrl);

  prefs.putUChar ("numleds",  cfg.numLeds);
  prefs.putUChar ("bright",   cfg.brightness);
  prefs.putUShort("maxma",    cfg.maxMilliamps);
  prefs.putUChar ("metric",   cfg.metric);
  prefs.putUChar ("dispmode", cfg.displayMode);
  prefs.putUChar ("chipset",  cfg.chipset);
  prefs.putUChar ("order",    cfg.colorOrder);
  prefs.putUChar ("effect",   cfg.effect);
  prefs.putUChar ("espeed",   cfg.effectSpeed);
  prefs.putBool  ("msbfirst", cfg.msbFirst);
  prefs.putBool  ("flash",    cfg.flashOnChange);
  prefs.putBool  ("startanim",cfg.startupAnim);
  prefs.putBool  ("offline",  cfg.offlineMode);
  prefs.putBool  ("goalanim", cfg.goalAnim);
  prefs.putBool  ("usecamp",  cfg.useCampaign);
  prefs.putUShort("poll",     cfg.pollSeconds);
  prefs.putUInt  ("con",      cfg.colorOn);
  prefs.putUInt  ("coff",     cfg.colorOff);
  prefs.putUInt  ("cgoal",    cfg.colorGoal);
  prefs.end();
}

// ---------------------------------------------------------------------
//  Der angezeigte Wert selbst
// ---------------------------------------------------------------------
//  Wird getrennt von den Einstellungen gespeichert, damit nach einem
//  Stromausfall sofort wieder der letzte Stand dasteht statt einer 0.
//  Derselbe Speicherplatz dient auch fuer den von Hand eingetippten
//  Wert -- ein Wert ist ein Wert, egal woher er kommt.
//
uint32_t valueLoad() {
  prefs.begin("bindisp", true);
  uint32_t v = prefs.getUInt("lastval", 0);
  prefs.end();
  return v;
}

void valueStore(uint32_t v) {
  // Nur schreiben, wenn sich wirklich etwas geaendert hat. Sonst wuerde
  // der Flash bei jeder Abfrage beschrieben, also alle 60 Sekunden.
  if (valueLoad() == v) return;
  prefs.begin("bindisp", false);
  prefs.putUInt("lastval", v);
  prefs.end();
}

// Merker, ob die Ziel-Feier schon gelaufen ist. Sonst wuerde sie nach
// jedem Neustart wieder losgehen, obwohl das Ziel laengst steht.
bool goalFlagLoad() {
  prefs.begin("bindisp", true);
  bool b = prefs.getBool("goaldone", false);
  prefs.end();
  return b;
}

void goalFlagStore(bool b) {
  if (goalFlagLoad() == b) return;
  prefs.begin("bindisp", false);
  prefs.putBool("goaldone", b);
  prefs.end();
}
