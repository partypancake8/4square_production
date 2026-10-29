// defaults.cpp — the factory settings, kept PURE and separate.
//
// This is in its own translation unit so the host-side layout prover can link
// it without pulling in Wire and the rest of store.cpp. The defaults are also
// the thing most worth reading on its own: they are the device as it ships.
#include "store.h"
#include "../demo.h"   // DEMO_BUILD: the demo's own defaults
#include "../screens/faces.h"
#include "../screens/display.h"   // SHIFT_MIN / SHIFT_MAX bound shift_amp
#include <string.h>

void settings_defaults(Settings &s) {
  memset(&s, 0, sizeof(s));
  s.version = CFG_VERSION;
  s.hour24  = 0;                 // 12-hour, as asked for
  // The classic layout, which is what the device has always shown:
  // hours top-left, minutes (with seconds tucked in the corner) top-right,
  // weekday bottom-left, date bottom-right.
  s.slot_widget[SLOT_TL] = W_HOUR;    s.slot_style[SLOT_TL] = S_OUTLINE;
  s.slot_widget[SLOT_TR] = W_MINUTE;  s.slot_style[SLOT_TR] = S_OUTLINE;
  s.slot_widget[SLOT_BL] = W_WEEKDAY; s.slot_style[SLOT_BL] = S_OUTLINE;
  s.slot_widget[SLOT_BR] = W_DATE;    s.slot_style[SLOT_BR] = S_OUTLINE;
  s.slot_overlay[SLOT_TR] = OV_SECONDS;
  // 140 is the day cap from design/anti-burn-in.md. Contrast is a linear
  // segment-current control and OLED lifetime goes as roughly J^-1.4, so the
  // cap IS the lifetime knob, not a comfort setting.
  s.bright_day   = 140;
  // 30, not 1. 1 is technically visible and practically a black screen —
  // "make it a little brighter at lower light intensities".
  s.bright_night = 30;
  s.autodim      = 1;
  s.dim_sens     = DIM_SENS_MED;
  s.night_mode    = 1;           // dim overnight, do not blank
  s.night_start_h = 22; s.night_start_m = 30;
  s.night_end_h   = 7;  s.night_end_m   = 0;
  s.shift_secs   = 60;
  // 2 WAS A PLACEBO. An envelope narrower than the glyph stroke cannot move a
  // stroke off its own interior: peak per-pixel duty is 0.667 at both +/-0 and
  // +/-2. SHIFT_MIN is now 4 and the default is the full 6 (1.51x on the hours
  // screen, 2.19x on the static ones). salvage/09 section 3.1.
  s.shift_amp    = 6;
  s.led_mode     = 1;            // status only; idle is dark
  s.led_bright   = 200;
  s.temp_unit    = 0;
  s.wifi_on      = 1;
#ifdef DEMO_BUILD
  // A DEMO UNIT SHIPS IN FAHRENHEIT, BRIGHT, AND WITH NO RADIO. The reasoning
  // for each — including why the brightness number is a deliberate panel-
  // lifetime trade that must NOT be copied back into the shipping defaults —
  // is in ../demo.h, the one file that defines what a demo build is.
  //
  // This branch only covers a board whose settings ring is blank or corrupt.
  // A board that has been used before comes up on its STORED record, which is
  // why demo_force() below exists and runs after settings_load().
  s.temp_unit    = DEMO_TEMP_UNIT;
  s.bright_day   = DEMO_BRIGHT_DAY;
  s.bright_night = DEMO_BRIGHT_NIGHT;
  s.led_bright   = DEMO_LED_BRIGHT;
  s.wifi_on      = DEMO_WIFI_ON;
#endif
  // The daily screens-off schedule ships ENABLED: panels off 23:30-06:30 every
  // day. The night window above dims from 22:30 because that is when the room
  // gets dark; full-off belongs later, when nobody is looking at it at all.
  // These are factory defaults only: a record already stored in the EEPROM
  // ring wins over them, and the AUTO OFF item on the settings page turns the
  // schedule off if you would rather the clock stayed lit.
  s.off_enable   = 1;
  s.off_start_h  = 23; s.off_start_m = 30;
  s.off_end_h    = 6;  s.off_end_m   = 30;
}

#ifdef DEMO_BUILD
// ---- THE DEMO'S PROMISES, RE-ASSERTED OVER A STORED RECORD -----------------
// settings_defaults() above only runs when the ring is blank or corrupt. The
// 24LC256 SURVIVES A FLASH -- that is the whole reason settings live on a
// separate chip -- so a board that has been used before comes up on ITS
// record and would show Celsius at whatever brightness it was left on, which
// is exactly the thing a demo cannot afford to be a coin flip.
//
// So this runs immediately after settings_load() and re-asserts ONLY the
// fields the demo actually promises: unit, brightness, and that the radio is
// down. Everything else -- the clock style, the shift amplitude, the night
// window -- is left exactly as stored, so the unit stays adjustable from the
// settings page during the demo itself.
//
// It deliberately does NOT save. Forcing at boot and leaving the ring alone
// means the demo build never rewrites the user's settings behind their back.
void demo_force(Settings &s) {
  s.temp_unit    = DEMO_TEMP_UNIT;
  s.bright_day   = DEMO_BRIGHT_DAY;
  s.bright_night = DEMO_BRIGHT_NIGHT;
  s.led_bright   = DEMO_LED_BRIGHT;
  s.wifi_on      = DEMO_WIFI_ON;
}
#endif

// EVERY BYTE THAT COMES BACK FROM THE EEPROM IS UNTRUSTED.
//
// slot_widget, slot_style and slot_overlay are used directly as array indices
// into the name tables and the render dispatch. A socketed chip that was never
// programmed, a chip pulled from another project, or a record whose CRC
// happens to pass on garbage would otherwise index off the end of a table —
// and on a C3 with no memory protection that is a silent corruption that
// surfaces as an unrelated crash days later.
//
// A CRC proves the bytes arrived intact. It says nothing about whether they
// were ever meaningful. This is the part that checks that.
void settings_sanitize(Settings &s) {
  Settings d;
  settings_defaults(d);

  for (uint8_t i = 0; i < N_SCREENS; i++) {
    if (s.slot_widget[i]  >= W_COUNT)  s.slot_widget[i]  = d.slot_widget[i];
    if (s.slot_style[i]   >= S_COUNT)  s.slot_style[i]   = d.slot_style[i];
    if (s.slot_overlay[i] >= OV_COUNT) s.slot_overlay[i] = d.slot_overlay[i];
    // A style the widget cannot render is in range but still wrong.
    if (!widget_allows(s.slot_widget[i], s.slot_style[i])) {
      s.slot_style[i] = S_OUTLINE;
      for (uint8_t st = 0; st < S_COUNT; st++)
        if (widget_allows(s.slot_widget[i], st)) { s.slot_style[i] = st; break; }
    }
  }

  if (s.hour24    > 1) s.hour24    = d.hour24;
  if (s.temp_unit > 1) s.temp_unit = d.temp_unit;
  if (s.autodim   > 1) s.autodim   = d.autodim;
  if (s.wifi_on   > 1) s.wifi_on   = d.wifi_on;
  if (s.night_mode > 2) s.night_mode = d.night_mode;
  if (s.led_mode   > 1) s.led_mode   = d.led_mode;

  if (s.night_start_h > 23) s.night_start_h = d.night_start_h;
  if (s.night_end_h   > 23) s.night_end_h   = d.night_end_h;
  if (s.night_start_m > 59) s.night_start_m = d.night_start_m;
  if (s.night_end_m   > 59) s.night_end_m   = d.night_end_m;

  // ZERO IS THE UPGRADE CASE, not a legal value. A record written by
  // CFG_VERSION 1 is the same length as this struct, so slot_read()'s overlay
  // copies version 1's reserved zeros straight over dim_sens. Clamping "0 or
  // out of range" to the default is what makes an in-place field addition
  // safe; treating 0 as a sensitivity would divide the dimming window by zero.
  if (s.dim_sens == 0 || s.dim_sens >= DIM_SENS_COUNT) s.dim_sens = d.dim_sens;

  if (s.off_enable  > 1)  s.off_enable  = 0;
  if (s.off_start_h > 23) s.off_start_h = d.off_start_h;
  if (s.off_end_h   > 23) s.off_end_h   = d.off_end_h;
  if (s.off_start_m > 59) s.off_start_m = d.off_start_m;
  if (s.off_end_m   > 59) s.off_end_m   = d.off_end_m;
  // A zero-length window is not a schedule, it is a device that never sleeps —
  // and, read the other way by a naive comparison, one that never wakes. The
  // window code treats start == end as "no window", so an enable flag left on
  // with equal times would silently do nothing. Refuse the state instead.
  if (s.off_enable &&
      s.off_start_h == s.off_end_h && s.off_start_m == s.off_end_m)
    s.off_enable = 0;

  // A zero or absurd shift period would either thrash the panels or stop the
  // wander entirely, which is the mitigation this whole design rests on.
  if (s.shift_secs < 10 || s.shift_secs > 240) s.shift_secs = d.shift_secs;
  // The floor is 4, not 1. 1..3 are the dead zone where the envelope is
  // narrower than the stroke and the shift relieves nothing measurable -- a
  // setting that reads as "on" while doing nothing is worse than no setting.
  // A stored 1..3 from a firmware B device is silently promoted to the default.
  if (s.shift_amp  < SHIFT_MIN || s.shift_amp > SHIFT_MAX) s.shift_amp = d.shift_amp;

  // A stored brightness of 0 is a black screen the user cannot see well
  // enough to navigate back out of. Never allow one to be loaded.
  if (s.bright_day   < 20) s.bright_day   = d.bright_day;
  if (s.bright_night < 5)  s.bright_night = d.bright_night;
  if (s.bright_night > s.bright_day) s.bright_night = s.bright_day;
  if (s.led_bright   < 10) s.led_bright   = d.led_bright;
}
