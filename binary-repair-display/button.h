// =====================================================================
//  button.h  --  der Taster am Board
// =====================================================================
//
//  GPIO 0 ist auf praktisch jedem ESP32-DevKit der BOOT-Taster, samt
//  Pullup -- es muss also nichts angeloetet werden. Gedrueckt zieht er
//  den Pin auf Masse, ungedrueckt liegt er auf 3,3 V.
//
//    kurz druecken  -> naechster Effekt (wird gleich mitgespeichert)
//    lang druecken  -> Konfigurationsportal aufmachen
//
//  Damit kommt man vor Ort ohne Laptop und ohne die IP zu kennen an die
//  Einstellungen heran.
//
#pragma once
#include "config.h"
#include "display.h"
#include "portal.h"

#define BUTTON_LONG_MS   2000    // ab hier gilt es als langer Druck
#define BUTTON_DEBOUNCE  30      // Prellzeit des mechanischen Kontakts

static bool     btnRaw      = false;   // zuletzt gelesener Pegel
static uint32_t btnRawMs    = 0;       // wann er sich zuletzt geaendert hat
static bool     btnPressed  = false;   // entprellter Zustand
static uint32_t btnDownMs   = 0;       // seit wann gedrueckt
static bool     btnLongDone = false;   // langer Druck schon ausgeloest?

void buttonBegin() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void buttonLoop() {
  bool raw = (digitalRead(BUTTON_PIN) == LOW);

  // Entprellen: eine Aenderung zaehlt erst, wenn sie kurz stabil bleibt.
  if (raw != btnRaw) {
    btnRaw   = raw;
    btnRawMs = millis();
    return;
  }
  if (millis() - btnRawMs < BUTTON_DEBOUNCE) return;

  // ---- gerade gedrueckt ----
  if (btnRaw && !btnPressed) {
    btnPressed  = true;
    btnDownMs   = millis();
    btnLongDone = false;
    return;
  }

  // ---- gehalten: langer Druck schlaegt zu, ohne aufs Loslassen zu warten,
  //      damit man sofort sieht, dass es geklappt hat ----
  if (btnRaw && btnPressed && !btnLongDone && millis() - btnDownMs > BUTTON_LONG_MS) {
    btnLongDone = true;
    if (!portalApMode) {
      Serial.println("Taster lang -- Konfigurationsportal wird geoeffnet");
      displayConfirm(COLOR_PORTAL);
      portalStartAp();
    }
    return;
  }

  // ---- losgelassen ----
  if (!btnRaw && btnPressed) {
    btnPressed = false;
    if (!btnLongDone) {                       // war also ein kurzer Druck
      cfg.effect = (cfg.effect + 1) % EFFECT_COUNT;
      configSave();
      Serial.printf("Taster kurz -- Effekt: %s\n", EFFECT_NAMES[cfg.effect]);
    }
  }
}
