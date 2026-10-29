#include "ui.h"
#include "../screens/display.h"
#include "../settings/store.h"
#include "leds.h"
#include "../demo.h"
#include "../screens/anim.h"
#include <string.h>
#include <stdio.h>

UiEnv ui_env;

static uint8_t  page = PG_CLOCK;
static uint8_t  variant[PG_COUNT];
static uint8_t  set_cursor = 0;         // which SetItem the settings page edits
static uint16_t anim_tick = 0;
static uint32_t next_anim = 0;

// ---- THE ANIMATION PAGE PLAYS ITSELF --------------------------------------
// Press DOWN and animations start. No second press, no picker, no reel to
// enter: THE PAGE IS THE REEL. Every ANIM_SHUFFLE_MS the schedule picks a new
// selection, and a repeat press of DOWN reshuffles immediately.
//
// Sometimes all four panels show the SAME drawing and sometimes they each show
// a different one, because a page that only ever did one of those reads as a
// setting rather than as something alive.
//
// WHERE THE RANDOMNESS LIVES, AND WHY THE PROVER STILL WORKS. anim.cpp and
// pages.cpp are pure and stay pure — they are handed four ids and four phases
// and they draw them. Only this scheduler is nondeterministic, and the prover
// does NOT prove a schedule. It sweeps THE WHOLE SPACE the schedule can draw
// from: every animation, on every panel, across a full cycle of frames. Any
// selection this can ever make is four points already inside that set, so
// proving the set proves every possible schedule at once — including ones a
// sampled run would never have produced. Sampling one schedule would have been
// strictly weaker AND would have needed the wall-clock nondeterminism a prover
// in this family may not have.
static const uint32_t ANIM_SHUFFLE_MS = 5000;
// One selection in three is the same drawing on all four panels: often enough
// to read as deliberate, rare enough that the page never looks stuck.
static const uint8_t  ANIM_SAME_IN = 3;

static uint8_t  anim_ids[4]   = {0, 1, 2, 3};
static uint16_t anim_phase[4] = {0, 0, 0, 0};
static uint32_t anim_next_shuffle = 0;

// ---- THE SHOWREEL IS THE FIRST STOP ---------------------------------------
// Arriving on the animation page starts the segmented attract-mode loop
// (pages.h) and it LOOPS until asked to stop: no shuffling underneath it, and
// a repeat press of DOWN is the exit into the normal self-playing roster —
// the same gesture still means "show me something else". Serial B3 arrives
// through the same path, so one injected DOWN reaches the reel too.
static bool showreel_on = false;

// ---- THE STOPWATCH (UP) ----------------------------------------------------
// Replaced the stock ticker on 2026-10-15. Three states, pure millis():
//   ZERO   acc = 0, not running
//   RUN    elapsed = acc + (now - t0)   -- unsigned subtraction, so a millis()
//                                         wrap mid-run costs nothing
//   PAUSE  elapsed = acc, frozen
// No lapping. It keeps counting on any other page; UP brings it back.
static uint8_t  stw_state = STW_ZERO;
static uint32_t stw_acc = 0, stw_t0 = 0;

static uint32_t stw_elapsed(uint32_t now) {
  return stw_state == STW_RUN ? stw_acc + (uint32_t)(now - stw_t0) : stw_acc;
}
static void stw_run(uint32_t now) {            // ZERO or PAUSE -> RUN
  if (stw_state == STW_RUN) return;
  stw_t0 = now;
  stw_state = STW_RUN;
}
static void stw_down(uint32_t now) {           // RUN -> PAUSE -> ZERO
  if (stw_state == STW_RUN) {
    stw_acc = stw_elapsed(now);
    stw_state = STW_PAUSE;
  } else if (stw_state == STW_PAUSE) {
    stw_acc = 0;
    stw_state = STW_ZERO;
  }
}

// ---- the showreel's STOCKS segment, without a ticker ------------------------
// The reel (DOWN) still plays its 7 s markets segment through the pure
// render_market() renderer. Nothing fetches quotes any more, so the four rows
// come from here: in a DEMO build the same baked basket market.cpp used to
// serve, on the same bounded 3 s walk; in a shipping build the symbols with no
// price, which is exactly what that page showed on this board (it never held a
// WiFi association long enough to fetch one).
struct ReelQuote { const char *sym; int32_t base_cents; int16_t chg_bp;
                   uint16_t seed; int32_t step_cents; };
static const ReelQuote REEL_Q[4] = {
  {"SPX",  660412,   84, 0x2F1D,   45},
  {"NVDA",  18327,  213, 0x7A03,    9},
  {"AAPL",  23196,  -47, 0x51C7,   11},
  {"BTC", 11284300,  126, 0x1E95, 3500},
};
static void reel_quotes(Quote out[4], uint32_t now) {
#ifdef DEMO_BUILD
  const uint32_t step = now / 3000u;
  for (uint8_t k = 0; k < 4; k++) {
    const ReelQuote &r = REEL_Q[k];
    uint16_t x = (uint16_t)(r.seed ^ (step * 40503u));
    if (!x) x = 0xACE1;
    x ^= (uint16_t)(x << 7); x ^= (uint16_t)(x >> 9); x ^= (uint16_t)(x << 8);
    const int32_t off = (int32_t)(x % (uint16_t)(2 * r.step_cents + 1)) - r.step_cents;
    const int32_t px  = r.base_cents + off;
    int32_t bp = r.chg_bp + (int32_t)((int64_t)off * 10000 / r.base_cents);
    if (bp > 32000) bp = 32000;
    if (bp < -32000) bp = -32000;
    // Always positive (every base is far above its step), so unsigned keeps
    // the formatter's worst case inside the 12-byte field.
    const uint32_t upx   = (uint32_t)(px < 0 ? 0 : px);
    const uint32_t whole = upx / 100u;    // BTC is six figures
    const uint8_t  frac  = (uint8_t)(upx % 100u);
    Quote &q = out[k];
    snprintf(q.sym, sizeof q.sym, "%s", r.sym);
    if (whole >= 10000)     snprintf(q.price, sizeof q.price, "%lu", (unsigned long)whole);
    else if (whole >= 1000) snprintf(q.price, sizeof q.price, "%lu.%01u", (unsigned long)whole, (unsigned)(frac / 10u));
    else                    snprintf(q.price, sizeof q.price, "%lu.%02u", (unsigned long)whole, (unsigned)frac);
    q.chg_bp = (int16_t)bp;
    q.valid  = true;
  }
#else
  (void)now;
  for (uint8_t k = 0; k < 4; k++) {
    out[k] = Quote{};
    snprintf(out[k].sym, sizeof out[k].sym, "%s", REEL_Q[k].sym);
  }
#endif
}

static void anim_shuffle(uint32_t now);        // both defined further down
static uint32_t anim_rand(uint32_t now);

// ---- the walker's pass variant --------------------------------------------
// Two random bits per reel loop: which row he crosses and from which side
// (anim.h). Runtime randomness lives HERE, in the scheduler, exactly like the
// roster's anim_ids — the renderer is pure in the variant, and the prover
// enumerates all four. A re-roll is forced to differ from the last pass so
// two consecutive loops never look identical.
static uint8_t sr_walk_variant = 0;
static void sr_walk_roll(uint32_t now) {
  uint8_t v = (uint8_t)(anim_rand(now) & 3);
  if (v == sr_walk_variant) v = (uint8_t)(v ^ 1);   // at least the row differs
  sr_walk_variant = v;
}

static void showreel_start(uint32_t now) {
  showreel_on = true;
  anim_tick   = 0;                       // the reel starts at its first beat
  next_anim   = now + showreel_tick_ms(0);
  sr_walk_roll(now);
}

// ---- AND THE ROSTER TELLS THE TIME TOO ------------------------------------
// The reel alternates content and the giant clock inside its own script; the
// shuffled roster gets the SAME cadence from out here: ~10 s of roster, then
// the clock interlude played once (SHOWCLOCK_TICKS frames), then back. So
// whichever stop of the DOWN page is up, the time is never more than a few
// seconds of patience away, in the same giant-digit form.
static const uint32_t ROSTER_SCENE_MS = 10000;
static uint8_t  roster_scene = SCENE_ROSTER;   // ROSTER or CLOCK, roster mode
static uint32_t scene_until  = 0;

static void roster_start(uint32_t now) {
  showreel_on  = false;
  roster_scene = SCENE_ROSTER;
  scene_until  = now + ROSTER_SCENE_MS;
  anim_shuffle(now);
}

// A 32-bit xorshift — the firmware's existing convention for "varying, cheap
// and dependency-free".
// Seeded once from the boot counter and the millis of the first shuffle, so
// two boards do not march in step and one board does not replay the same reel
// on every power-on.
static uint32_t anim_rng = 0;
static uint32_t anim_rand(uint32_t now) {
  if (!anim_rng) anim_rng = (now ^ ((uint32_t)ui_env.boots << 16) ^ 0x9E3779B9u) | 1u;
  anim_rng ^= anim_rng << 13;
  anim_rng ^= anim_rng >> 17;
  anim_rng ^= anim_rng << 5;
  return anim_rng;
}

static void anim_shuffle(uint32_t now) {
  const uint8_t n = anim_total();
  if (!n) return;
  if ((anim_rand(now) % ANIM_SAME_IN) == 0) {
    // ALL FOUR THE SAME — but NOT in lockstep. Each panel gets its own phase,
    // so the four read as four instruments playing one part rather than as a
    // single picture stretched across the glass. That is the whole reason for
    // doing this on a 2x2 at all.
    const uint8_t id = (uint8_t)(anim_rand(now) % n);
    const uint16_t cyc = anim_cycle(id) ? anim_cycle(id) : 1;
    for (uint8_t k = 0; k < 4; k++) {
      anim_ids[k]   = id;
      anim_phase[k] = (uint16_t)(anim_rand(now) % cyc);
    }
  } else {
    // FOUR DIFFERENT ONES, avoiding repeats while the roster allows it: a
    // duplicate sitting beside its twin reads as a bug rather than as chance.
    for (uint8_t k = 0; k < 4; k++) {
      uint8_t id = 0;
      for (uint8_t tries = 0; tries < 8; tries++) {
        id = (uint8_t)(anim_rand(now) % n);
        bool clash = false;
        for (uint8_t j = 0; j < k; j++) if (anim_ids[j] == id) clash = true;
        if (!clash || n < 4) break;
      }
      const uint16_t cyc = anim_cycle(id) ? anim_cycle(id) : 1;
      anim_ids[k]   = id;
      anim_phase[k] = (uint16_t)(anim_rand(now) % cyc);
    }
  }
  anim_tick = 0;
  next_anim = now + ANIM_FRAME_MS;
  anim_next_shuffle = now + ANIM_SHUFFLE_MS;
}
// WHEN INPUT LAST HAPPENED. Compare against it with a SIGNED difference,
// always — `(int32_t)(now - last_input_ms) > TIMEOUT`, never the unsigned form.
//
// It can legitimately sit a millisecond or two AHEAD of the `now` a tick is
// working from: loop() samples millis() once at the top and passes that value
// down, but poll_serial() runs in between and a serial-injected press stamps
// this with a fresh millis(). Unsigned, that one millisecond of skew makes
// `now - last_input_ms` underflow to about 4.29 billion, which is greater than
// every timeout here — so the animation page bounced back to the clock the
// instant it was opened, and the settings page could not be entered at all.
// Intermittently, because whether the two millis() calls land in the same
// millisecond is a race.
//
// Signed is also what the rest of this firmware uses for deadline comparisons
// (see wifi_tick, ui_tick's animation clock), and it is correct across the
// 49-day millis() wrap as well, which the unsigned form is not.
static uint32_t last_input_ms = 0;
static const int32_t ANIM_IDLE_MS = 10L * 60L * 1000L;
static const int32_t SET_IDLE_MS  = 30L * 1000L;
static bool     dirty_screen = true;

// What was last painted, so nothing is pushed over I2C for a frame that is
// pixel-identical to the one already on the glass.
static int8_t  last_min = -1, last_sec = -1;
static uint8_t last_page = 0xFF, last_variant = 0xFF;
static bool    last_ok = false;

// ============================================================== buttons ====
// MODE and SET act on RELEASE, so a long press can mean something different
// from a short one.
//
// UP and DOWN act on PRESS. They also auto-repeat — but the repeat is only
// ACTED ON by the settings page. Version A removed auto-repeat outright,
// correctly, because there was no number left to run and leaning on a button
// would otherwise cycle a page as fast as the repeat timer. The
// settings page brings a number back: a time in 15-minute steps is 96 presses
// to sweep a day without it, and about seven seconds of holding with it. So
// the event is generated here and discarded everywhere the old reasoning still
// applies, rather than the two rules fighting over one flag.
struct Btn {
  bool     down;
  uint32_t t_down;
  bool     long_fired;
  bool     raw;
  uint32_t t_change;
  uint32_t t_repeat;
  bool     saw_press;      // a press was OBSERVED, even if it is already over
};
static Btn btn[4];

uint8_t btn_stable = 0, btn_latched = 0;

enum Ev : uint8_t { EV_NONE = 0, EV_SHORT, EV_LONG, EV_REPEAT };

static const uint32_t UI_LONG_MS = 700;

static Ev poll_btn(uint8_t i, uint32_t now) {
  static const uint8_t PIN[4] = {BTN_MODE, BTN_SET, BTN_UP, BTN_DOWN};
  bool raw = (digitalRead(PIN[i]) == LOW);          // pull-up: LOW = pressed
  Btn &b = btn[i];

  // ========================================================================
  // A TAP CAN BE SHORTER THAN THE GAP BETWEEN TWO SAMPLES. LATCH IT.
  // ========================================================================
  // This function is called once per loop() pass, so the sampling rate IS the
  // loop rate -- and on the animation page a repaint pushes 1024 bytes to each
  // of four panels, ~23 ms each at 400 kHz, ~92 ms for all four, blocking. So
  // loop() comes round every ~92-125 ms there against a few ms on the clock.
  //
  // The debounce below needs the level stable across TWO samples. Press and
  // release between them and the `raw != b.raw` arm simply re-stamps t_change
  // both times: THE PRESS IS DISCARDED, not merely delayed. The user's report
  // was that the animations button "has to be hit a bit longer than all the
  // other buttons", which is exactly this and only this -- it is the button
  // pressed while the expensive page is painting.
  //
  // So record that a press was SEEN. The debounce still decides when to act
  // and still rejects contact bounce, but a real tap can no longer fall down
  // the gap between two polls. Deliberately not a faster sampler: sampling
  // from inside the paint would mean re-entering the UI mid-repaint, and
  // changing page while a frame is half-written is a far worse bug than a
  // sluggish button.
  if (raw && !b.down) b.saw_press = true;

  if (raw != b.raw) { b.raw = raw; b.t_change = now; return EV_NONE; }
  if (now - b.t_change < BTN_DEBOUNCE_MS) return EV_NONE;

  if (raw && !b.down) {                              // press edge
    b.saw_press = false;                             // consumed by the real edge
    b.down = true;
    b.t_down = now;
    b.t_repeat = now;
    b.long_fired = false;
    btn_stable  |= (uint8_t)(1 << i);
    btn_latched |= (uint8_t)(1 << i);
    led_ack(i);
    if (i >= 2) return EV_SHORT;                     // UP/DOWN act at once
    return EV_NONE;
  }
  if (!raw && b.down) {                              // release edge
    b.down = false;
    b.saw_press = false;
    btn_stable &= (uint8_t)~(1 << i);
    if (i < 2 && !b.long_fired) return EV_SHORT;
    return EV_NONE;
  }

  // THE TAP THAT WAS OVER BEFORE WE LOOKED AGAIN. Settled low, never counted
  // as a press, but one was seen: honour it as a short press. It cannot become
  // a long press or a repeat, and that is correct -- it was a tap.
  if (!raw && !b.down && b.saw_press) {
    b.saw_press = false;
    btn_latched |= (uint8_t)(1 << i);
    led_ack(i);
    return EV_SHORT;
  }
  if (raw && b.down && i < 2 && !b.long_fired &&
      now - b.t_down >= UI_LONG_MS) {
    b.long_fired = true;
    return EV_LONG;
  }
  // Held UP/DOWN. Whether this means anything is the caller's business.
  if (raw && b.down && i >= 2 &&
      now - b.t_down   >= BTN_REPEAT_FIRST &&
      now - b.t_repeat >= BTN_REPEAT_MS) {
    b.t_repeat = now;
    return EV_REPEAT;
  }
  return EV_NONE;
}

// =============================================================== helpers ===
void ui_force_repaint() { dirty_screen = true; last_min = -1; last_sec = -1; }
uint8_t ui_page()       { return page; }
uint8_t ui_variant()    { return variant[page % PG_COUNT]; }
uint8_t ui_set_cursor() { return set_cursor; }

// A daily window, handling the wrap past midnight. 22:30 to 07:00 is the night
// default and is exactly the case a naive start<=now<end comparison gets
// wrong. ONE implementation, because there are now two windows — the night
// dim and the screens-off schedule — and a second copy of this comparison is a
// second chance to get the midnight wrap wrong in only one of them.
static bool in_window(const RtcTime &t, uint8_t sh, uint8_t sm,
                      uint8_t eh, uint8_t em) {
  uint16_t now = (uint16_t)(t.hour * 60 + t.min);
  uint16_t a = (uint16_t)(sh * 60 + sm);
  uint16_t b = (uint16_t)(eh * 60 + em);
  if (a == b) return false;                 // a zero-length window is no window
  return (a < b) ? (now >= a && now < b) : (now >= a || now < b);
}

bool ui_is_night(const RtcTime &t) {
  if (!t.ok || cfg.night_mode == 0) return false;
  return in_window(t, cfg.night_start_h, cfg.night_start_m,
                      cfg.night_end_h,   cfg.night_end_m);
}

bool ui_is_screens_off(const RtcTime &t) {
  // NO RTC, NO SCHEDULE. A clock that does not know the time must not decide
  // the screens should be off — the failure would look exactly like a dead
  // board, and the one thing the user needs to see in that state is the
  // display saying it does not know the time.
  if (!t.ok || !cfg.off_enable) return false;
  return in_window(t, cfg.off_start_h, cfg.off_start_m,
                      cfg.off_end_h,   cfg.off_end_m);
}

// ============================================================ blank policy ==
// TWO SCHEDULES, ONE OWNER. The night window (night_mode 2) and the daily
// screens-off schedule can both want the glass dark. They used to each call
// disp_all_off() from their own code — display.cpp for the first, and the
// second would have added a rival — so whichever transitioned last won, and a
// wake inside one window was silently undone by the other. Now display.cpp
// keeps only the mechanism and the decision is made here, from both inputs,
// once per tick.
//
// A press inside a dark window wakes the panels for OFF_WAKE_MS and then they
// go back down. Waking permanently is what the night blank used to do, and it
// means one accidental press at midnight leaves a bedroom lit until morning.
static const int32_t OFF_WAKE_MS = 30000;
static bool woken = false;

static void wake_panels() {
  disp_all_off(false);
  woken = true;
  // The glass currently holds the frame written BEFORE the blank — 23:30 at
  // 04:00. ui_paint's last_min bookkeeping thinks it painted every minute
  // since, so without this the wrong time stays up until the next boundary.
  ui_force_repaint();
}

static void blank_policy(uint32_t now, const RtcTime &t) {
  bool want_dark = ui_is_screens_off(t) ||
                   (cfg.night_mode == 2 && ui_is_night(t));

  if (!want_dark) {
    woken = false;
    if (disp_is_blanked()) { disp_all_off(false); ui_force_repaint(); }
    return;
  }
  if (woken) {
    // Signed, for the reason spelled out at last_input_ms. Unsigned, a press
    // injected over serial re-blanked the panels on the very next tick instead
    // of holding them up for half a minute.
    if ((int32_t)(now - last_input_ms) < OFF_WAKE_MS) return;
    woken = false;
  }
  if (!disp_is_blanked()) disp_all_off(true);
}

// Press the button for the page you are on and it cycles; press any other and
// it jumps. One rule, four buttons.
static void select_page(uint8_t p, uint32_t now) {
  const bool arriving = (page != p);
  if (page == p) {
    // THE ANIMATION PAGE HAS NOTHING TO CYCLE. It plays itself, so a repeat
    // press means "show me something else": the first repeat leaves the
    // showreel for the shuffled roster (which restarts its own clock
    // cadence), and every one after that reshuffles.
    if (p == PG_ANIM) roster_start(now);
    else variant[p] = (uint8_t)((variant[p] + 1) % page_variants(p));
  } else {
    page = p;
  }
  // ARRIVING STARTS THE SHOWREEL IMMEDIATELY — it is the page's first stop,
  // playing before the user's finger is off the button.
  if (p == PG_ANIM && arriving) showreel_start(now);
  dirty_screen = true;
  last_input_ms = now;
}

// UP and DOWN short presses outside the settings page. UP always lands on the
// stopwatch and starts it unless it is already running (then it only shows
// it). DOWN on the stopwatch pauses a running one and clears a paused one; DOWN
// anywhere else is the animation page, exactly as before.
static void updown_press(uint8_t i, uint32_t now) {
  if (i == 2) {
    page = PG_STOPWATCH;
    stw_run(now);
  } else if (page == PG_STOPWATCH) {
    stw_down(now);
  } else {
    select_page(PG_ANIM, now);
    return;
  }
  dirty_screen = true;
  last_input_ms = now;
}

// ============================================================== settings ====
// Times move in 15-minute steps. Fine enough to say "half eleven" and coarse
// enough that the auto-repeat sweeps a whole day in a few seconds.
static const int16_t TIME_STEP_MIN = 15;

static void bump_time(uint8_t &h, uint8_t &m, int8_t dir) {
  int16_t t = (int16_t)(h * 60 + m);
  int16_t rem = (int16_t)(t % TIME_STEP_MIN);
  if (rem) {
    // SNAP FIRST when the stored time is off the grid, which it can be after a
    // serial edit or an older default. Adding 15 to 22:37 should give 22:45,
    // not 22:52, and pressing DOWN from 22:37 should give 22:30 rather than
    // skipping past it to 22:15.
    t = (int16_t)(t - rem);
    if (dir > 0) t = (int16_t)(t + TIME_STEP_MIN);
  } else {
    t = (int16_t)(t + dir * TIME_STEP_MIN);
  }
  while (t < 0) t = (int16_t)(t + 24 * 60);
  t = (int16_t)(t % (24 * 60));
  h = (uint8_t)(t / 60);
  m = (uint8_t)(t % 60);
}

static void settings_adjust(int8_t dir) {
  switch (set_cursor) {
    case SI_SENS: {
      // ONE DIAL OVER TWO FIELDS. Position 0 is OFF, which is cfg.autodim = 0;
      // 1..4 are the DimSens values with autodim on. Presenting them as one
      // ordinal is what makes the item readable — "sensitivity: off" is the
      // obvious way to say "do not dim", and a separate on/off row next to a
      // sensitivity row invites setting a sensitivity that does nothing.
      int8_t lv = (int8_t)(cfg.autodim ? cfg.dim_sens : 0) + dir;
      if (lv < 0) lv = (int8_t)(DIM_SENS_COUNT - 1);
      if (lv > (int8_t)(DIM_SENS_COUNT - 1)) lv = 0;
      if (lv == 0) cfg.autodim = 0;
      else { cfg.autodim = 1; cfg.dim_sens = (uint8_t)lv; }
      // The contrast deadband is 8 and one sensitivity step can move the
      // target by less than that, so without forcing the write the first press
      // changes nothing you can see and the second one appears to do the work
      // of both.
      disp_refresh();
      break;
    }
    case SI_TEMP:
      cfg.temp_unit = cfg.temp_unit ? 0 : 1;
      break;
    case SI_OFF_EN:
      cfg.off_enable = cfg.off_enable ? 0 : 1;
      break;
    case SI_OFF_START:
      bump_time(cfg.off_start_h, cfg.off_start_m, dir);
      break;
    default:
      bump_time(cfg.off_end_h, cfg.off_end_m, dir);
      break;
  }
  settings_mark_dirty();
  dirty_screen = true;
}

// MODE long-presses the LEDs on and off from ANY page, settings included, so
// the one control that silences the board never depends on where you are.
static void toggle_leds() {
  cfg.led_mode = cfg.led_mode ? 0 : 1;
  if (!cfg.led_mode) led_all_off();
  settings_mark_dirty();
  Serial.print("# leds "); Serial.println(cfg.led_mode ? "on" : "off");
}

// cfg -> the pure snapshot the renderer and the serial report both read. One
// place, so what a panel shows and what `P` prints cannot disagree.
static void snapshot_settings(SettingsData &s) {
  s.sens_level  = (uint8_t)(cfg.autodim ? cfg.dim_sens : 0);
  s.temp_f      = (uint8_t)(cfg.temp_unit != 0);
  s.off_enable  = (uint8_t)(cfg.off_enable != 0);
  s.off_start_h = cfg.off_start_h; s.off_start_m = cfg.off_start_m;
  s.off_end_h   = cfg.off_end_h;   s.off_end_m   = cfg.off_end_m;
  s.cursor      = set_cursor;
}

static void enter_settings(uint32_t now) {
  page = PG_SETTINGS;
  set_cursor = 0;
  dirty_screen = true;
  last_input_ms = now;
  Serial.println("# settings: SET=next  UP/DOWN=change  MODE=exit");
}

static void leave_settings() {
  page = PG_CLOCK;
  dirty_screen = true;
}

// While the settings page is up the four buttons mean something else. This is
// the only modal state on the device and it is bounded two ways: MODE always
// leaves, and ui_tick times it out.
static void settings_input(uint8_t i, Ev e) {
  if (i == 0) {                                   // MODE
    if (e == EV_LONG) toggle_leds();
    else              leave_settings();
    return;
  }
  if (i == 1) {                                   // SET
    if (e == EV_LONG) { leave_settings(); return; }
    set_cursor = (uint8_t)((set_cursor + 1) % SI_COUNT);
    dirty_screen = true;
    return;
  }
  settings_adjust(i == 2 ? (int8_t)+1 : (int8_t)-1);   // UP / DOWN
}

// ================================================================ input ====
bool ui_input(uint32_t now) {
  bool acted = false;
  for (uint8_t i = 0; i < 4; i++) {
    Ev e = poll_btn(i, now);
    if (e == EV_NONE) continue;
    acted = true;
    last_input_ms = now;

    // Gate on whether the glass is ACTUALLY dark, not on the setting. Testing
    // cfg.night_mode == 2 here meant that once you chose "blank" the device
    // swallowed every press from every button at any hour, and it survived
    // reboots. The first press only wakes the panels.
    if (disp_is_blanked()) { wake_panels(); continue; }

    if (page == PG_SETTINGS) { settings_input(i, e); continue; }

    // A held UP or DOWN outside the settings page still means nothing. See the
    // note on the Btn struct: there is no number to run on the stopwatch or
    // animation pages, and leaning on a button should not cycle them.
    if (e == EV_REPEAT) continue;

    if (e == EV_SHORT) {
      switch (i) {
        case 0: select_page(PG_CLOCK,  now); break;
        case 1: select_page(PG_SENSOR, now); break;
        default: updown_press(i, now);       break;
      }
      continue;
    }

    // EV_LONG, MODE and SET only. MODE silences the LEDs. SET opens the
    // settings page — it used to toggle C/F, which is now the second item in
    // there, so nothing that was reachable before became less reachable.
    if (i == 0)      toggle_leds();
    else if (i == 1) enter_settings(now);
  }
  return acted;
}

// The serial stand-in for a real press. Goes through the SAME functions a
// button reaches — not a second implementation that could work while the real
// one does not. With SW1-SW4 unfitted this is the only way to reach the
// settings page at all, which is why the long press is injectable too.
void ui_button(uint8_t i, bool long_press) {
  uint32_t now = millis();
  i &= 3;
  last_input_ms = now;
  if (disp_is_blanked()) { wake_panels(); return; }

  if (page == PG_SETTINGS) {
    settings_input(i, long_press ? EV_LONG : EV_SHORT);
  } else if (long_press) {
    if (i == 0)      toggle_leds();
    else if (i == 1) enter_settings(now);
    else Serial.println("# nothing bound to a long press on UP/DOWN");
  } else {
    switch (i) {
      case 0: select_page(PG_CLOCK,  now); break;
      case 1: select_page(PG_SENSOR, now); break;
      default: updown_press(i, now);       break;
    }
  }

  Serial.print("# page "); Serial.print(page_name(page));
  if (page == PG_SETTINGS) {
    char lab[16], val[16];
    SettingsData s{};
    snapshot_settings(s);
    set_row_text(s, set_cursor, lab, sizeof lab, val, sizeof val);
    Serial.print("  ["); Serial.print(lab);
    Serial.print(" = ");  Serial.print(val); Serial.println("]");
  } else if (page == PG_ANIM) {
    // Say WHICH stop, so a serial-injected B3 can be verified end to end:
    // one press lands on SHOWREEL, a second on the shuffled roster (which
    // cuts to the clock interlude on its own ~10 s cadence).
    Serial.println(showreel_on ? " SHOWREEL" : " shuffle+clock");
  } else if (page == PG_STOPWATCH) {
    Serial.println();
    ui_stopwatch_report();
  } else {
    Serial.print(" variant "); Serial.println(variant[page]);
  }
}

// Serial `W`: ONE line, parsed by the acceptance script. Keep the format.
void ui_stopwatch_report() {
  static const char *const NAME[3] = {"zero", "run", "pause"};
  Serial.print("# SW state="); Serial.print(NAME[stw_state % 3]);
  Serial.print(" ms=");        Serial.print(stw_elapsed(millis()));
  Serial.print(" page=");      Serial.println((unsigned)page);
}

// ================================================================= tick ====
void ui_tick(uint32_t now, const RtcTime &t) {
  disp_burnin_tick(now / 1000);

  if (page == PG_ANIM && (int32_t)(now - next_anim) >= 0) {
    anim_tick++;
    if (showreel_on) {
      const uint16_t rt = (uint16_t)(anim_tick % SHOWREEL_TICKS);
      // A NEW PASS VARIANT EVERY LOOP, rolled at the wrap so the walker's
      // row and direction are a surprise each time round.
      if (rt == 0) sr_walk_roll(now);
      // The reel's ticks are not one duration — the walker and the skies run
      // on the fast dirty-commit tick. pages.cpp states which; see
      // showreel_tick_ms.
      next_anim = now + showreel_tick_ms(rt);
    } else {
      next_anim = now + ANIM_FRAME_MS;
    }
    dirty_screen = true;
  }

  // THE ROSTER'S OWN CADENCE: ~10 s of shuffled streams, then the clock
  // interlude played once, then back. SIGNED comparisons, always — the same
  // millis() underflow that bit last_input_ms applies to every deadline here.
  if (page == PG_ANIM && !showreel_on &&
      (int32_t)(now - scene_until) >= 0) {
    if (roster_scene == SCENE_ROSTER) {
      roster_scene = SCENE_CLOCK;
      anim_tick    = 0;                  // the interlude starts at its slam
      next_anim    = now + ANIM_FRAME_MS;
      scene_until  = now + (uint32_t)SHOWCLOCK_TICKS * ANIM_FRAME_MS;
    } else {
      roster_scene = SCENE_ROSTER;
      anim_shuffle(now);                 // fresh picks, resets the tick
      scene_until  = now + ROSTER_SCENE_MS;
    }
    dirty_screen = true;
  }

  // A NEW SELECTION EVERY FIVE SECONDS while the roster streams are up — but
  // never under the showreel or the clock interlude, which are scripted.
  if (page == PG_ANIM && !showreel_on && roster_scene == SCENE_ROSTER &&
      (int32_t)(now - anim_next_shuffle) >= 0) {
    anim_shuffle(now);
    dirty_screen = true;
  }

  // The animation page must not be left running for a week. It is the highest
  // duty cycle content on the device and the only page whose pixels move for
  // reasons unrelated to the burn-in walk.
  //
  // THE DEMO BUILD PINS THE SHOWREEL: it exists to be filmed, and a take that
  // dies mid-loop because ten minutes passed is a broken take. A demo unit
  // does not run 24/7 for a year (see demo.h on the brightness trade), so the
  // burn-in argument that owns this timeout does not own the reel there. The
  // SHIPPING build keeps the timeout on everything, showreel included.
#ifdef DEMO_BUILD
  const bool anim_pinned = showreel_on;
#else
  const bool anim_pinned = false;
#endif
  if (page == PG_ANIM && !anim_pinned &&
      (int32_t)(now - last_input_ms) > ANIM_IDLE_MS) {
    page = PG_CLOCK;
    dirty_screen = true;
  }

  // The settings page is the one modal state on the device, so it gets the
  // short timeout the old menu had. Walking away mid-edit must not leave a
  // clock showing "SLEEP AT" indefinitely — and it is the one page that never
  // repaints on its own, so a stale one would also be four static images
  // sitting in the same pixels, which is the thing the whole display layer
  // exists to prevent.
  if (page == PG_SETTINGS && (int32_t)(now - last_input_ms) > SET_IDLE_MS) {
    leave_settings();
    Serial.println("# settings timed out");
  }

  // LED_MENU exists for exactly this and had nothing to report since version A
  // deleted the menu: a steady amber column saying you are in a mode rather
  // than looking at the clock. Its hold budget bounds it, so a settings page
  // left up by a wedged UI cannot glow all night.
  led_hold(LED_MENU, page == PG_SETTINGS);

  disp_set_night(ui_is_night(t));
  // AFTER disp_set_night, which sets the contrast cap this may then override
  // by turning the panels off entirely.
  blank_policy(now, t);
}

// ================================================================ paint ====
static void fill_pagedata(PageData &d, const RtcTime &t, uint32_t now) {
  d.clock.hour = t.hour; d.clock.minute = t.min; d.clock.second = t.sec;
  d.clock.day = t.day; d.clock.month = t.mon; d.clock.year = t.year;
  d.clock.weekday = fx_weekday(t.year, t.mon, t.day);
  d.clock.temp_c10 = (int16_t)(ui_env.sht_c * 10.0f);
  d.clock.humidity = ui_env.rh;
  d.clock.hour24 = cfg.hour24 != 0;
  d.clock.temp_f = cfg.temp_unit != 0;
  d.clock.valid = t.ok;

  d.sens.temp_c10 = d.clock.temp_c10;
  d.sens.humidity = ui_env.rh;
  d.sens.sht_ok   = ui_env.sht_ok;
  d.sens.wifi_up  = ui_env.wifi_up;
  d.sens.rssi     = (int16_t)ui_env.rssi;
  snprintf(d.sens.ssid, sizeof d.sens.ssid, "%s", ui_env.ssid);
  snprintf(d.sens.ip,   sizeof d.sens.ip,   "%s", ui_env.ip);
  d.sens.uptime_s = millis() / 1000u;
  d.sens.contrast = disp_contrast();
  d.sens.night    = ui_is_night(t);
  d.sens.temp_f   = cfg.temp_unit != 0;
  // Raw counts to a percentage. The divisor is the same 1023 full scale the
  // dimming curve uses, so the number on the panel is the one the brightness
  // logic actually saw.
  {
    uint32_t pct = (uint32_t)light_raw() * 100u / 1023u;
    d.sens.light_pct = (uint8_t)(pct > 100 ? 100 : pct);
  }

  reel_quotes(d.q, now);
  d.sw.state = stw_state;
  d.sw.ms    = stw_elapsed(now);
  d.anim_frame = anim_tick;
  d.anim_scene = showreel_on ? (uint8_t)SCENE_SHOWREEL : roster_scene;
  d.sr_walk    = sr_walk_variant;
  // The four streams, published into the render snapshot. The renderer is
  // handed the schedule's answer; it never asks for one.
  for (uint8_t k = 0; k < 4; k++) {
    d.anim_ids[k]   = anim_ids[k];
    d.anim_phase[k] = anim_phase[k];
  }

  snapshot_settings(d.set);
}

// Only slot 1 carries the seconds overlay on the clock page, so it is the only
// one that needs repainting between minutes.
static const uint8_t CLOCK_SEC_MASK = 0x02;

void ui_paint(uint32_t now, const RtcTime &t, bool force) {
  bool ok_changed = (t.ok != last_ok);
  bool state_changed = force || dirty_screen || ok_changed ||
                       page != last_page || variant[page] != last_variant;

  bool min_changed = t.ok && (int8_t)t.min != last_min;
  bool sec_changed = t.ok && (int8_t)t.sec != last_sec;

  uint8_t mask = 0;
  switch (page) {
    case PG_CLOCK:
      // The cheapest correct policy: everything on the minute, and in between
      // only the panel that actually carries a second.
      //
      // `!t.ok` must NOT force a repaint every pass. With the RTC dead the
      // content is a constant "--", so it needs painting once — driving four
      // 1 KB frames every 50 ms instead asks for more than 100% of the bus.
      if (state_changed || min_changed) mask = 0x0F;
      else if (sec_changed)             mask = CLOCK_SEC_MASK;
      break;
    case PG_SENSOR:
      // Uptime ticks, so this one genuinely changes every second.
      if (state_changed || sec_changed) mask = 0x0F;
      break;
    case PG_STOPWATCH: {
      // ASK FOR ALL FOUR every displayed second (and on every RTC second, so
      // a paused or zero face still picks up a burn-in shift step within a
      // second of it happening). Asking is cheap: display.cpp's dirty check
      // compares the post-shift bytes, so in a normal second only the SECONDS
      // panel is actually pushed, and on the minute the minutes panel too.
      static uint32_t last_stw_s = 0xFFFFFFFFu;
      const uint32_t es = stw_elapsed(now) / 1000u;
      if (state_changed || sec_changed || es != last_stw_s) {
        mask = 0x0F;
        last_stw_s = es;
      }
      break;
    }
    default:
      if (state_changed) mask = 0x0F;
      break;
  }
  if (!mask) return;

  PageData d{};
  fill_pagedata(d, t, now);

  for (uint8_t i = 0; i < N_SCREENS; i++) {
    if (!(mask & (1 << i))) continue;
    page_render(disp_canvas(i), page, i, variant[page], d);
  }
  disp_commit(mask);

  last_ok  = t.ok;
  last_min = t.ok ? (int8_t)t.min : -1;
  last_sec = t.ok ? (int8_t)t.sec : -1;
  last_page = page;
  last_variant = variant[page];
  dirty_screen = false;
}

void ui_begin() {
  static const uint8_t PIN[4] = {BTN_MODE, BTN_SET, BTN_UP, BTN_DOWN};
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(PIN[i], INPUT_PULLUP);
    btn[i] = Btn{};
  }
  memset(variant, 0, sizeof variant);
  page = PG_CLOCK;
  last_input_ms = millis();
}
