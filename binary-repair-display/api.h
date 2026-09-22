// =====================================================================
//  api.h  --  Zahlen von reparatur.fab-bergisch.org holen
// =====================================================================
//
//  Doku: https://reparatur.fab-bergisch.org/api-doku
//    /api/stats     total, attempted, today, pending, goal (5 min Cache)
//    /api/campaign  status: before | open | after, dazu goal
//  Kein Login noetig, 120 Anfragen pro Minute.
//
#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

uint32_t apiValue    = 0;      // zuletzt erfolgreich gelesener Wert
uint32_t apiGoal     = 0;      // Ziel der Kampagne (0 = unbekannt)
String   apiError    = "";     // leer = alles gut
uint32_t apiLastOkMs = 0;      // Zeitpunkt der letzten erfolgreichen Abfrage

// Stand der Kampagne. Vorher zeigt das Display ein Wartemuster statt
// einer 0, nachher bleibt der Endstand stehen und atmet langsam.
enum CampaignStatus { CAMPAIGN_UNKNOWN = 0, CAMPAIGN_BEFORE, CAMPAIGN_OPEN, CAMPAIGN_AFTER };
const char* const CAMPAIGN_NAMES[] = { "unbekannt", "noch nicht gestartet", "laeuft", "beendet" };
CampaignStatus apiCampaign = CAMPAIGN_UNKNOWN;

// ---------------------------------------------------------------------
//  Eine JSON-Antwort holen
// ---------------------------------------------------------------------
//  Gemeinsam fuer /api/stats und /api/campaign. Setzt bei Problemen
//  apiError und gibt false zurueck.
//
static bool httpGetJson(const char* url, JsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) {
    apiError = "Kein WLAN";
    return false;
  }

  HTTPClient http;
  bool started;

  if (String(url).startsWith("https://")) {
    // Zertifikat wird nicht geprueft. Fuer eine oeffentliche Zahl auf einem
    // Display ist das in Ordnung und spart die Pflege des Root-Zertifikats.
    static WiFiClientSecure secure;
    secure.setInsecure();
    started = http.begin(secure, url);
  } else {
    static WiFiClient plain;
    started = http.begin(plain, url);
  }

  if (!started) {
    apiError = "URL ungueltig";
    return false;
  }

  http.setTimeout(8000);
  http.addHeader("Accept", "application/json");
  http.setUserAgent("binary-repair-display/1.0");

  int code = http.GET();

  if (code != 200) {
    // Die Doku nennt diese Faelle ausdruecklich:
    if      (code == 403) apiError = "Kampagne laeuft gerade nicht (403)";
    else if (code == 429) apiError = "Zu viele Anfragen (429) - Intervall erhoehen";
    else if (code >= 500) apiError = String("Server nicht erreichbar (") + code + ")";
    else if (code <  0)   apiError = String("Verbindungsfehler: ") + http.errorToString(code);
    else                  apiError = String("HTTP ") + code;
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    apiError = String("JSON-Fehler: ") + err.c_str();
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------
//  /api/stats -- die eigentliche Zahl
// ---------------------------------------------------------------------
//  Rueckgabe: true bei Erfolg. Bei Fehlern bleibt der alte Wert stehen --
//  ein kurzer Serverausfall soll die Anzeige nicht auf 0 springen lassen.
//
bool apiFetch() {
  JsonDocument doc;
  if (!httpGetJson(cfg.apiUrl, doc)) return false;

  JsonVariant v = doc[METRIC_KEYS[cfg.metric]];
  if (v.isNull()) {
    apiError = String("Feld '") + METRIC_KEYS[cfg.metric] + "' fehlt in der Antwort";
    return false;
  }

  apiValue    = v.as<uint32_t>();
  apiGoal     = doc["goal"] | 0;
  apiError    = "";
  apiLastOkMs = millis();
  return true;
}

// ---------------------------------------------------------------------
//  /api/campaign -- laeuft die Kampagne ueberhaupt schon?
// ---------------------------------------------------------------------
//  Wird deutlich seltener abgefragt als die Zahl, der Status aendert
//  sich ja hoechstens zweimal im Jahr.
//
bool apiFetchCampaign() {
  // Ein Fehler der Zahlen-Abfrage soll hier weder ueberschrieben noch
  // durch einen Kampagnen-Fehler ersetzt werden -- die Zahl ist wichtiger.
  String keep = apiError;

  JsonDocument doc;
  if (!httpGetJson(cfg.campUrl, doc)) { apiError = keep; return false; }

  const char* status = doc["status"] | "";
  if      (strcmp(status, "before") == 0) apiCampaign = CAMPAIGN_BEFORE;
  else if (strcmp(status, "open")   == 0) apiCampaign = CAMPAIGN_OPEN;
  else if (strcmp(status, "after")  == 0) apiCampaign = CAMPAIGN_AFTER;
  else                                    apiCampaign = CAMPAIGN_UNKNOWN;

  uint32_t goal = doc["goal"] | 0;
  if (goal) apiGoal = goal;

  apiError = keep;
  return true;
}
