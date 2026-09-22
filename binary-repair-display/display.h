// =====================================================================
//  display.h  --  alles was auf die LEDs geht
// =====================================================================
//
//  Die Streifen haengen in Reihe: der DOUT des ersten geht auf den DIN
//  des zweiten usw. Fuer den ESP32 ist das eine einzige lange Kette --
//  und genau so sieht sie auch aus: eine durchgehende Farbe, keine
//  Unterscheidung zwischen den Streifen.
//
//  Wie viele LEDs dranhaengen, kann Hardware nicht zurueckmelden --
//  deshalb steht die Zahl in der Konfiguration (cfg.numLeds).
//
#pragma once
#include <FastLED.h>
#include "config.h"

CRGB leds[MAX_LEDS];

// Farbe fuer den Effekt "neue Farbe bei jedem Update". Wird jedes Mal
// weitergedreht, wenn eine neue Reparatur dazukommt. 43 ist ein gutes
// Stueck des Farbkreises -- nie zweimal hintereinander etwas Aehnliches.
uint8_t updateHue = 0;
#define HUE_STEP_PER_UPDATE  43

// Ziel geknackt? Dann glimmen auch die Nullen mit (siehe offColor).
bool goalGlow = false;

#define COLOR_OVERFLOW  CRGB(255, 0, 0)   // Zahl passt nicht mehr in die LEDs
#define COLOR_PORTAL    CRGB(0, 80, 255)  // Hinweis "Konfigurationsseite offen"
#define COLOR_CONNECTED CRGB(0, 255, 60)  // Erfolgsmeldung "WLAN verbunden"

// ---------------------------------------------------------------------
//  FastLED starten
// ---------------------------------------------------------------------
//  FastLED braucht Chip-Typ, Pin und Farbreihenfolge zur Compile-Zeit.
//  Damit man den Streifentyp trotzdem im Webinterface waehlen kann,
//  sind alle Kombinationen einkompiliert und es wird beim Start genau
//  eine davon aktiviert.
//
#define ADD_CHIPSET(idx, CHIP)                                                    \
  case idx * 3 + 0: FastLED.addLeds<CHIP, LED_PIN, GRB>(leds, MAX_LEDS); break;   \
  case idx * 3 + 1: FastLED.addLeds<CHIP, LED_PIN, RGB>(leds, MAX_LEDS); break;   \
  case idx * 3 + 2: FastLED.addLeds<CHIP, LED_PIN, BRG>(leds, MAX_LEDS); break;

void displayBegin() {
  switch (cfg.chipset * 3 + cfg.colorOrder) {
    ADD_CHIPSET(0, WS2812B)
    ADD_CHIPSET(1, WS2811)
    ADD_CHIPSET(2, WS2813)
    ADD_CHIPSET(3, WS2815)
    ADD_CHIPSET(4, SK6812)
    default: FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, MAX_LEDS); break;
  }

  // Strombegrenzung. FastLED rechnet vor jedem show() aus, wie viel die
  // Kette ziehen wuerde, und dimmt notfalls alles gleichmaessig herunter.
  // Ohne das reisst eine volle weisse Kette ein schwaches Netzteil nach
  // unten und der ESP32 startet mitten im Betrieb neu.
  if (cfg.maxMilliamps > 0)
    FastLED.setMaxPowerInVoltsAndMilliamps(5, cfg.maxMilliamps);

  FastLED.setBrightness(cfg.brightness);
  FastLED.clear(true);
}
#undef ADD_CHIPSET

// ---------------------------------------------------------------------
//  Farbe einer ausgeschalteten LED
// ---------------------------------------------------------------------
//  Normalerweise schwarz (bzw. was im Portal eingestellt ist). Sobald
//  das Ziel der Kampagne geknackt ist, glimmen die Nullen dauerhaft in
//  einer zweiten Farbe mit -- dann leuchtet die ganze Kette und nicht
//  mehr nur die Einsen. Unuebersehbar, ohne den gewaehlten Effekt zu
//  ueberfahren.
//
static CRGB offColor() {
  if (!goalGlow) return CRGB(cfg.colorOff);
  CRGB c = CRGB(cfg.colorGoal);
  c.nscale8_video(beatsin8(12, 40, 120));   // langsames Atmen
  return c;
}

// ---------------------------------------------------------------------
//  Farbe einer eingeschalteten LED
// ---------------------------------------------------------------------
//  k            = die wievielte eingeschaltete LED das ist (0, 1, 2, ...)
//  activeCount  = wie viele LEDs insgesamt an sind
//
//  Wichtig: gezaehlt wird nur ueber die Einsen. Ein Regenbogen verteilt
//  sich also gleichmaessig ueber die leuchtenden LEDs, die Nullen
//  dazwischen werden uebersprungen und denken nicht mit.
//
static CRGB effectColor(uint8_t k, uint8_t activeCount) {
  const uint8_t spread = 255 / activeCount;   // Farbabstand zweier Einsen
  const uint8_t speed  = cfg.effectSpeed;     // 1 bis 10

  switch (cfg.effect) {

    case EFFECT_RAINBOW:
      // Verlauf ueber die aktiven LEDs, der langsam durchwandert.
      // beat8() liefert einen Saegezahn mit der angegebenen Drehzahl.
      return CHSV(k * spread + beat8(speed * 2), 255, 255);

    case EFFECT_UPDATE:
      // Alle Einsen in derselben Farbe -- sie springt bei jeder
      // neuen Reparatur ein Stueck weiter (siehe updateHue).
      return CHSV(updateHue, 255, 255);

    case EFFECT_BREATHE: {
      CRGB c = CRGB(cfg.colorOn);
      c.nscale8_video(beatsin8(speed * 3 + 5, 40, 255));
      return c;
    }

    case EFFECT_PARTY: {
      // Jede Eins hat ihre eigene Farbe, und alle springen im Takt
      // weiter -- hart, ohne Ueberblenden.
      uint8_t jump = (millis() / (1200 / speed)) * 41;
      return CHSV(k * spread + jump, 255, 255);
    }

    case EFFECT_SOLID:
    default:
      return CRGB(cfg.colorOn);
  }
}

// ---------------------------------------------------------------------
//  Anzeigeart 1: die Zahl binaer
// ---------------------------------------------------------------------
//  LED i zeigt genau ein Bit. Welches, haengt von der Montagerichtung ab:
//    msbFirst = false : LED 0 (am Dateneingang) = Bit 0  -> 1 2 4 8 ...
//    msbFirst = true  : LED 0 = hoechstes Bit            -> wie geschrieben
//
static void fillBinary(uint32_t value) {
  const uint8_t n = cfg.numLeds;

  // Passt die Zahl ueberhaupt in n Bit? (bei 32 LEDs immer)
  const bool overflow = (n < 32) && (value >= (1UL << n));

  // Erst zaehlen, wie viele LEDs an sind -- die Effekte brauchen das,
  // um ihre Farben ueber genau diese LEDs zu verteilen.
  uint8_t activeCount = 0;
  for (uint8_t b = 0; b < n; b++)
    if ((value >> b) & 1UL) activeCount++;
  if (activeCount == 0) activeCount = 1;      // Teilen durch Null vermeiden

  uint8_t k = 0;                              // Nummer unter den aktiven LEDs
  for (uint8_t i = 0; i < n; i++) {
    uint8_t bit = cfg.msbFirst ? (uint8_t)(n - 1 - i) : i;

    if (!((value >> bit) & 1UL)) {
      leds[i] = offColor();                   // Bit = 0
      continue;
    }
    leds[i] = overflow ? COLOR_OVERFLOW : effectColor(k, activeCount);
    k++;
  }
}

// ---------------------------------------------------------------------
//  Anzeigeart 2: Fortschrittsbalken zum Ziel
// ---------------------------------------------------------------------
//  Ohne Vorkenntnisse lesbar: wie voll ist der Balken, so weit ist die
//  Kampagne. Gerechnet wird in 1/256-Schritten, damit die vorderste LED
//  den angebrochenen Rest anteilig dunkler anzeigen kann.
//
static void fillBar(uint32_t value, uint32_t goal) {
  const uint8_t n = cfg.numLeds;
  if (goal == 0) { fillBinary(value); return; }     // ohne Ziel kein Balken

  uint32_t frac = (value >= goal)
                    ? (uint32_t)n * 256
                    : (uint32_t)(((uint64_t)value * n * 256) / goal);

  // Wie viele LEDs leuchten (mindestens eine), damit sich Verlaeufe
  // ueber den gefuellten Teil verteilen und nicht ueber die ganze Kette.
  uint8_t lit = (uint8_t)((frac + 255) / 256);
  if (lit == 0) lit = 1;

  for (uint8_t i = 0; i < n; i++) {
    uint8_t pos = cfg.msbFirst ? (uint8_t)(n - 1 - i) : i;   // gleiche Richtung wie die Zahl
    uint32_t start = (uint32_t)i * 256;

    if (frac >= start + 256) {
      leds[pos] = effectColor(i, lit);                        // voll
    } else if (frac > start) {
      CRGB c = effectColor(i, lit);
      c.nscale8_video((uint8_t)(frac - start));               // angebrochen
      leds[pos] = c;
    } else {
      leds[pos] = offColor();                                 // noch leer
    }
  }
}

// ---------------------------------------------------------------------
//  Hinweis "Konfigurationsseite ist offen"
// ---------------------------------------------------------------------
//  Die erste LED pulsiert. Wichtig dabei: das muss in beiden Zustaenden
//  funktionieren. Wuerde man nur die Helligkeit drehen, bliebe eine LED
//  mit Bit = 0 schwarz und man saehe gar nichts. Stattdessen wird zur
//  Markierungsfarbe hin gemischt -- aus Schwarz wird dadurch ein blaues
//  Pulsieren, aus einer leuchtenden LED ein Hin und Her der Farbe.
//
static void applyPortalPulse() {
  // Der Anteil geht nie ganz auf 0 -- das macht aus einem harten Blinken
  // ein ruhiges Pulsieren, und der Hinweis ist auch im tiefsten Punkt
  // noch da. Eine volle Schwingung dauert rund zwei Sekunden.
  uint8_t amount = beatsin8(30, 40, 150);
  leds[0] = blend(leds[0], COLOR_PORTAL, amount);
}

// ---------------------------------------------------------------------
//  Ein komplettes Bild zeichnen
// ---------------------------------------------------------------------
void displayFrame(uint32_t value, uint32_t goal, bool goalReached, bool portalOpen) {
  goalGlow = goalReached;

  if (cfg.displayMode == MODE_BAR) fillBar(value, goal);
  else                             fillBinary(value);

  for (uint8_t i = cfg.numLeds; i < MAX_LEDS; i++) leds[i] = CRGB::Black;

  if (portalOpen) applyPortalPulse();
  FastLED.show();
}

// ---------------------------------------------------------------------
//  Animationen und Statusmuster
// ---------------------------------------------------------------------

// Start-Animation: ein Punkt laeuft einmal hin und zurueck. Zeigt
// nebenbei, dass jede einzelne LED funktioniert. Blockiert rund 2,5 s.
void displayStartupSweep() {
  const uint8_t n = cfg.numLeds;
  const uint16_t stepMs = 1200 / n;           // eine Richtung dauert ~1,2 s

  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint8_t s = 0; s < n; s++) {
      uint8_t pos = (pass == 0) ? s : (uint8_t)(n - 1 - s);
      fadeToBlackBy(leds, n, 90);             // der alte Punkt verblasst
      leds[pos] = CRGB(cfg.colorOn);
      FastLED.show();
      delay(stepMs);
    }
  }
  fill_solid(leds, MAX_LEDS, CRGB::Black);
  FastLED.show();
}

// WLAN steht: ein gruener Wisch von vorn nach hinten, danach ausblenden.
// Laeuft bei jeder Verbindung, also auch wenn das WLAN zurueckkommt --
// so sieht man im Raum sofort, dass es wieder geht. Dauert rund 1 s.
void displayConnectedSweep() {
  const uint8_t n = cfg.numLeds;

  fill_solid(leds, MAX_LEDS, CRGB::Black);
  for (uint8_t i = 0; i < n; i++) {
    leds[i] = COLOR_CONNECTED;
    FastLED.show();
    delay(600 / n);
  }
  for (uint8_t f = 0; f < 20; f++) {
    fadeToBlackBy(leds, n, 40);
    FastLED.show();
    delay(20);
  }
}

// Ziel geknackt: einmalig rund sechs Sekunden Regenbogen ueber die
// ganze Kette. Laeuft nur einmal, der Merker liegt im Flash.
void displayGoalCelebration() {
  const uint8_t n = cfg.numLeds;
  for (uint16_t f = 0; f < 150; f++) {
    fill_rainbow(leds, n, (uint8_t)(f * 3), (uint8_t)(255 / n + 1));
    FastLED.show();
    delay(40);
  }
}

// Kampagne laeuft noch nicht: ein ruhiger Punkt zieht langsam hin und
// her. Deutlich ruhiger als eine blinkende 0 -- und es ist klar, dass
// das Display lebt und nur noch nichts zu zaehlen hat.
void displayWaitingPattern() {
  const uint8_t n = cfg.numLeds;
  fadeToBlackBy(leds, n, 12);
  uint8_t pos = beatsin8(4, 0, (uint8_t)(n - 1));
  leds[pos] = CRGB(cfg.colorOn);
  leds[pos].nscale8_video(120);
  for (uint8_t i = n; i < MAX_LEDS; i++) leds[i] = CRGB::Black;
  FastLED.show();
}

// WLAN-Verbindungsaufbau: gelber Punkt laeuft durch
void displayConnectingPattern() {
  fill_solid(leds, MAX_LEDS, CRGB::Black);
  uint8_t pos = (millis() / 120) % cfg.numLeds;
  leds[pos] = CRGB(60, 40, 0);
  FastLED.show();
}

// Kurzes Aufblitzen, wenn eine neue Reparatur dazugekommen ist
void displayFlash() {
  fill_solid(leds, cfg.numLeds, CRGB::White);
  FastLED.show();
  delay(120);
}

// Kurze Bestaetigung in einer Farbe, z. B. nach einem Tastendruck
void displayConfirm(const CRGB& color) {
  fill_solid(leds, cfg.numLeds, color);
  FastLED.show();
  delay(200);
}
