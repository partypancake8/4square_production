#include "display.h"
#include "shift_tour.h"
#include "blit_map.h"
#include "../settings/store.h"
#include "../board/bus.h"
#include <Adafruit_SSD1306.h>

// EVERYTHING IN THIS BLOCK IS FILE-LOCAL ON PURPOSE. These are the only
// objects on the device that can talk to a panel, and `static` is what makes
// "all drawing goes through disp_commit()" a fact the linker enforces rather
// than a rule someone has to remember.
//
// THE PANEL CLOCK, IN ONE PLACE. 800 kHz is 2x the SSD1306 datasheet figure,
// which these modules are commonly run at and well past on short traces  - 
// this board is 74 mm end to end with proper pull-ups (R1/R2), so the edges
// have every chance of surviving. It is an EXPERIMENT with a bench soak
// standing behind it, not a datasheet number: if the soak (or any later `?`
// report) shows I2C errors or visual corruption, set this back to 400000UL  - 
// this one constant is the whole change.
//
// THE PANELS ARE THE ONLY THING THAT RUNS AT THIS SPEED. clkAfter (below)
// hands the bus back at BUS_PERIPH_HZ, the same 400 kHz bus_begin() sets  - 
// because the DS3231, 24LC256 and PCA9548A are 400 kHz parts by datasheet.
// Overclocking the panel payload is an experiment the soak can retire;
// running the RTC out of spec between commits would be a defect, and one
// with no symptom until a corrupted read.
static const uint32_t OLED_BUS_HZ    = 800000UL;
static const uint32_t BUS_PERIPH_HZ  = 400000UL;   // = bus_begin()'s setClock
// THE LAST TWO ARGUMENTS ARE NOT OPTIONAL HERE.
// Adafruit_SSD1306's signature is (w, h, twi, rst, clkDuring, clkAfter) and
// clkAfter DEFAULTS TO 100000. The library restores that clock at the end of
// every display(), every ssd1306_command() and every begin(), so the
// four-argument form means bus_begin()'s Wire.setClock(400000) holds only
// until the first panel write, and the DS3231, SHT31, EEPROM and mux then run
// at 100 kHz for the life of the device. The panel payload itself stays at
// the ctor clock, so there is no visible symptom at all; it just quietly
// slows every other transaction on the bus.
static Adafruit_SSD1306 panel[N_SCREENS] = {
  Adafruit_SSD1306(SCR_W, SCR_H, &Wire, -1, OLED_BUS_HZ, BUS_PERIPH_HZ),
  Adafruit_SSD1306(SCR_W, SCR_H, &Wire, -1, OLED_BUS_HZ, BUS_PERIPH_HZ),
  Adafruit_SSD1306(SCR_W, SCR_H, &Wire, -1, OLED_BUS_HZ, BUS_PERIPH_HZ),
  Adafruit_SSD1306(SCR_W, SCR_H, &Wire, -1, OLED_BUS_HZ, BUS_PERIPH_HZ)};
static bool panel_ok[N_SCREENS] = {false, false, false, false};

// ---- the dirty-commit shadows ---------------------------------------------
// One 1 KB copy per panel of the POST-TRANSFORM bytes last pushed over the
// bus. A push costs ~23 ms at 400 kHz and a memcmp costs microseconds, so a
// panel whose bytes have not changed is simply not pushed, which is what
// lets the reel's walker and weather segments tick at ~16 fps while only the
// panel that changed pays the bus.
//
// The compare happens AFTER blit() has applied rotation, burn-in shift and
// per-slot bias, so a shift step changes every panel's bytes and naturally
// forces a full push that tick; nothing has to remember to special-case it.
// The shadow is INVALID (never trusted) wherever GDDRAM or the transform can
// change behind our back: a (re)scan re-initialises the controller, a flip
// changes the transform, a panel that reappears was power-cycled. A blank
// (DISPLAYOFF) is deliberately NOT one of those, it stops the scan but
// preserves GDDRAM, so on wake the glass still holds exactly the shadowed
// bytes.
static uint8_t shadow[N_SCREENS][(SCR_W * SCR_H) / 8];
static bool    shadow_valid[N_SCREENS] = {false, false, false, false};
static inline void shadow_drop(uint8_t i) { shadow_valid[i & 3] = false; }
static void shadow_drop_all() {
  for (uint8_t i = 0; i < N_SCREENS; i++) shadow_valid[i] = false;
}

// Commit accounting, for the `?` report: the claim "dirty commits made the
// page faster" has to be a measured number, not a belief.
static uint32_t commit_last_us = 0, commit_max_us = 0;
static uint8_t  commit_last_pushed = 0, commit_last_asked = 0;
static uint32_t commit_pushed_total = 0, commit_skipped_total = 0;

// The draw targets. 4 x 1 KB of plain RAM, allocated once and never freed  - 
// a 1 KB malloc/free per repaint would fragment the heap on a device that is
// expected to run for months without a reboot.
static GFXcanvas1 canvas[N_SCREENS] = {
  GFXcanvas1(SCR_W, SCR_H), GFXcanvas1(SCR_W, SCR_H),
  GFXcanvas1(SCR_W, SCR_H), GFXcanvas1(SCR_W, SCR_H)};

// THE TOP ROW IS UPSIDE DOWN RELATIVE TO THE BOTTOM ROW. All four modules are
// mounted with their pin header facing the board's centreline. Reasoned from
// the footprint, then CONFIRMED BY EYE on the assembled board 2026-08-07.
// Rotation is applied during the blit, so layout code never thinks about it.
uint8_t OLED_ROT[N_SCREENS] = {2, 2, 0, 0};

static int8_t   sh_x = 0, sh_y = 0;
static uint32_t last_shift_s = 0;
static uint16_t ambient_raw  = 0;
static bool     is_night     = false;
static uint8_t  cur_contrast = 0xFF;      // 0xFF = never set
static int16_t  pinned       = -1;        // serial 'D n'
static uint32_t light_avg    = 0;         // x16 fixed point
static bool     blanked      = false;

// ============================================================ brightness ==
// THREE REGISTERS, NOT ONE. The contrast register (0x81) alone barely changes
// what the eye sees on these panels, it scales segment current, but the
// perceived floor stays high. The two that actually move brightness with it
// are the PRECHARGE period (0xD9) and the VCOMH deselect level (0xDB).
// Driven together the panel goes from clearly bright to a faint glow; driven
// apart, 255 and 1 look nearly the same, which is exactly the "dimming isn't
// working" symptom this cost a day to find.
//
// Contrast is also the LIFETIME knob, not a comfort setting: it is a linear
// segment-current control (ISEG = C/256 x IREF x 8) and OLED half-life goes
// as roughly J^-n. THE EXPONENT IS 1.82, NOT THE 1.4 THIS FILE USED TO CLAIM:
// 1.4 came from a simulation of a different emitter, whereas 1.82 is the
// median of the panel makers' own tiered lifetime-vs-brightness tables, and
// measured literature converges on 1.4-2.0. Dimming is worth MORE than we
// credited it -- the day cap is 3.6x, not 2.7x, and 140 -> 120 buys 1.32x for
// a dim nobody can see. The panel behind these modules (Univision
// UG-2864HSWEG01, doc SAS1-9046-B rev B section 5.2) is rated 10,000 h min to
// half brightness at 100 cd/m2 on a 50% checkerboard, and this one runs 24/7,
// which is 8,760 h a year.
//
// NOTE ON 0xD9 BELOW. Precharge does not change peak segment current, but it
// does change the charge delivered per row period, so it scales EMISSION DUTY
// and therefore lifetime too. The wear model's flat 1/64 duty constant is
// wrong: real per-pixel duty is (1/64)(50/K), which makes the night branch
// here run ~22% hotter per unit of contrast than the day branch.
//   salvage/09-oled-burnin-techniques.md sections 1.2 and 2.2
static void write_contrast(uint8_t c) {
  uint8_t pre  = (c > 110) ? 0xF1 : (c > 70) ? 0xC1 : (c > 40) ? 0x61 : 0x22;
  uint8_t vcom = (c > 110) ? 0x30 : (c > 70) ? 0x20 : (c > 40) ? 0x10 : 0x00;
  for (uint8_t i = 0; i < N_SCREENS; i++) {
    if (!panel_ok[i]) continue;
    mux_select(OLED_CH[i]);
    panel[i].ssd1306_command(SSD1306_SETPRECHARGE);  panel[i].ssd1306_command(pre);
    panel[i].ssd1306_command(SSD1306_SETVCOMDETECT); panel[i].ssd1306_command(vcom);
    panel[i].ssd1306_command(SSD1306_SETCONTRAST);   panel[i].ssd1306_command(c);
  }
  mux_off();
  cur_contrast = c;
}

// ---- how hard the room pulls on the brightness -----------------------------
// SENSITIVITY IS THE WIDTH OF THE WINDOW, NOT A GAIN. LIGHT_RAW_LO..HI is the
// span of ambient readings the curve maps onto bright_night..bright_day, and
// the honest way to say "respond more" is to make that span narrower: the same
// change in the room then covers more of it. A multiplier on the output would
// have done something quite different, it would have fought the two caps,
// which exist for panel lifetime and are not this setting's to move.
//
// The window is scaled about its MIDPOINT so the sensitivity knob does not
// also quietly re-centre what counts as "a lit room"; only the steepness of
// the ramp between dark and lit changes.
void disp_light_window(uint16_t &lo, uint16_t &hi) {
  // Indexed by DimSens. Index 0 is unreachable, settings_sanitize() maps a
  // zero (which is what a CFG_VERSION 1 record supplies) to MED, but it is
  // filled in with the MED value anyway so a future path that skipped the
  // sanitizer degrades to the default instead of collapsing the window.
  static const uint16_t WIDTH_PCT[DIM_SENS_COUNT] = {100, 210, 100, 55, 28};
  uint8_t s = cfg.dim_sens;
  if (s >= DIM_SENS_COUNT) s = DIM_SENS_MED;

  const uint32_t mid  = ((uint32_t)LIGHT_RAW_LO + LIGHT_RAW_HI) / 2;
  const uint32_t half = ((uint32_t)LIGHT_RAW_HI - LIGHT_RAW_LO) / 2;
  uint32_t h = half * WIDTH_PCT[s] / 100;
  // A window narrower than this is a switch, not a dimmer, and the subtraction
  // in the interpolation below would be dividing by a handful of ADC counts
  // that the sensor's own noise covers.
  if (h < 6) h = 6;

  lo = (uint16_t)(mid > h ? mid - h : 0);
  hi = (uint16_t)(mid + h);
}

// The cap is the lower of what the room asks for and what the hour allows.
// Night is a HARD CAP, not a target: no amount of light in the room raises
// the panel above the night ceiling between the configured hours.
static uint8_t target_contrast() {
  uint8_t lo = cfg.bright_night, hi = cfg.bright_day;
  if (hi < lo) hi = lo;
  if (!cfg.autodim) return hi;

  uint16_t wlo, whi;
  disp_light_window(wlo, whi);

  uint16_t sm = (uint16_t)(light_avg >> 4);
  uint32_t c;
  if (sm <= wlo)      c = lo;
  else if (sm >= whi) c = hi;
  else c = lo + (uint32_t)(sm - wlo) * (hi - lo) / (whi - wlo);

  // NIGHT_CAP is 24 from design/anti-burn-in.md, a ~9 uA/segment operating
  // point. Overnight is when the display is least looked at and most lit, so
  // it is where the lifetime budget is won or lost.
  if (is_night && cfg.night_mode >= 1) {
    const uint8_t NIGHT_CAP = 24;
    if (c > NIGHT_CAP) c = NIGHT_CAP;
  }
  return (uint8_t)c;
}

void disp_set_ambient(uint16_t raw) {
  // SMOOTH FIRST. The bare reading wanders +/-10 counts sample to sample,
  // which through the curve came out as the contrast hunting between 154 and
  // 172 five times a second, visible flicker, and four I2C writes each time
  // for a change nobody can see. An exponential average over ~2 s of samples
  // plus a wide deadband turns it into the slow drift that "the room got
  // darker" actually looks like.
  ambient_raw = raw;
  if (light_avg == 0) light_avg = (uint32_t)raw << 4;
  else                light_avg = light_avg - (light_avg >> 3) + raw * 2;

  if (pinned >= 0) return;
  uint8_t c = target_contrast();
  const uint8_t DEADBAND = 8;
  if (cur_contrast == 0xFF ||
      c > cur_contrast + DEADBAND || c + DEADBAND < cur_contrast)
    write_contrast(c);
}

void disp_set_night(bool night) {
  if (night == is_night) return;
  is_night = night;
  cur_contrast = 0xFF;                    // force the cap to be applied now
  if (pinned < 0) write_contrast(target_contrast());
  // BLANKING IS NOT DECIDED HERE ANY MORE. It used to be: night_mode 2 called
  // disp_all_off() from this function. That was fine while the night window
  // was the only thing that could darken the glass, and became a bug the
  // moment the daily screens-off schedule became a second one, two owners
  // toggling the same flag from different clocks, so whichever ran last won
  // and a wake inside one window was undone by the other.
  //
  // ui.cpp now owns the policy in a single place (ui_blank_policy) and this
  // layer keeps only the mechanism, disp_all_off()/disp_is_blanked(). The
  // repaint-on-wake obligation moved with it: while blanked, disp_commit()
  // returns without writing but ui_paint() still updates its last_min/last_sec
  // bookkeeping as though the frame had reached the glass, so un-blanking
  // without forcing a repaint lights the panels showing the image physically
  // written BEFORE the blank, up to a minute of a confidently wrong clock.
}

uint8_t disp_contrast() { return cur_contrast; }

void disp_refresh() {
  if (pinned >= 0) return;
  write_contrast(target_contrast());
}

void disp_pin_contrast(int16_t c) {
  pinned = c;
  if (c >= 0) write_contrast((uint8_t)c);
  else { cur_contrast = 0xFF; write_contrast(target_contrast()); }
}

// ================================================================= shift ==
// A HAMILTONIAN TOUR OF THE OFFSET GRID, not a random walk.
//
// WHY THE RANDOM WALK WENT. firmware B's walk was biased, and badly:
//
//     int8_t dx = (r & 3) - 1;   // 0,1,2,3 -> -1, 0, +1, +2
//     if (dx > 1) dx = -1;       // the +2 folded back onto -1
//
// Two of the four equally-likely outcomes were -1, so P(-1)=1/2, P(0)=1/4,
// P(+1)=1/4 and the mean step was -0.25 px. That is not a walk, it is a slide:
// from centre it reached the -6 wall in ~24 ticks and stayed pinned, because
// leaving meant winning at 2:1 odds every step. The stationary distribution is
// geometric with ratio 1/2, so the corner (-6,-6) alone took 25% of all time
// and the far corner about one part in 16 million. Effective spread was ~9 of
// the 169 offsets -- a 19x shortfall against the uniform box the file's own
// comment claimed. Unbiasing the step fixes the drift and gives a uniform
// distribution in EXPECTATION, but a walk still clumps: over one evening the
// image is parked in a neighbourhood, since a lazy walk over 13 states has a
// relaxation time of ~(2A+1)^2/pi^2 ~ 17 steps and takes hours to mix.
//
// SO WE DO NOT LEAVE IT TO CHANCE. A boustrophedon tour visits every offset
// EXACTLY once per pass, so residency is exactly uniform per cycle rather than
// uniform on average -- no clumping, no variance, nothing to get unlucky with.
// The archived spec rejected "a cycle" because "a repeating path would still
// leave the long-run average brightest at the positions it visits most". That
// is true of a Lissajous or a LUT path and FALSE BY CONSTRUCTION of a
// Hamiltonian tour, which by definition weights every offset identically.
//
// AND IT IS TRAVERSED THERE AND BACK. A (2A+1)^2 grid is odd x odd, which by a
// parity argument admits no Hamiltonian CYCLE -- only a path. Running the path
// forward then backward keeps every step 1 px (no jump home) and still visits
// every offset exactly twice per 2*M cycle, so uniformity is exact either way.
// At +/-6 and 1 px/60 s a full cycle is 338 min. gcd(338, 1440) = 2, so the
// tour does not sit in step with the day.
//
// One pixel per event stays deliberate: a larger jump reads as the image
// twitching, and the point is that nobody ever notices this happening.
//   salvage/09-oled-burnin-techniques.md section 3.1
static uint16_t tour_i      = 0;
static bool     tour_seeded = false;

void disp_burnin_tick(uint32_t now_s) {
  uint8_t period = cfg.shift_secs ? cfg.shift_secs : 60;
  if (last_shift_s && (now_s - last_shift_s) < period) return;
  last_shift_s = now_s;

  int16_t amp = (int16_t)cfg.shift_amp;
  if (amp < SHIFT_MIN) amp = SHIFT_MIN;
  if (amp > SHIFT_MAX) amp = SHIFT_MAX;

  // ONE SETTING, TWO AMPLITUDES. The setting names the horizontal wander; the
  // vertical one is capped at SHIFT_MAX_Y because the case hides four rows and
  // the vertical budget is already exactly spent (see display.h). Capping here
  // rather than clamping the setting means the setting keeps its meaning on a
  // future build whose case shows all 64 rows.
  const int16_t amp_x = amp;
  const int16_t amp_y = amp < SHIFT_MAX_Y ? amp : SHIFT_MAX_Y;

  const uint16_t len = shift_tour_len(amp_x, amp_y);

  // Start somewhere random so a shelf of these does not age in lockstep, and
  // so a device that reboots often does not keep restarting at one corner.
  // esp_random() is a true hardware RNG here, unlike a seeded PRNG.
  if (!tour_seeded) { tour_i = (uint16_t)(esp_random() % len); tour_seeded = true; }

  shift_tour_at(tour_i, amp_x, amp_y, &sh_x, &sh_y);
  tour_i = (uint16_t)((tour_i + 1) % len);
}

int8_t disp_shift_x() { return sh_x; }
int8_t disp_shift_y() { return sh_y; }

// ================================================================= commit ==
// Canvas -> panel. Three things happen here and nowhere else: the burn-in
// shift, the 180 degree rotation of the top row, and the format change from
// the canvas's row-major bitmap to the SSD1306's page-major GDDRAM.
//
// The vertical half of the shift COULD be done for free in hardware with
// SETDISPLAYOFFSET (0xD3). It is done in software anyway, because 0xD3 wraps
// rather than clips, its sign inverts on the two rotated panels, and
// Adafruit's begin() resets it to zero behind our back. Doing both axes the
// same way costs about 0.2 ms per screen and removes three ways to be subtly
// wrong. Note that "its sign inverts on the two rotated panels" was true of
// this function too until 2026-08-11 -- see the ordering note below.
static void blit(uint8_t i) {
  const uint8_t *src = canvas[i].getBuffer();
  uint8_t       *dst = panel[i].getBuffer();
  if (!src || !dst) return;
  const int16_t stride = (SCR_W + 7) / 8;          // 16 bytes per canvas row
  const bool flip = (OLED_ROT[i] == 2);

  memset(dst, 0, (SCR_W * SCR_H) / 8);

  // THE MAPPING LIVES IN blit_map.h, and so does the reasoning about which
  // order it applies things in. THIS COMMENT USED TO SAY "ROTATE FIRST, THEN
  // SHIFT", which was wrong and briefly shipped: canvas space IS the viewer's
  // space, the rotation exists to cancel the mounting, and shifting after it
  // let the mounting negate the shift -- the top row slid down while the
  // bottom row slid up. Caught by eye, not by this file. The header carries
  // the full account; do not restate it here, where it drifted out of step
  // with the code sitting directly beneath it.
  //
  // Each slot also carries its own measured vertical bias, so the image is
  // centred on what the eye can SEE through the case rather than on the middle
  // of the frame buffer. Both displacements are in the viewer's frame.
  const int16_t bias_y = SLOT_BIAS_Y[i & 3];
  for (int16_t y = 0; y < SCR_H; y++) {
    const uint8_t *row = src + (int32_t)y * stride;
    for (int16_t x = 0; x < SCR_W; x++) {
      if (!(row[x >> 3] & (0x80 >> (x & 7)))) continue;
      int16_t px, py;
      if (!blit_map(x, y, flip, sh_x, sh_y, bias_y, &px, &py)) continue;
      dst[px + (py >> 3) * SCR_W] |= (uint8_t)(1 << (py & 7));
    }
  }
}

void disp_commit(uint8_t mask) {
  if (blanked) return;                    // night blank / screensaver
  const uint32_t t0 = micros();
  uint8_t asked = 0, pushed = 0;
  for (uint8_t i = 0; i < N_SCREENS; i++) {
    if (!panel_ok[i] || !(mask & (1 << i))) continue;
    asked++;
    blit(i);
    // THE DIRTY CHECK, on the post-transform bytes, the exact image the
    // controller would receive. Identical bytes on an initialised panel mean
    // the glass already shows this frame; pushing it again buys nothing and
    // costs a panel-time of bus.
    uint8_t *out = panel[i].getBuffer();
    if (!out) continue;
    if (shadow_valid[i] && memcmp(out, shadow[i], sizeof shadow[i]) == 0) {
      commit_skipped_total++;
      continue;
    }
    mux_select(OLED_CH[i]);
    panel[i].display();
    memcpy(shadow[i], out, sizeof shadow[i]);
    shadow_valid[i] = true;
    pushed++;
    commit_pushed_total++;
  }
  if (pushed) mux_off();
  commit_last_us = micros() - t0;
  commit_last_pushed = pushed;
  commit_last_asked  = asked;
  if (commit_last_us > commit_max_us) commit_max_us = commit_last_us;
}

void disp_commit_stats(uint32_t &last_us, uint32_t &max_us,
                       uint8_t &last_pushed, uint8_t &last_asked,
                       uint32_t &pushed_total, uint32_t &skipped_total) {
  last_us = commit_last_us;   max_us = commit_max_us;
  last_pushed = commit_last_pushed; last_asked = commit_last_asked;
  pushed_total = commit_pushed_total; skipped_total = commit_skipped_total;
}

GFXcanvas1 &disp_canvas(uint8_t i) { return canvas[i & 3]; }
bool disp_ok(uint8_t i) { return i < N_SCREENS && panel_ok[i]; }

uint8_t disp_present_mask() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < N_SCREENS; i++) if (panel_ok[i]) m |= (1 << i);
  return m;
}

bool disp_health_check() {
  bool changed = false;
  for (uint8_t i = 0; i < N_SCREENS; i++) {
    mux_select(OLED_CH[i]);
    // Ask the mux what channel it thinks it is on, rather than believing our
    // own cache. This is the only thing that catches a mux that answers but
    // has not switched.
    bool routed = mux_verify(OLED_CH[i]);
    bool answering = false;
    if (routed) {
      Wire.beginTransmission(ADDR_OLED);
      answering = (Wire.endTransmission() == 0);
    }
    if (answering) bus_note_ok(DEV_OLED);
    else           bus_note_error(DEV_OLED);

    if (answering != panel_ok[i]) {
      Serial.print("# OLED J"); Serial.print(i + 1);
      Serial.println(answering ? " came back" : " STOPPED ANSWERING");
      panel_ok[i] = answering;
      changed = true;
      // A panel that has just reappeared has lost its configuration: it was
      // either power-cycled or reseated. Re-initialise it rather than pushing
      // pixels at a controller that is not set up.
      if (answering) {
        panel[i].begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
        panel[i].setRotation(0);
        cur_contrast = 0xFF;
        // A re-initialised controller has blank GDDRAM whatever the shadow
        // says; the next commit must push unconditionally.
        shadow_drop(i);
      }
    }
  }
  mux_off();
  if (changed && pinned < 0) write_contrast(target_contrast());
  return changed;
}

void disp_all_off(bool off) {
  blanked = off;
  for (uint8_t i = 0; i < N_SCREENS; i++) {
    if (!panel_ok[i]) continue;
    mux_select(OLED_CH[i]);
    panel[i].ssd1306_command(off ? SSD1306_DISPLAYOFF : SSD1306_DISPLAYON);
  }
  mux_off();
}

bool disp_is_blanked() { return blanked; }

void disp_rescan() {
  // Every controller below is re-begun and cleared; no shadow survives that.
  shadow_drop_all();
  for (uint8_t i = 0; i < N_SCREENS; i++) {
    // ~40 I2C transactions per panel. On a wedged bus, at the per-transaction
    // timeout, four panels is longer than the task watchdog allows, and
    // rescan is reachable at runtime from the serial 'S' command, not just at
    // boot. Feed the dog between panels.
    feedLoopWDT();
    mux_select(OLED_CH[i]);
    delay(2);
    panel_ok[i] = panel[i].begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
    if (panel_ok[i]) {
      // NEVER let the library's own rotation run. Rotation is applied in
      // blit(), and if GFX also rotated we would get it twice.
      panel[i].setRotation(0);
      panel[i].clearDisplay();
      panel[i].display();
      // begin() writes 0xD3,0x00 and its own contrast triple, so anything set
      // before a (re)scan is gone. Re-apply.
      panel[i].ssd1306_command(SSD1306_SETDISPLAYOFFSET);
      panel[i].ssd1306_command(0x00);
    }
    Serial.print("# OLED J"); Serial.print(i + 1);
    Serial.print(" ch");      Serial.print(OLED_CH[i]);
    Serial.println(panel_ok[i] ? " OK" : " NOT FOUND");
  }
  mux_off();
  cur_contrast = 0xFF;
  write_contrast(target_contrast());
}

void disp_begin() {
  // The five 1 KB canvases are malloc'd by their constructors, before setup()
  // runs, and a failure leaves buffer == NULL rather than throwing. Most of
  // GFXcanvas1 guards on that, but drawFastRawVLine/HLine, which every
  // fillRect, drawRect and drawCircle reaches, do not. So check here rather
  // than trusting the library, because the alternative is a null-deref on the
  // first frame with no explanation.
  for (uint8_t i = 0; i < N_SCREENS; i++) {
    if (!canvas[i].getBuffer()) {
      Serial.print("# FATAL: canvas "); Serial.print(i);
      Serial.println(" did not allocate, out of heap. Halting.");
      // Better a board that says why it stopped than one that crashes with a
      // backtrace into the graphics library.
      while (true) { delay(1000); }
    }
    canvas[i].fillScreen(0);
  }
  disp_rescan();
}

void disp_flip(int which) {
  for (uint8_t i = 0; i < N_SCREENS; i++)
    if (which < 0 || which == (int)i) {
      OLED_ROT[i] = (uint8_t)((OLED_ROT[i] + 2) & 3);
      // The transform changed, so the shadow describes an image this panel
      // will never be asked to show again. (The blit would produce different
      // bytes anyway; dropping the shadow just makes that not matter.)
      shadow_drop(i);
    }
}
