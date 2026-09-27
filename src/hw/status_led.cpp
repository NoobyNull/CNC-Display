#include "hw/status_led.h"
#include "model/status_color.h"
#include "model/settings.h"
#include <Adafruit_NeoPixel.h>

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 4
#endif
#ifndef STATUS_LED_COUNT
#define STATUS_LED_COUNT 8
#endif
// The bar follows the panel brightness setting but never below this, so a
// dimmed screen cannot silently turn the fault light off.
#ifndef STATUS_LED_MIN_BRIGHT
#define STATUS_LED_MIN_BRIGHT 40
#endif

static Adafruit_NeoPixel s_px(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t s_shown = 0xFFFFFFFF;   // impossible colour so the first tick writes
static uint8_t  s_bright = 0;

// STATUS_LED_PIN < 0 builds the bar out entirely (bisecting, or a panel
// with no bar fitted).
void status_led_begin() {
    if (STATUS_LED_PIN < 0) return;
    s_px.begin();
    status_led_tick();
}

void status_led_tick() {
    if (STATUS_LED_PIN < 0) return;
    uint8_t  b = settings_brightness();
    if (b < STATUS_LED_MIN_BRIGHT) b = STATUS_LED_MIN_BRIGHT;
    uint32_t c = status_color(state);
    if (c == s_shown && b == s_bright) return;
    s_shown = c; s_bright = b;
    s_px.setBrightness(b);
    s_px.fill(c);
    s_px.show();
}

int status_led_pin() { return STATUS_LED_PIN; }
