// =====================================================================
//  portal.h  --  Webinterface und Captive Portal
// =====================================================================
//
//  Zwei Betriebsarten:
//
//  1) Kein WLAN konfiguriert, nicht erreichbar, oder der Taster wurde
//     lang gedrueckt -> der ESP32 macht ein eigenes WLAN "Repair-Display"
//     auf. Ein DNS-Server beantwortet JEDE Anfrage mit der eigenen IP,
//     dadurch poppt auf Handy und Laptop die Seite automatisch auf.
//
//  2) WLAN verbunden -> dieselbe Seite ist unter der IP im Netz
//     erreichbar (steht beim Start auf dem seriellen Monitor).
//
//  Hinweis zum Stil: die Seite wird Stueck fuer Stueck mit  p += ...
//  zusammengebaut. Das sieht umstaendlich aus, ist aber Absicht --
//  Arduinos String-Klasse kann  "text" + String(x)  nicht.
//
#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config.h"
#include "api.h"

WebServer server(80);
DNSServer dnsServer;

bool     portalApMode  = false;   // true = eigenes WLAN offen
String   scanOptions   = "";      // gefundene Netze als <option>-Liste
uint32_t testValue     = 0;       // Testanzeige ueber /test?value=...
uint32_t testUntilMs   = 0;
bool     rebootPending = false;

// ---------------------------------------------------------------------
//  kleine Helfer
// ---------------------------------------------------------------------
static String colorToHex(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06X", (unsigned)(c & 0xFFFFFF));
  return String(buf);
}

static uint32_t hexToColor(const String& s, uint32_t fallback) {
  if (s.length() < 7 || s[0] != '#') return fallback;
  return (uint32_t) strtoul(s.c_str() + 1, nullptr, 16) & 0xFFFFFF;
}

// Zahl aus dem Formular lesen und in einen erlaubten Bereich zwingen.
// (Arduinos constrain() ist ein Makro und wuerde server.arg() dreimal
//  auswerten -- also dreimal einen String bauen.)
static long argInt(const char* name, long lo, long hi, long fallback) {
  if (!server.hasArg(name)) return fallback;
  long v = server.arg(name).toInt();
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// Auswahlfeld aus einer Namensliste bauen
static void addSelect(String& p, const char* name, const char* const* labels,
                      uint8_t count, uint8_t selected) {
  p += "<select name='";
  p += name;
  p += "'>";
  for (uint8_t i = 0; i < count; i++) {
    p += "<option value='";
    p += String(i);
    p += "'";
    if (i == selected) p += " selected";
    p += ">";
    p += labels[i];
    p += "</option>";
  }
  p += "</select>";
}

// Farbwaehler
static void addColor(String& p, const char* label, const char* name, uint32_t value) {
  p += "<label>";
  p += label;
  p += "<input type=color name=";
  p += name;
  p += " value='";
  p += colorToHex(value);
  p += "'></label>";
}

// Ankreuzfeld
static void addCheck(String& p, const char* label, const char* name, bool on) {
  p += "<label><input type=checkbox name=";
  p += name;
  p += " value=1";
  if (on) p += " checked";
  p += ">";
  p += label;
  p += "</label>";
}

// WLAN-Suche -- die Liste wird nur bei Bedarf aktualisiert, weil ein
// Scan ein paar Sekunden dauert und solange nichts anderes passiert.
void portalScan() {
  int n = WiFi.scanNetworks();
  scanOptions = "";
  for (int i = 0; i < n && i < 20; i++) {
    scanOptions += "<option value='";
    scanOptions += WiFi.SSID(i);
    scanOptions += "'>";
    scanOptions += WiFi.SSID(i);
    scanOptions += " (";
    scanOptions += String(WiFi.RSSI(i));
    scanOptions += " dBm)</option>";
  }
  WiFi.scanDelete();
}

// ---------------------------------------------------------------------
//  Konfigurationsseite
// ---------------------------------------------------------------------
static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html lang=de><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Binaeranzeige Reparaturrekord</title><style>
body{font-family:system-ui,sans-serif;max-width:40rem;margin:0 auto;padding:1rem;
     background:#111;color:#eee;line-height:1.5}
h1{font-size:1.3rem} h2{font-size:1rem;margin:1.5rem 0 .5rem;color:#0f6}
label{display:block;margin:.6rem 0}
input,select{width:100%;padding:.45rem;border-radius:.3rem;border:1px solid #444;
     background:#1c1c1c;color:#eee;font-size:1rem;box-sizing:border-box}
input[type=color]{height:2.6rem;padding:.2rem}
input[type=checkbox]{width:auto;margin-right:.5rem}
button{width:100%;padding:.8rem;margin-top:1.2rem;border:0;border-radius:.3rem;
     background:#0f6;color:#000;font-size:1rem;font-weight:600}
.status{background:#1c1c1c;border-radius:.4rem;padding:.7rem 1rem;font-size:.9rem}
.status b{color:#0f6} .err{color:#f66}
.row{display:flex;gap:.6rem} .row>label{flex:1}
small{color:#888;display:block;margin-top:.2rem}
a{color:#0af}
</style><h1>Binaeranzeige Reparaturrekord NRW</h1>)HTML";

void handleRoot() {
  String p = FPSTR(PAGE_HEAD);

  // ---- Statusblock ----
  p += "<div class=status>Modus: <b>";
  p += portalApMode ? "Konfigurations-WLAN" : "verbunden";
  p += "</b><br>";
  if (!portalApMode) {
    p += "Netz: <b>";
    p += WiFi.SSID();
    p += "</b> &middot; IP: <b>";
    p += WiFi.localIP().toString();
    p += "</b><br>";
  }
  p += "Kampagne: <b>";
  p += CAMPAIGN_NAMES[apiCampaign];
  p += "</b><br>Aktueller Wert: <b>";
  p += String(apiValue);
  p += "</b>";
  if (apiGoal) {
    p += " von ";
    p += String(apiGoal);
  }
  p += "<br>Binaer: <b>";
  p += String(apiValue, BIN);
  p += "</b> (";
  p += String(cfg.numLeds);
  p += " Bit verfuegbar)<br>";
  if (apiLastOkMs) {
    p += "Letzte Abfrage: vor ";
    p += String((millis() - apiLastOkMs) / 1000);
    p += " s<br>";
  }
  if (apiError.length()) {
    p += "<span class=err>Fehler: ";
    p += apiError;
    p += "</span>";
  }
  p += "</div><form method=POST action=/save>";

  // ---- WLAN ----
  p += "<h2>WLAN</h2><small>Bis zu ";
  p += String(WIFI_SLOTS);
  p += " Netze, sie werden der Reihe nach durchprobiert. Praktisch, wenn das "
       "Display zwischen Werkstatt und Veranstaltungsort wandert. Leeres "
       "Passwortfeld heisst: das gespeicherte bleibt stehen.</small>";
  p += "<datalist id=nets>";
  p += scanOptions;
  p += "</datalist>";

  for (uint8_t i = 0; i < WIFI_SLOTS; i++) {
    p += "<div class=row><label>Netz ";
    p += String(i + 1);
    p += "<input name=ssid";
    p += String(i);
    p += " list=nets value='";
    p += cfg.ssid[i];
    p += "'></label><label>Passwort<input name=pass";
    p += String(i);
    p += " type=password></label></div>";
  }
  p += "<small><a href=/scan>Netze neu suchen</a></small>";

  // ---- Anzeige ----
  p += "<h2>Anzeige</h2>";
  p += "<label>Anzahl LEDs<input name=leds type=number min=1 max=";
  p += String(MAX_LEDS);
  p += " value=";
  p += String(cfg.numLeds);
  p += "><small>Zwei Streifen a 8 LEDs = 16, drei Streifen = 24. LED-Streifen koennen "
       "ihre Anzahl nicht selbst melden, deshalb hier eintragen.</small></label>";

  p += "<label>Anzeigeart";
  addSelect(p, "dispmode", MODE_NAMES, MODE_COUNT, cfg.displayMode);
  p += "<small>Der Balken braucht ein Ziel aus der API und zeigt, wie viel Prozent "
       "davon erreicht sind -- ohne Vorkenntnisse lesbar.</small></label>";

  p += "<label>Angezeigter Wert";
  addSelect(p, "metric", METRIC_NAMES, METRIC_COUNT, cfg.metric);
  p += "</label>";

  p += "<label>Bit-Reihenfolge<select name=msb><option value=0";
  if (!cfg.msbFirst) p += " selected";
  p += ">Erste LED = Bit 0 (niedrigstes)</option><option value=1";
  if (cfg.msbFirst) p += " selected";
  p += ">Erste LED = hoechstes Bit</option></select>"
       "<small>Erste LED = die am Dateneingang des ersten Streifens. Dreht auch "
       "die Richtung des Balkens um.</small></label>";

  p += "<label>Helligkeit (1-255)<input name=bright type=number min=1 max=255 value=";
  p += String(cfg.brightness);
  p += "></label>";

  addCheck(p, "Kurz aufblitzen bei neuer Reparatur", "flash",     cfg.flashOnChange);
  addCheck(p, "Start-Animation (Lauflicht hin und zurueck)", "startanim", cfg.startupAnim);

  // ---- Farbe und Effekt ----
  p += "<h2>Farbe und Effekt</h2><div class=row>";
  addColor(p, "Farbe der Einsen", "con",   cfg.colorOn);
  addColor(p, "Farbe der Nullen", "coff",  cfg.colorOff);
  addColor(p, "Ziel erreicht",    "cgoal", cfg.colorGoal);
  p += "</div><small>Schwarz bei den Nullen laesst die Kette wie ein durchgehendes "
       "Objekt wirken. Die dritte Farbe glimmt in den Nullen, sobald das Ziel der "
       "Kampagne geknackt ist -- dann leuchtet die ganze Kette.</small>";

  p += "<label>Effekt";
  addSelect(p, "effect", EFFECT_NAMES, EFFECT_COUNT, cfg.effect);
  p += "<small>Gilt nur fuer die eingeschalteten LEDs. Verlaeufe verteilen sich ueber "
       "die Einsen, die Nullen dazwischen zaehlen nicht mit. Solid und Atmen benutzen "
       "die Farbe oben, die anderen drei ihre eigenen.</small></label>";
  p += "<label>Geschwindigkeit (1-10)<input name=espeed type=number min=1 max=10 value=";
  p += String(cfg.effectSpeed);
  p += "></label>";

  // ---- Streifentyp ----
  p += "<h2>LED-Streifen</h2><div class=row><label>Chip";
  addSelect(p, "chip", CHIPSET_NAMES, CHIPSET_COUNT, cfg.chipset);
  p += "</label><label>Farbreihenfolge";
  addSelect(p, "order", ORDER_NAMES, ORDER_COUNT, cfg.colorOrder);
  p += "</label></div><small>Datenpin ist fest auf GPIO ";
  p += String(LED_PIN);
  p += ", Taster auf GPIO ";
  p += String(BUTTON_PIN);
  p += " (beides in config.h). Wenn Rot und Gruen vertauscht sind: Farbreihenfolge "
       "aendern.</small>";

  // ---- Strom ----
  p += "<h2>Strom</h2>";
  p += "<label>Strombudget der LEDs in mA<input name=maxma type=number min=0 max=20000 value=";
  p += String(cfg.maxMilliamps);
  p += "><small>Wie viel das Netzteil fuer die LEDs uebrig hat. FastLED dimmt notfalls "
       "alles gleichmaessig herunter, damit der Wert nie ueberschritten wird -- sonst "
       "bricht die Spannung ein und der ESP32 startet mitten im Betrieb neu. "
       "0 schaltet die Begrenzung ab.</small></label>";

  // ---- Wert ----
  p += "<h2>Wert</h2>";
  p += "<label>Wert von Hand setzen<input name=manual type=number min=0 value=";
  p += String(apiValue);
  p += "><small>Wird gespeichert und sofort angezeigt. Sobald die API antwortet, "
       "wird er ueberschrieben -- ausser im Offline-Modus.</small></label>";
  addCheck(p, "Offline-Modus: API gar nicht erst abfragen", "offline", cfg.offlineMode);

  // ---- API ----
  p += "<h2>API</h2><label>URL der Zahlen<input name=url value='";
  p += cfg.apiUrl;
  p += "'></label><label>URL des Kampagnenstatus<input name=campurl value='";
  p += cfg.campUrl;
  p += "'></label>";
  addCheck(p, "Kampagnenstatus beruecksichtigen", "usecamp", cfg.useCampaign);
  p += "<small>Vor dem Start laeuft dann ein ruhiges Wartemuster statt einer 0, "
       "nach dem Ende bleibt der Endstand stehen und atmet langsam.</small>";
  addCheck(p, "Feier-Animation, wenn das Ziel geknackt ist", "goalanim", cfg.goalAnim);

  p += "<label>Abfrage alle ... Sekunden<input name=poll type=number min=10 max=3600 value=";
  p += String(cfg.pollSeconds);
  p += "><small>Der Server cacht 5 Minuten, oefter als alle 60 s bringt nichts.</small></label>";

  p += "<button type=submit>Speichern und neu starten</button></form>";

  // ---- Test / Reset ----
  p += "<h2>Testen</h2><p><a href='/test?value=43690'>Muster 1010...</a> &middot; "
       "<a href='/test?value=1'>nur Bit 0</a> &middot; "
       "<a href='/test?value=255'>erstes Byte voll</a> &middot; "
       "<a href='/status.json'>status.json</a><br>"
       "<small>Die Testwerte stehen 15 Sekunden lang an -- gut um Reihenfolge und "
       "Farbkanaele zu pruefen.</small></p>"
       "<h2>Taster</h2><p><small>Der BOOT-Taster am Board: kurz druecken schaltet zum "
       "naechsten Effekt, lang druecken (2 s) oeffnet dieses Portal auch dann, wenn "
       "das Display gerade normal laeuft.</small></p>"
       "<p><a href='/reset' onclick=\"return confirm('Alle Einstellungen loeschen?')\">"
       "Werkseinstellungen</a></p></html>";

  server.send(200, "text/html; charset=utf-8", p);
}

// ---------------------------------------------------------------------
//  Kleiner Statusbericht als JSON -- zum Nachsehen, wenn vor Ort
//  etwas klemmt und man nur ein Handy dabei hat.
// ---------------------------------------------------------------------
void handleStatusJson() {
  String p = "{";
  p += "\"value\":";        p += String(apiValue);
  p += ",\"goal\":";        p += String(apiGoal);
  p += ",\"campaign\":\"";  p += CAMPAIGN_NAMES[apiCampaign];
  p += "\",\"error\":\"";   p += apiError;
  p += "\",\"mode\":\"";    p += MODE_NAMES[cfg.displayMode];
  p += "\",\"effect\":\"";  p += EFFECT_NAMES[cfg.effect];
  p += "\",\"leds\":";      p += String(cfg.numLeds);
  p += ",\"uptime_s\":";    p += String(millis() / 1000);
  p += ",\"last_ok_s\":";   p += String(apiLastOkMs ? (millis() - apiLastOkMs) / 1000 : 0);
  p += ",\"ap_mode\":";     p += (portalApMode ? "true" : "false");
  p += ",\"ssid\":\"";      p += WiFi.SSID();
  p += "\",\"ip\":\"";      p += (portalApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  p += "\",\"rssi\":";      p += String(WiFi.RSSI());
  p += ",\"free_heap\":";   p += String(ESP.getFreeHeap());
  p += "}";
  server.send(200, "application/json", p);
}

// ---------------------------------------------------------------------
//  Formular speichern
// ---------------------------------------------------------------------
void handleSave() {
  char name[8];
  for (uint8_t i = 0; i < WIFI_SLOTS; i++) {
    snprintf(name, sizeof(name), "ssid%u", (unsigned)i);
    if (server.hasArg(name))
      strlcpy(cfg.ssid[i], server.arg(name).c_str(), sizeof(cfg.ssid[i]));
    // leeres Passwortfeld = altes Passwort behalten
    snprintf(name, sizeof(name), "pass%u", (unsigned)i);
    if (server.arg(name).length())
      strlcpy(cfg.pass[i], server.arg(name).c_str(), sizeof(cfg.pass[i]));
  }

  if (server.arg("url").length())
    strlcpy(cfg.apiUrl, server.arg("url").c_str(), sizeof(cfg.apiUrl));
  if (server.arg("campurl").length())
    strlcpy(cfg.campUrl, server.arg("campurl").c_str(), sizeof(cfg.campUrl));

  cfg.numLeds      = argInt("leds",     1, MAX_LEDS,          cfg.numLeds);
  cfg.brightness   = argInt("bright",   1, 255,               cfg.brightness);
  cfg.maxMilliamps = argInt("maxma",    0, 20000,             cfg.maxMilliamps);
  cfg.pollSeconds  = argInt("poll",    10, 3600,              cfg.pollSeconds);
  cfg.metric       = argInt("metric",   0, METRIC_COUNT - 1,  cfg.metric);
  cfg.displayMode  = argInt("dispmode", 0, MODE_COUNT - 1,    cfg.displayMode);
  cfg.chipset      = argInt("chip",     0, CHIPSET_COUNT - 1, cfg.chipset);
  cfg.colorOrder   = argInt("order",    0, ORDER_COUNT - 1,   cfg.colorOrder);
  cfg.effect       = argInt("effect",   0, EFFECT_COUNT - 1,  cfg.effect);
  cfg.effectSpeed  = argInt("espeed",   1, 10,                cfg.effectSpeed);

  // Ankreuzfelder: taucht der Name nicht auf, ist das Haekchen weg
  cfg.msbFirst      = server.arg("msb") == "1";
  cfg.flashOnChange = server.arg("flash") == "1";
  cfg.startupAnim   = server.arg("startanim") == "1";
  cfg.offlineMode   = server.arg("offline") == "1";
  cfg.goalAnim      = server.arg("goalanim") == "1";
  cfg.useCampaign   = server.arg("usecamp") == "1";

  cfg.colorOn   = hexToColor(server.arg("con"),   cfg.colorOn);
  cfg.colorOff  = hexToColor(server.arg("coff"),  cfg.colorOff);
  cfg.colorGoal = hexToColor(server.arg("cgoal"), cfg.colorGoal);

  configSave();

  // Von Hand gesetzter Wert wird wie ein API-Wert behandelt: gespeichert,
  // und nach dem Neustart steht er sofort wieder da.
  valueStore((uint32_t) argInt("manual", 0, 2147483647L, apiValue));

  server.send(200, "text/html; charset=utf-8",
    "<!doctype html><meta charset=utf-8>"
    "<body style='font-family:sans-serif;background:#111;color:#eee;padding:2rem'>"
    "<h1>Gespeichert</h1><p>Der ESP32 startet jetzt neu und verbindet sich.</p>");

  rebootPending = true;     // Neustart erst nachdem die Antwort raus ist
}

void handleScan() {
  portalScan();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleTest() {
  testValue   = (uint32_t) server.arg("value").toInt();
  testUntilMs = millis() + 15000;   // siehe testActive()
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleReset() {
  prefs.begin("bindisp", false);
  prefs.clear();
  prefs.end();
  server.send(200, "text/html; charset=utf-8",
    "<!doctype html><meta charset=utf-8>Zurueckgesetzt, Neustart...");
  rebootPending = true;
}

// Alles Unbekannte auf die Startseite umleiten. Genau das laesst
// Handys und Laptops das Konfigurationsfenster automatisch aufpoppen.
void handleNotFound() {
  String target = "http://";
  target += portalApMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  target += "/";
  server.sendHeader("Location", target, true);
  server.send(302, "text/plain", "");
}

// Laeuft gerade eine Testanzeige? Die Differenz-Schreibweise ist
// unempfindlich gegen den Ueberlauf von millis() nach 49 Tagen.
bool testActive() {
  return testUntilMs != 0 && (int32_t)(testUntilMs - millis()) > 0;
}

// ---------------------------------------------------------------------
//  Starten
// ---------------------------------------------------------------------
void portalRoutes() {
  static bool started = false;
  if (started) return;              // der Taster kann das hier nochmal aufrufen
  started = true;

  server.on("/",            HTTP_GET,  handleRoot);
  server.on("/status.json", HTTP_GET,  handleStatusJson);
  server.on("/save",        HTTP_POST, handleSave);
  server.on("/scan",        HTTP_GET,  handleScan);
  server.on("/test",        HTTP_GET,  handleTest);
  server.on("/reset",       HTTP_GET,  handleReset);
  server.onNotFound(handleNotFound);
  server.begin();
}

// Eigenes WLAN aufmachen. Passiert beim Start, wenn kein WLAN erreichbar
// ist, und jederzeit per langem Tastendruck.
void portalStartAp() {
  if (portalApMode) return;
  portalApMode = true;

  WiFi.mode(WIFI_AP_STA);            // AP_STA, damit der Netzwerk-Scan funktioniert
  WiFi.softAP(AP_SSID, strlen(AP_PASS) >= 8 ? AP_PASS : NULL);
  delay(200);
  dnsServer.start(53, "*", WiFi.softAPIP());   // "*" = jede Domain hierher
  portalScan();
  portalRoutes();

  Serial.println();
  Serial.println("Konfigurationsportal gestartet:");
  Serial.printf("  WLAN     : %s\n", AP_SSID);
  Serial.printf("  Passwort : %s\n", AP_PASS);
  Serial.printf("  Adresse  : http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void portalLoop() {
  if (portalApMode) dnsServer.processNextRequest();
  server.handleClient();

  if (rebootPending) {
    delay(300);            // Antwort noch rausschicken lassen
    ESP.restart();
  }
}
