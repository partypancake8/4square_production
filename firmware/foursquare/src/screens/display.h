// display.h, the ONLY way anything reaches a panel.
//
// ===========================================================================
// THIS FILE IS THE ANTI-BURN-IN ENFORCEMENT POINT.
// ===========================================================================
// Every pixel that has ever been lit on this device passed through
// disp_commit(). That is not a convention, it is a structural fact:
//
//   * The four Adafruit_SSD1306 objects have INTERNAL LINKAGE in display.cpp.
//     Nothing outside that translation unit can name them, so nothing outside
//     it can write to a panel.
//   * Drawing code is handed a GFXcanvas1, plain RAM. It cannot reach I2C.
//   * The shift, the brightness cap and the night policy are applied during
//     the blit from canvas to panel, after all drawing is finished.
//
// So a new clock face cannot forget to be burn-in safe, and cannot opt out.
// The one thing a layout CAN get wrong is drawing outside the safe area, and
// that is what tools/layoutcheck proves it does not, on the host, from the
// same source, before it ever runs.
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "../config.h"

// ---- the safe area --------------------------------------------------------
// The image wanders within a +/-SHIFT_MAX box to stop any pixel being lit in
// the same place for the life of the device. Ink outside the safe area would
// be clipped at the extremes of that wander, so the safe area is a hard
// contract on every layout, asserted on the host.
//
//   panel   128 x 64, of which only 60 rows are visible through the case
//   shift   +/-6 px in x, +/-4 px in y
//   bias    +/-2 px in y, per slot, measured (slot_bias.h)
//   safe    116 x 52 at (6, 6)  ->  x in [6,121], y in [6,57] inclusive
static const int16_t SHIFT_MAX = 6;

// AND A FLOOR, WHICH IS NOT COSMETIC. A shift envelope narrower than the glyph
// stroke cannot move a stroke off a pixel in its own interior, so it relieves
// nothing at all. At text size S the stroke is S px wide; +/-2 spans 5 columns
// against a size-6 stroke, and simulated peak per-pixel duty is 0.667 at both
// +/-0 and +/-2 -- identical to three decimals. firmware B shipped a DEFAULT of
// 2, i.e. the anti-burn-in shift was a placebo at its default setting, and
// three of the six settable values were in that dead zone.
//   salvage/09-oled-burnin-techniques.md section 3.1
static const int16_t SHIFT_MIN = 4;   // +/-4 = 1.24x, +/-6 = 1.51x, +/-2 = 1.00x
// ---- and the per-slot vertical bias ---------------------------------------
// MEASURED, not chosen: slot_bias.h is generated from what the user could
// actually see through the assembled case. It costs the safe area BIAS_MAX at
// the top and bottom, because a slot's fixed bias and the wandering shift can
// point the same way at once, and ink at the edge of the safe area would then
// clip. Y ONLY -- horizontal loss measured zero on all four panels.
#include "slot_bias.h"

// THE VERTICAL WANDER IS SMALLER THAN THE HORIZONTAL ONE, because the case
// only shows 60 of the 64 rows. The vertical budget is exact and it is fully
// spent: safe area 52 + bias 2 at each end + wander 4 at each end = 60. All
// 128 columns are visible, so x keeps the full +/-6 and loses nothing.
// Taking the bias out of the safe area instead would have cut it to 48 rows
// and broken every layout in the build; taking it out of the vertical wander
// costs burn-in relief and nothing else, and the relief gate still has to pass.
static const int16_t SHIFT_MAX_Y = 4;

static const int16_t SAFE_X0 = SHIFT_MAX;
static const int16_t SAFE_Y0 = SHIFT_MAX_Y + BIAS_MAX;             // 6
static const int16_t SAFE_X1 = SCR_W - 1 - SHIFT_MAX;              // 121
static const int16_t SAFE_Y1 = SCR_H - 1 - SHIFT_MAX_Y - BIAS_MAX; // 57
static const int16_t SAFE_W  = SAFE_X1 - SAFE_X0 + 1;              // 116
static const int16_t SAFE_H  = SAFE_Y1 - SAFE_Y0 + 1;              // 52

void  disp_begin();
bool  disp_ok(uint8_t i);
uint8_t disp_present_mask();
// Probe every channel and update which panels are actually answering.
// WITHOUT THIS, the display path is completely un-instrumented: Adafruit's
// display() and ssd1306_command() discard every endTransmission() result, so
// four dead panels produced zero counted errors, never lit the fault LED and
// could never reach the recovery ladder. panel_ok[] was a boot-time snapshot
// being read as if it were live health.
// It also revalidates the mux, whose selected-channel cache is otherwise
// trusted forever, a PCA9548A that loses its register to a glitch would
// freeze all four screens on the last good frame with nothing noticing.
// Returns true if anything changed and the screens need a repaint.
bool  disp_health_check();

// The draw target. Layout code gets this and nothing else.
GFXcanvas1 &disp_canvas(uint8_t i);

// Push the given screens. Applies shift, rotation and the brightness policy.
// DIRTY COMMITS: a panel whose post-transform bytes are unchanged since the
// last push is skipped, a memcmp against a shadow copy costs microseconds
// against ~11-23 ms of bus per panel. The compare sits AFTER the shift/bias
// application, so a burn-in shift step dirties everything and full-pushes
// automatically.
void  disp_commit(uint8_t mask);
// The measured cost of the last commit and running totals, for the `?`
// report: last call's duration and pushed/asked panel counts, the worst
// duration seen, and how many panel-pushes were made vs skipped since boot.
void  disp_commit_stats(uint32_t &last_us, uint32_t &max_us,
                        uint8_t &last_pushed, uint8_t &last_asked,
                        uint32_t &pushed_total, uint32_t &skipped_total);

// Ambient light in raw ADC counts, already sampled with the LEDs blanked.
void  disp_set_ambient(uint16_t raw);
// The ambient window the auto-dim curve spans, AFTER cfg.dim_sens has widened
// or narrowed it. Exported so leds.cpp scales its own brightness against the
// same window the panels do: the LEDs and the screens have to agree about how
// bright the room is, and two copies of the constants would drift the moment
// the sensitivity setting moved one of them.
void  disp_light_window(uint16_t &lo, uint16_t &hi);
void  disp_set_night(bool night);
uint8_t disp_contrast();
// Re-apply the contrast NOW, ignoring the deadband. The deadband is 8 and the
// brightness step is 5, so without this a single button press in the editor
// changed nothing visible and the "live preview" only moved on the second one.
void  disp_refresh();
void  disp_pin_contrast(int16_t c);      // <0 hands auto-dim back; serial 'D'

// Advance the burn-in shift. Cheap; call it once a second.
void  disp_burnin_tick(uint32_t now_s);
int8_t disp_shift_x();
int8_t disp_shift_y();

// MECHANISM ONLY. Whether the glass SHOULD be dark is not decided here, the
// night window and the daily screens-off schedule can both want it, and one
// owner for that is ui.cpp's blank policy. This layer just does it.
void  disp_all_off(bool off);
// Is the panel actually dark right now? The UI needs the REAL state, not the
// night_mode setting, gating a button on the setting instead of the state is
// what made "night mode = blank" swallow every press at noon.
bool  disp_is_blanked();
void  disp_rescan();
void  disp_flip(int which);              // serial 'R'
extern uint8_t OLED_ROT[N_SCREENS];
