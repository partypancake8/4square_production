// leds.h, what the three RGB LEDs say.
//
// D2, D4 and D5 form a VERTICAL COLUMN on the board: D2 top, D4 middle, D5
// bottom, in that chain order. That geometry is the whole language, a state
// is not just a colour, it is a movement along the column, and movement is far
// easier to read across a dark room than a hue is.
//
// (These three were reverse-mounted on the rev O build, the GL5050's chamfer
// marks pin 3/GND, not pin 1, and were hot-air rotated on 2026-08-07. Any
// design note that assumes they are not fitted is stale.)
//
// IDLE IS DARK. This lives in a bedroom. An indicator that is always doing
// something is an indicator you stop reading.
#pragma once
#include <Arduino.h>
#include "../config.h"

// Highest number wins. The order is the whole contract: an OTA in flight must
// never be masked by a WiFi animation, and a bus fault must never be masked by
// a button acknowledgement.
enum LedStatus : uint8_t {
  LED_NONE = 0,
  LED_WIFI_UP,        // 10  one-shot, then quiet for good
  LED_ACK_MODE,       // 20  spatial button feedback
  LED_ACK_SET,
  LED_ACK_UP,
  LED_ACK_DOWN,
  LED_CONFIRM,        // 30  a setting was saved
  LED_REJECT,         // 30  that input did nothing
  LED_MENU,           // 40  sticky while in the menus
  LED_BOOT,           // 50  one-shot at power-on
  LED_WIFI_FAIL,      // 55
  LED_WIFI_JOINING,   // 60
  // NO LED_SETUP_AP HERE. This tree has no provisioning-AP module (net/ held
  // only the market fetcher, removed 2026-10-15), so there is no condition that could assert
  // one. Do not add the state before the module that drives it exists: a state
  // nothing asserts is a state nothing tests, and tools/ledcheck would be
  // proving the smoothness of an animation the device can never reach.
  LED_RTC_UNSET,      // 75  the clock does not know what time it is
  LED_FAULT_I2C,      // 80  the bus is erroring
  LED_IDENTIFY,       // 90  "which one is this device"
  LED_OTA_FAIL,       // 95
  LED_OTA_ACTIVE,     // 100 always shown, always at full brightness
  LED_COUNT
};

void led_begin();

// Sticky states: on until turned off.
void led_hold(LedStatus s, bool on);
// One-shot states: shown for their own duration and then forgotten.
void led_fire(LedStatus s);
// Button feedback, positioned to match the button.
void led_ack(uint8_t btn_index);
// Renders IMMEDIATELY, from the OTA callback. ArduinoOTA.handle() blocks for
// the whole transfer, so loop(), and therefore led_tick(), never runs while
// an update is in flight. Merely storing the percentage meant the LEDs froze
// on whatever they last showed, which with WiFi idle is dark: indistinguishable
// from a bricked board at exactly the moment you most want to know.
void led_ota_progress(uint8_t pct);

// Called every frame. `ambient_raw` is the blanked light reading, so the LEDs
// are as bright as the room justifies and no brighter.
//
// EVERY GAIT IS A PURE FUNCTION OF WALL TIME, never of how many times this has
// been called. loop() serialises led_tick() with ui_paint(), which on the
// animation page pushes ~92 ms of blocking I2C, so the real cadence collapses
// from the intended 60 Hz to nearer 10 Hz and does so unevenly. A gait driven
// by a frame COUNTER would stutter and change speed with whatever the panels
// were doing; driven by `now` it merely gets sampled more coarsely, which is
// the difference between an animation that limps and one that is simply drawn
// less often. The one thing that genuinely cannot survive the coarse sampling
// is the temporal dither, so it measures the real interval and switches itself
// off, see the dither note in leds.cpp.
void led_tick(uint32_t now, uint16_t ambient_raw, bool night);
void led_all_off();

// ---- the light-sensor blank ------------------------------------------------
// R7 sits 9.5 mm from D2 on the same board face, so the LDR reads our own LEDs
// unless they are off: light_sample() has to blank the strip to measure the
// ROOM. It used to do that by reaching into `strip` directly and leaving it
// black until the next led_tick, a 25 ms hard-edged hole punched into every
// animation, once a second, which is the black notch the user reported as
// flicker.
//
// So the blank is bracketed here instead, and the strip is private to leds.cpp
// again. begin() returns FALSE when the strip is already dark, in which case
// there is nothing to blank, no settling to wait for and no hole to punch  - 
// which, since IDLE IS DARK, is the common case. When it returns true it has
// eased the strip down rather than cutting it, and end() eases it back to the
// exact frame that was showing, so the LED image is continuous across the read.
bool led_is_dark();
bool led_light_blank_begin();
void led_light_blank_end();

// ---- the failsafes ---------------------------------------------------------
// A HELD STATE IS A NOTIFICATION, NOT A STATUS BAR. Every sticky state above
// is asserted by a condition that can fail to clear, a missing panel, a join
// that never succeeds, and each one is therefore given a time budget in
// leds.cpp. When it lapses the state stops being RENDERED while remaining
// asserted, so the bedroom goes dark and the serial report still says why.
//
// millis() when led_ota_progress() last ran, or 0 if no transfer has started.
// loop() uses it to recover from an OTA that ended without firing onEnd or
// onError, the one failure that leaves the LEDs lit with the main loop
// short-circuited and the loop watchdog switched off.
uint32_t led_ota_progress_ms();
// Held states and how much of their budget is left, for the serial `P` report.
void led_report();

// The two tables above, readable. led_report() uses them, and so does the LED
// prover (tools/ledcheck), which asserts mechanically that EVERY state which is
// not a one-shot has a hold budget, the failsafe described above is worth
// nothing if a state added next year quietly has neither.
uint32_t led_oneshot_ms(uint8_t s);
uint32_t led_hold_max_ms(uint8_t s);
