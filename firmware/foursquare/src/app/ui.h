// ui.h, version A: buttons, page selection, and deciding what to repaint.
//
// THE GRAMMAR, in one sentence:
//   each button owns a page, and pressing it again cycles a variant of it.
//
//   MODE  clock     SET  sensors     UP  stopwatch   DOWN  animations
//   (on the stopwatch, DOWN pauses a running one and clears a paused one)
//
// There is no menu, no modal editor and nothing to get stuck in: every button
// is always a direct jump to a page you can see the name of. Settings that
// used to need the menu are reachable over serial (`P`, `D`, `F`) and by long
// press for the two that matter day to day.
#pragma once
#include <Arduino.h>
#include "../screens/pages.h"
#include "../board/sensors.h"

// Live values the pages read. Filled in by the main loop; the UI never talks
// to hardware itself, which is what keeps the render path pure.
struct UiEnv {
  float    sht_c;
  uint8_t  rh;
  bool     sht_ok;
  bool     wifi_up;
  bool     ota_ready;
  int      rssi;
  char     ip[20];
  char     ssid[24];
  uint32_t boots;
};
extern UiEnv ui_env;

// The button bitmaps the `T` serial line reports. Preserved verbatim because
// 4square-mon.py parses these two fields.
extern uint8_t btn_stable, btn_latched;

void ui_begin();

// Poll the buttons and act. Returns true if anything changed on screen.
bool ui_input(uint32_t now);

// The animation clock and the anti-burn-in shift.
void ui_tick(uint32_t now, const RtcTime &t);

// Repaint whatever needs repainting. `force` redraws all four panels.
void ui_paint(uint32_t now, const RtcTime &t, bool force);

// True while the configured night window is in effect.
bool ui_is_night(const RtcTime &t);
// True while the daily screens-off schedule says the glass should be dark.
// Separate from the night window on purpose: night DIMS from the hour the room
// gets dark, and full-off belongs later, when nobody is looking at all.
bool ui_is_screens_off(const RtcTime &t);

// Inject a button press from the serial interface. Same path as a real press,
// so it exercises the actual state machine rather than a parallel one, which
// is what makes it a usable substitute while SW1-SW4 are unfitted. The long
// press must be injectable too, because holding SET is the ONLY way into the
// settings page and there is no switch to hold.
void ui_button(uint8_t i, bool long_press = false);

uint8_t ui_page();          // which page is showing, for the serial report
uint8_t ui_variant();
uint8_t ui_set_cursor();    // which settings item is selected, for the report
void    ui_force_repaint();
// Serial `W`: "# SW state=<zero|run|pause> ms=<elapsed> page=<PG_ index>"
void    ui_stopwatch_report();
