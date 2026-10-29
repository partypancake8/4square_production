// ledcheck, the LED prover, a gate in the same sense the layout, store, graph
// and network provers are gates.
//
// WHY THIS EXISTS. On 2026-08-19 the user's report of the LED layer was "really
// choppy... It looks like it's flickering. I hate that." Three separate causes
// were found by reading the source, and all three were the kind of thing no
// amount of careful reading catches TWICE:
//
//   1. sensors.cpp blanked the whole strip for 25 ms, once a second, forever,
//      to read the LDR, a black notch punched into every animation.
//   2. The temporal dither in led_tick() is a 60 fps mechanism, and the real
//      cadence on the animation page is nearer 10 Hz, so it was generating the
//      flicker it exists to remove.
//   3. render() CUT between states, and half the gaits were square waves.
//
// Every one of those is a statement about NUMBERS OVER TIME, which is exactly
// what a host prover can see and a person staring at three LEDs cannot. So this
// compiles the FIRMWARE'S OWN leds.cpp, the source that ships, against a host
// model of the WS2812 (shim/Adafruit_NeoPixel.h), drives it at the design rate
// AND at the degraded rate the panels really leave it, and asserts:
//
//   - THE SLEW LAW: no channel moves faster than 4.5 units per millisecond of
//     wall time. Sampled at 1 ms, 16 ms and 100 ms. A hard cut is 255 in one
//     sample at any rate and cannot hide.
//   - LOOP CONTINUITY: an animation that wraps is exactly periodic and does not
//     jump at the seam.
//   - NO BLUE -> GREEN, ever: not between states, not within a state, not
//     during a transition. Encoded as "no frame this file emits may be
//     green-dominant", which is a rule that cannot be forgotten back in.
//   - THE DITHER LAW: at the degraded rate a constant input must give a
//     constant output. This is cause (2) stated as an assertion.
//   - HOLD BUDGETS: every state that is not a one-shot has one, and a lapsed
//     state renders dark.
//   - GAMMA EXACTLY ONCE, against an independent reference, plus the arithmetic
//     headroom the comment in led_tick() claims.
//   - strip.begin() called EXACTLY ONCE.
//
// What it structurally CANNOT see, and it is worth knowing which:
//   - the WS2812 itself: real timing, the 5 V buffer, a marginal joint. The
//     model always latches.
//   - the human question. "4.5 units per millisecond is smooth" is a judgement
//     (see SLEW_MAX_PER_MS below for how it was derived); the prover only holds
//     the code to it.
//   - disp_light_window(), which lives in display.cpp and is modelled here  - 
//     see the note on it below.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <array>
#include <algorithm>

#include "Arduino.h"
#include "Wire.h"
#include "Adafruit_NeoPixel.h"
#include "../../foursquare/src/app/leds.h"
#include "../../foursquare/src/settings/store.h"

unsigned long host_millis = 0;
bool          host_serial_quiet = true;
HostSerial    Serial;

uint8_t neo_latched[NEO_MAX_PIXELS][3];
int     neo_begin_calls = 0;
int     neo_shows       = 0;
uint8_t neo_brightness  = 0;
int     neo_pin         = -1;

// ---------------------------------------------------------------------------
// THE ONE MODELLED FUNCTION. disp_light_window() lives in display.cpp, which
// drags in Adafruit_SSD1306, bus.cpp and the whole panel stack, none of which
// has anything to do with what is being proved here. What leds.cpp actually
// wants from it is "the ambient window", and every test below holds the ambient
// reading at an END of that window (0 or 4095), where the exact endpoints
// cannot change the answer. So it is modelled, deliberately and visibly, and
// the prover asserts nothing about the window itself, that belongs to the
// display layer, which owns it.
void disp_light_window(uint16_t &lo, uint16_t &hi) { lo = 5; hi = 109; }

// ============================================================ the framework ==
static int  n_checks   = 0;
static int  n_failures = 0;
static bool expect_fail = false;   // selftest mode: a FALSE check is the pass
static int  n_caught    = 0;

static void check(bool ok, const char *what, int line) {
  n_checks++;
  if (expect_fail) {
    if (!ok) { n_caught++; return; }
    printf("  SELFTEST FAILURE line %d: '%s' should have failed and did not\n",
           line, what);
    n_failures++;
    return;
  }
  if (ok) return;
  printf("  FAIL line %d: %s\n", line, what);
  n_failures++;
}
#define CHECK(c, msg) check((c), (msg), __LINE__)

// ============================================================== the sampler ==
typedef std::array<int, 9> Frame;          // 3 fitted LEDs x r,g,b

static Frame readout() {
  Frame f{};
  for (int i = 0; i < 3; i++)
    for (int k = 0; k < 3; k++) f[i * 3 + k] = neo_latched[i][k];
  return f;
}

// The chain is driven eight long and populated three. Nothing may ever light
// past the third.
static bool tail_is_dark() {
  for (int i = 3; i < 8; i++)
    for (int k = 0; k < 3; k++) if (neo_latched[i][k]) return false;
  return true;
}

static void tick_at(uint32_t t, uint16_t amb, bool night) {
  host_millis = t;
  led_tick(t, amb, night);
}

static const uint16_t AMB_FULL = 4095;     // at or above the window's top

// Advance to a clean, dark, settled state at cadence `dt`, with the dither
// interval estimator converged on `dt` so a measurement is not contaminated by
// the estimator still catching up.
static void quiesce(uint32_t dt) {
  for (uint8_t s = 1; s < LED_COUNT; s++) led_hold((LedStatus)s, false);
  host_millis += 60000;                    // every one-shot has expired
  for (int i = 0; i < 60; i++) tick_at((uint32_t)host_millis + dt, AMB_FULL, false);
}

// ==================================================== the laws, as functions ==
//
// THE SLEW LAW. Every ramp in leds.cpp is at least RAMP_MIN_MS = 90 ms long,
// and smoothstep's peak slope is 1.5x its average, so the fastest any channel
// can legally move is 1.5 * 255 / 90 = 4.25 units per millisecond. 4.5 is that
// with a little room; the +3 covers integer rounding and the one-LSB temporal
// dither.
//
// WHY 90 ms IS "SMOOTH". It is about five frames at the 60 fps the code intends
// and one frame at the ~10 Hz the animation page actually delivers, so nothing
// in the column can ever appear or vanish inside a single delivered frame  - 
// which is the definition of the step the user was seeing. It is also short
// enough that a button acknowledgement still reads as instant: 90 ms is at the
// bottom of the range people report as "immediate". The old code's worst case
// was 255 in one sample, at any rate, from LED_CONFIRM's literal 0/255 square
// wave.
static const double SLEW_MAX_PER_MS = 4.5;
static int slew_bound(uint32_t dt_ms) {
  return (int)(SLEW_MAX_PER_MS * (double)dt_ms) + 3;
}

// Hue families, from the emitted RGB and nothing else. "Dominant" means clearly
// ahead of BOTH other channels, a 25% margin, so a near-white or near-grey
// frame belongs to no family and cannot trip the adjacency rule by rounding.
static bool green_dom(int r, int g, int b) { return g > 0 && g * 4 > r * 5 && g * 4 > b * 5; }
static bool blue_dom (int r, int g, int b) { return b > 0 && b * 4 > r * 5 && b * 4 > g * 5; }

static bool frame_has_green(const Frame &f) {
  for (int i = 0; i < 3; i++) if (green_dom(f[i*3], f[i*3+1], f[i*3+2])) return true;
  return false;
}
static bool frame_has_blue(const Frame &f) {
  for (int i = 0; i < 3; i++) if (blue_dom(f[i*3], f[i*3+1], f[i*3+2])) return true;
  return false;
}
// THE PAIRING THE USER BANNED. A blue or cyan frame immediately followed by a
// green one. It is checked as well as the absolute no-green rule because the
// two say different things: the absolute rule is how it is currently made
// impossible, this is what was actually wrong.
static bool blue_then_green(const Frame &a, const Frame &b) {
  return frame_has_blue(a) && frame_has_green(b);
}

struct Verdict {
  int      max_delta   = 0;      // worst per-sample channel step
  uint32_t max_at      = 0;      // when it happened
  bool     green       = false;
  bool     bg_adjacent = false;
  bool     tail_lit    = false;
  int      samples     = 0;
};

// Judge a recorded run. `dt` is the sampling interval the run was taken at.
static Verdict judge(const std::vector<Frame> &f, const std::vector<uint32_t> &t) {
  Verdict v;
  v.samples = (int)f.size();
  for (size_t i = 0; i < f.size(); i++) {
    if (frame_has_green(f[i])) v.green = true;
    if (i) {
      if (blue_then_green(f[i-1], f[i])) v.bg_adjacent = true;
      for (int k = 0; k < 9; k++) {
        int d = abs(f[i][k] - f[i-1][k]);
        if (d > v.max_delta) { v.max_delta = d; v.max_at = t[i]; }
      }
    }
  }
  return v;
}

// ============================================================== the run rig ==
struct Run { std::vector<Frame> f; std::vector<uint32_t> t; bool tail_lit = false; };

// Hold or fire `s`, sample it for `dur` ms at `dt`, then let it go and keep
// sampling for `tail` ms so the return to idle is judged by the same law.
static Run run_state(uint8_t s, uint32_t dur, uint32_t dt, uint32_t tail,
                     bool night = false, bool as_hold = true) {
  quiesce(dt);
  Run r;
  uint32_t t0 = (uint32_t)host_millis;
  if (as_hold) led_hold((LedStatus)s, true);
  else         led_fire((LedStatus)s);
  for (uint32_t e = 0; e <= dur; e += dt) {
    tick_at(t0 + e, AMB_FULL, night);
    r.f.push_back(readout());
    r.t.push_back(e);
    if (!tail_is_dark()) r.tail_lit = true;
  }
  if (as_hold) led_hold((LedStatus)s, false);
  for (uint32_t e = dt; e <= tail; e += dt) {
    tick_at(t0 + dur + e, AMB_FULL, night);
    r.f.push_back(readout());
    r.t.push_back(dur + e);
    if (!tail_is_dark()) r.tail_lit = true;
  }
  return r;
}

// ================================================================ the states ==
// Duplicated from leds.cpp on purpose, like storecheck's ring constants: a test
// that imported the table it is checking would agree with any change to it,
// including a wrong one. `period` is 0 for anything that does not loop.
struct StateDesc { uint8_t s; const char *name; uint32_t period; };
static const StateDesc STATES[] = {
  { LED_WIFI_UP,      "WIFI_UP",      0    },
  { LED_ACK_MODE,     "ACK_MODE",     0    },
  { LED_ACK_SET,      "ACK_SET",      0    },
  { LED_ACK_UP,       "ACK_UP",       0    },
  { LED_ACK_DOWN,     "ACK_DOWN",     0    },
  { LED_CONFIRM,      "CONFIRM",      0    },
  { LED_REJECT,       "REJECT",       0    },
  { LED_MENU,         "MENU",         4200 },   // g_ember
  { LED_BOOT,         "BOOT",         0    },
  { LED_WIFI_FAIL,    "WIFI_FAIL",    1750 },   // g_group(1): 550 + 1200
  { LED_WIFI_JOINING, "WIFI_JOINING", 1700 },   // g_comet
  { LED_RTC_UNSET,    "RTC_UNSET",    2300 },   // g_group(2)
  { LED_FAULT_I2C,    "FAULT_I2C",    2850 },   // g_group(3)
  { LED_IDENTIFY,     "IDENTIFY",     1900 },   // g_ripple
  { LED_OTA_FAIL,     "OTA_FAIL",     3400 },   // g_group(4)
  { LED_OTA_ACTIVE,   "OTA_ACTIVE",   1000 },   // g_bar's leading-pixel breathe
};
static const int N_STATES = (int)(sizeof(STATES) / sizeof(STATES[0]));

// ===================================================================== tests ==
static int worst_at[3];            // worst per-sample delta seen at each rate
static const uint32_t RATES[3] = { 1, 16, 100 };

// 1 ms is the continuity rate: at that resolution ANY step in the underlying
// function shows up whole, so it is the sample that catches a hard cut, a
// square wave or a loop seam. 16 ms is the design rate. 100 ms is what the
// animation page really delivers, and a gait that is only smooth at 60 fps has
// not solved the user's problem.
static void t_smoothness() {
  printf("smoothness, every state, at 1 ms, 16 ms and 100 ms\n");
  for (int ri = 0; ri < 3; ri++) {
    uint32_t dt = RATES[ri];
    int bound = slew_bound(dt);
    int worst = 0;
    const char *worst_name = "-";
    for (int i = 0; i < N_STATES; i++) {
      const StateDesc &d = STATES[i];
      bool hold = (led_oneshot_ms(d.s) == 0);
      // Long enough for at least two full loops of anything that loops, and for
      // the whole of anything that does not.
      uint32_t dur = hold ? std::max<uint32_t>(9000, d.period * 3)
                          : led_oneshot_ms(d.s) + 200;
      if (d.s == LED_OTA_ACTIVE) led_ota_progress(55);   // a partial bar breathes
      Run r = run_state(d.s, dur, dt, 800, false, hold);
      Verdict v = judge(r.f, r.t);
      char msg[160];
      snprintf(msg, sizeof msg,
               "%s at %u ms: per-sample step %d must be <= %d (at t=%u)",
               d.name, dt, v.max_delta, bound, v.max_at);
      CHECK(v.max_delta <= bound, msg);
      snprintf(msg, sizeof msg, "%s: no frame may be green-dominant", d.name);
      CHECK(!v.green, msg);
      snprintf(msg, sizeof msg, "%s: no blue frame may be followed by a green one", d.name);
      CHECK(!v.bg_adjacent, msg);
      snprintf(msg, sizeof msg, "%s: LEDs 4-8 are not fitted and must stay dark", d.name);
      CHECK(!r.tail_lit, msg);
      if (v.max_delta > worst) { worst = v.max_delta; worst_name = d.name; }
      if (dt != 1) printf("      %-13s %3d\n", d.name, v.max_delta);
    }
    worst_at[ri] = worst;
    printf("    %3u ms sampling: worst per-sample step %3d (bound %3d, %s)\n",
           dt, worst, bound, worst_name);
  }
}

// An animation that wraps must be EXACTLY periodic and must not jump at the
// seam. The seam is the same class of bug as the square wave: a shape that is
// beautiful for 1699 ms and then teleports.
static void t_loops() {
  printf("loop seams, a wrap must be periodic and continuous\n");
  for (int i = 0; i < N_STATES; i++) {
    const StateDesc &d = STATES[i];
    if (!d.period) continue;
    if (led_oneshot_ms(d.s) != 0) continue;         // held states only
    char msg[160];
    // ---- exactly periodic, sampled at 50 ms ---------------------------------
    // AT 50 ms BECAUSE THE DITHER IS OFF THERE, and the dither is deliberately
    // history-dependent: it carries a remainder between frames, so a gait whose
    // colour is not a whole multiple of 1/256 is genuinely not bit-periodic
    // while it is running. That is the mechanism working, not a defect. Every
    // period in the table is a multiple of 50, so this samples the seam exactly.
    {
      quiesce(50);
      if (d.s == LED_OTA_ACTIVE) led_ota_progress(55);
      uint32_t t0 = (uint32_t)host_millis;
      led_hold((LedStatus)d.s, true);
      Frame at_p{}, at_2p{};
      for (uint32_t e = 0; e <= 2 * d.period; e += 50) {
        tick_at(t0 + e, AMB_FULL, false);
        if (e == d.period)     at_p  = readout();
        if (e == 2 * d.period) at_2p = readout();
      }
      led_hold((LedStatus)d.s, false);
      snprintf(msg, sizeof msg, "%s: the animation must repeat exactly at %u ms",
               d.name, d.period);
      CHECK(at_p == at_2p, msg);
    }
    // ---- continuous across the seam, sampled at 1 ms ------------------------
    // The slew bound at 1 ms already covers the dither's one LSB, so this can
    // run at the rate that would expose a jump.
    {
      quiesce(1);
      if (d.s == LED_OTA_ACTIVE) led_ota_progress(55);
      uint32_t t0 = (uint32_t)host_millis;
      led_hold((LedStatus)d.s, true);
      std::vector<Frame> f(2 * d.period + 2);
      for (uint32_t e = 0; e <= 2 * d.period + 1; e++) {
        tick_at(t0 + e, AMB_FULL, false);
        f[e] = readout();
      }
      led_hold((LedStatus)d.s, false);
      int seam = 0;
      for (uint32_t e = 2 * d.period - 2; e <= 2 * d.period + 1; e++)
        for (int k = 0; k < 9; k++)
          seam = std::max(seam, abs(f[e][k] - f[e - 1][k]));
      snprintf(msg, sizeof msg, "%s: the wrap must not jump (%d <= %d)",
               d.name, seam, slew_bound(1));
      CHECK(seam <= slew_bound(1), msg);
    }
  }
}

// THE CHECK THE USER ASKED FOR, stated as a matrix. Every ordered pair of
// states, driven through the real transition, at the design rate and at the
// degraded one. This is what a hard cut cannot survive, and it is where the
// blue->green pairing lived: WIFI_UP (cyan) -> CONFIRM, and WIFI_JOINING's cyan
// comet -> CONFIRM, both of which were one frame apart.
static void t_transitions() {
  printf("transitions, every ordered pair of states\n");
  for (int ri = 1; ri < 3; ri++) {            // 16 ms and 100 ms
    uint32_t dt = RATES[ri];
    int bound = slew_bound(dt);
    int worst = 0, pairs = 0, green = 0, adj = 0;
    for (int a = 0; a < N_STATES; a++) {
      for (int b = 0; b < N_STATES; b++) {
        if (a == b) continue;
        quiesce(dt);
        led_ota_progress(55);
        uint32_t t0 = (uint32_t)host_millis;
        std::vector<Frame> f; std::vector<uint32_t> tv;
        // Let A establish itself, then assert B on top of it. B may or may not
        // win the priority order, either way the pair is exercised.
        led_hold((LedStatus)STATES[a].s, true);
        for (uint32_t e = 0; e <= 900; e += dt) {
          tick_at(t0 + e, AMB_FULL, false); f.push_back(readout()); tv.push_back(e);
        }
        led_hold((LedStatus)STATES[b].s, true);
        for (uint32_t e = 900 + dt; e <= 2200; e += dt) {
          tick_at(t0 + e, AMB_FULL, false); f.push_back(readout()); tv.push_back(e);
        }
        led_hold((LedStatus)STATES[a].s, false);
        led_hold((LedStatus)STATES[b].s, false);
        for (uint32_t e = 2200 + dt; e <= 3000; e += dt) {
          tick_at(t0 + e, AMB_FULL, false); f.push_back(readout()); tv.push_back(e);
        }
        Verdict v = judge(f, tv);
        if (v.max_delta > worst) worst = v.max_delta;
        if (v.green) green++;
        if (v.bg_adjacent) adj++;
        pairs++;
      }
    }
    char msg[160];
    snprintf(msg, sizeof msg,
             "at %u ms: the worst step across %d state transitions is %d, bound %d",
             dt, pairs, worst, bound);
    CHECK(worst <= bound, msg);
    snprintf(msg, sizeof msg, "at %u ms: no transition may emit a green-dominant frame", dt);
    CHECK(green == 0, msg);
    snprintf(msg, sizeof msg, "at %u ms: no transition may put blue next to green", dt);
    CHECK(adj == 0, msg);
    printf("    %3u ms: %d ordered pairs, worst step %d (bound %d), "
           "%d green, %d blue->green\n", dt, pairs, worst, bound, green, adj);
  }
}

// THE DITHER LAW, cause (2) of the choppiness, as an assertion.
//
// Held OTA at 100% is the only genuinely CONSTANT gait in the file: g_bar fills
// all three and stops. So the input to the dither is a constant, and what comes
// out says everything. led_bright is set low on purpose: that is where the
// dither actually does something, and where its failure mode lives.
static void t_dither() {
  printf("dither, a constant input must give a constant output at the real rate\n");
  uint8_t saved = cfg.led_bright;
  cfg.led_bright = 20;

  // Degraded rate: the carry is 100 ms away and would be a 10 Hz square wave on
  // the bottom bit. It must degrade to plain rounding instead.
  quiesce(100);
  led_ota_progress(100);
  uint32_t t0 = (uint32_t)host_millis;
  led_hold(LED_OTA_ACTIVE, true);
  Frame first{}; int moved = 0;
  for (uint32_t e = 0; e <= 12000; e += 100) {
    tick_at(t0 + e, AMB_FULL, false);
    Frame f = readout();
    if (e == 1000) first = f;                 // past the fade-in
    if (e > 1000 && f != first) moved++;
  }
  led_hold(LED_OTA_ACTIVE, false);
  CHECK(moved == 0, "at 100 ms a constant gait must produce a bit-exact "
                    "constant frame, any movement is the 10 Hz dither strobe");

  // Design rate: the dither is the right tool and must still be doing its job.
  quiesce(16);
  led_ota_progress(100);
  t0 = (uint32_t)host_millis;
  led_hold(LED_OTA_ACTIVE, true);
  std::vector<Frame> f; std::vector<uint32_t> tv;
  for (uint32_t e = 0; e <= 4000; e += 16) {
    tick_at(t0 + e, AMB_FULL, false);
    if (e >= 400) { f.push_back(readout()); tv.push_back(e); }
  }
  led_hold(LED_OTA_ACTIVE, false);
  Verdict v = judge(f, tv);
  CHECK(v.max_delta <= 1, "at 16 ms the dither may only ever move a channel by "
                          "one LSB");
  bool varies = false;
  for (size_t i = 1; i < f.size(); i++) if (f[i] != f[0]) { varies = true; break; }
  CHECK(varies, "at 16 ms the dither must actually be running, a dither that "
                "is off everywhere passes the law above for the wrong reason");
  cfg.led_bright = saved;
}

// Every state that is not a one-shot has a budget, and a lapsed one goes dark
// while STAYING ASSERTED. held_since is stamped only on the false->true edge,
// so a fault that flickers cannot re-arm its own budget.
static void t_budgets() {
  printf("hold budgets, a held state is a notification, not a status bar\n");
  for (uint8_t s = 1; s < LED_COUNT; s++) {
    char msg[120];
    if (led_oneshot_ms(s) == 0) {
      snprintf(msg, sizeof msg, "state %u is held and must have a hold budget", s);
      CHECK(led_hold_max_ms(s) > 0, msg);
    } else {
      snprintf(msg, sizeof msg, "state %u is a one-shot and needs no hold budget", s);
      CHECK(led_hold_max_ms(s) == 0, msg);
    }
  }
  // A lapsed state renders dark.
  quiesce(16);
  uint32_t t0 = (uint32_t)host_millis;
  led_hold(LED_FAULT_I2C, true);
  for (uint32_t e = 0; e <= 2000; e += 16) tick_at(t0 + e, AMB_FULL, false);
  CHECK(!led_is_dark(), "a fresh fault must actually light the column");
  uint32_t cap = led_hold_max_ms(LED_FAULT_I2C);
  for (uint32_t e = cap + 500; e <= cap + 2000; e += 16) tick_at(t0 + e, AMB_FULL, false);
  Frame f = readout();
  bool lit = false; for (int k = 0; k < 9; k++) if (f[k]) lit = true;
  CHECK(!lit, "a lapsed hold must stop being RENDERED");
  CHECK(led_is_dark(), "a lapsed hold must stop driving the wire at all");
  // Still asserted: re-arming is only possible on a fresh false->true edge.
  led_hold(LED_FAULT_I2C, true);                 // no edge, already true
  for (uint32_t e = cap + 2016; e <= cap + 3000; e += 16) tick_at(t0 + e, AMB_FULL, false);
  f = readout(); lit = false; for (int k = 0; k < 9; k++) if (f[k]) lit = true;
  CHECK(!lit, "re-asserting a still-true hold must not re-arm its budget");
  led_hold(LED_FAULT_I2C, false);
}

// GAMMA EXACTLY ONCE, against a reference written here from scratch. The level
// and colour are duplicated from leds.cpp on purpose, see the STATES note.
static void t_gamma() {
  printf("gamma, applied exactly once, on the master scalar\n");
  // LED_MENU is C_AMBER (255,120,0) running g_ember: g_breathe(t, 4200, 26, 74).
  // At t = 2100 the triangle is at its peak, so the level is exactly 74.
  const int COL[3] = {255, 120, 0};
  const int L = 74;
  uint8_t saved = cfg.led_bright;
  const int BRIGHTS[] = {255, 200, 128, 64, 32};
  int differs_from_double = 0;
  for (int bi = 0; bi < 5; bi++) {
    int B = BRIGHTS[bi];
    cfg.led_bright = (uint8_t)B;
    quiesce(100);                       // dither off, so this is deterministic
    uint32_t t0 = (uint32_t)host_millis;
    led_hold(LED_MENU, true);
    for (uint32_t e = 0; e <= 2100; e += 100) tick_at(t0 + e, AMB_FULL, false);
    Frame f = readout();
    led_hold(LED_MENU, false);

    // Reference: master = B (ambient is at the top of the window), gamma 2.2
    // applied ONCE to the master scalar, folded to 8 bits, then the colour and
    // the level.
    uint32_t g16 = (uint32_t)(65535.0f * powf((float)B / 255.0f, 2.2f));
    uint32_t g8  = (g16 + 128) >> 8;
    // And the same arithmetic with gamma applied TWICE, which is what this test
    // exists to tell apart.
    uint32_t g16d = (uint32_t)(65535.0f * powf(powf((float)B / 255.0f, 2.2f), 2.2f));
    uint32_t g8d  = (g16d + 128) >> 8;
    bool all_ok = true, any_diff = false;
    for (int k = 0; k < 3; k++) {
      uint32_t once = ((uint32_t)COL[k] * L * g8 / 255 + 128) >> 8;
      uint32_t twice = ((uint32_t)COL[k] * L * g8d / 255 + 128) >> 8;
      if ((uint32_t)f[k] != once) all_ok = false;
      if (once != twice) any_diff = true;
    }
    char msg[140];
    snprintf(msg, sizeof msg,
             "at led_bright %d the top LED must be (%d,%d,%d), gamma once",
             B, f[0], f[1], f[2]);
    CHECK(all_ok, msg);
    if (any_diff) differs_from_double++;
  }
  CHECK(differs_from_double >= 3,
        "the reference must actually distinguish one gamma from two, or the "
        "check above proves nothing");
  cfg.led_bright = saved;
}

// The headroom the comment in led_tick() claims. The one-expression form it
// replaced peaked at 4,261,413,375 against a uint32 ceiling of 4,294,967,295  - 
// 0.8% of margin. This pins the margin so a future edit cannot quietly eat it.
static void t_headroom() {
  printf("arithmetic, no channel may approach its type's ceiling\n");
  const uint64_t worst_ch16 = (uint64_t)255 * 255 * 256 / 255;   // col * l * g8 / 255
  const uint64_t worst_prod = (uint64_t)255 * 255 * 256;         // before the divide
  CHECK(worst_ch16 <= 65535, "ch16 must stay inside 16 bits so >>8 lands in a byte");
  CHECK(worst_prod * 250 < 4294967295ULL,
        "the widest intermediate must keep at least 250x of uint32 headroom");
  const uint64_t old_form = (uint64_t)255 * 255 * 65535;
  CHECK(old_form * 250 >= 4294967295ULL,
        "the OLD one-expression form must fail that margin, or it is not a test");
}

// IDLE IS DARK, and dark means not driving the wire either.
static void t_idle() {
  printf("idle, dark, and silent on the wire\n");
  quiesce(16);
  Frame f = readout();
  bool lit = false; for (int k = 0; k < 9; k++) if (f[k]) lit = true;
  CHECK(!lit, "with nothing asserted the column is dark");
  CHECK(led_is_dark(), "and the dark latch says so");
  int before = neo_shows;
  for (uint32_t e = 0; e < 200; e++) tick_at((uint32_t)host_millis + 16, AMB_FULL, false);
  CHECK(neo_shows == before, "an idle tick must not push a frame down the wire");

  // led_mode 0 is dark whatever is asserted.
  cfg.led_mode = 0;
  led_hold(LED_FAULT_I2C, true);
  for (uint32_t e = 0; e < 30; e++) tick_at((uint32_t)host_millis + 16, AMB_FULL, false);
  f = readout(); lit = false; for (int k = 0; k < 9; k++) if (f[k]) lit = true;
  CHECK(!lit, "led_mode 0 is off, whatever is asserted");
  cfg.led_mode = 1;
  led_hold(LED_FAULT_I2C, false);
}

// The priority order is the contract: an OTA must never be masked by a WiFi
// animation, and a bus fault must never be masked by a button acknowledgement.
static void t_priority() {
  printf("priority, the highest number wins, always\n");
  quiesce(16);
  uint32_t t0 = (uint32_t)host_millis;
  led_hold(LED_MENU, true);                 // amber, b = 0
  led_hold(LED_FAULT_I2C, true);            // magenta, b = 160
  int max_b = 0;
  for (uint32_t e = 0; e <= 3000; e += 16) {
    tick_at(t0 + e, AMB_FULL, false);
    Frame f = readout();
    for (int i = 0; i < 3; i++) max_b = std::max(max_b, f[i*3+2]);
  }
  CHECK(max_b > 0, "a bus fault outranks the menu and must show its own colour");
  led_hold(LED_MENU, false); led_hold(LED_FAULT_I2C, false);
}

// Night: some states are suppressed outright, and everything else is capped.
static void t_night() {
  printf("night, a fault at 3 am must not light a bedroom\n");
  Run r = run_state(LED_FAULT_I2C, 4000, 16, 400, true);
  int peak = 0;
  for (auto &f : r.f) for (int k = 0; k < 9; k++) peak = std::max(peak, f[k]);
  CHECK(peak == 0, "LED_FAULT_I2C is suppressed overnight");

  // LED_IDENTIFY is NOT on the suppression list, you asked for it, but it is
  // still capped. By day it is a real light; overnight it must be a glow at
  // most. Both halves are asserted, because "dark at night" is only meaningful
  // next to "lit by day": a state that was accidentally dark all the time would
  // otherwise sail through.
  Run day = run_state(LED_IDENTIFY, 4000, 16, 400, false, false);
  int day_peak = 0;
  for (auto &f : day.f) for (int k = 0; k < 9; k++) day_peak = std::max(day_peak, f[k]);
  CHECK(day_peak > 40, "LED_IDENTIFY is a real light by day");

  r = run_state(LED_IDENTIFY, 4000, 16, 400, true, false);
  peak = 0;
  for (auto &f : r.f) for (int k = 0; k < 9; k++) peak = std::max(peak, f[k]);
  // OBSERVED, not assumed: master is capped at 12 overnight and gamma 2.2 takes
  // (12/255)^2.2 * 65535 to 79, which folds to a g8 of 0, so the night cap
  // currently quantises every state to fully off, not to a dim glow. That is
  // the display layer's number (NIGHT_CAP in led_tick) and not this change's to
  // move; it is pinned here so that if anyone ever raises the cap, they find
  // out from a gate rather than from a lit bedroom.
  CHECK(peak <= 4, "overnight everything is capped to a glow at most");
}

// The blank light_sample() takes. It must not leave the strip dark, and it must
// not touch the wire at all when there was nothing lit to blank.
static void t_light_blank() {
  printf("light blank, the LED image is continuous across the ADC read\n");
  quiesce(16);
  CHECK(led_is_dark(), "idle: the strip is dark before the sample");
  int before = neo_shows;
  CHECK(led_light_blank_begin() == false,
        "an already-dark strip needs no blank, no settle and no restore");
  CHECK(neo_shows == before, "and no frame is pushed for it");

  // Lit: the frame that was showing must come back, bit for bit.
  uint32_t t0 = (uint32_t)host_millis;
  led_hold(LED_IDENTIFY, true);
  for (uint32_t e = 0; e <= 2000; e += 16) tick_at(t0 + e, AMB_FULL, false);
  Frame lit_frame = readout();
  bool any = false; for (int k = 0; k < 9; k++) if (lit_frame[k]) any = true;
  CHECK(any, "the column is lit going into the sample");
  CHECK(led_light_blank_begin() == true, "a lit strip does get blanked");
  Frame blanked = readout();
  bool dark = true; for (int k = 0; k < 9; k++) if (blanked[k]) dark = false;
  CHECK(dark, "and the ADC really does see an unlit strip");
  led_light_blank_end();
  CHECK(readout() == lit_frame,
        "and the exact frame that was showing comes back, no black notch");
  CHECK(!led_is_dark(), "the dark latch is cleared again afterwards");
  led_hold(LED_IDENTIFY, false);
}

// strip.begin() EXACTLY ONCE. On this chip the RMT binding never comes back.
static void t_begin_once() {
  printf("the strip, bound once and never touched again\n");
  CHECK(neo_begin_calls == 1, "strip.begin() is called exactly once, in led_begin()");
  CHECK(neo_brightness == 255,
        "setBrightness must be 255, anything less throws away the bits the "
        "16-bit master scalar and the dither depend on");
  CHECK(neo_pin == PIN_LED, "and it is bound to PIN_LED");
}

// A one-shot must finish, and finish DARK, inside its own budget plus the fade.
static void t_oneshots_end() {
  printf("one-shots, every one ends, and ends dark\n");
  for (int i = 0; i < N_STATES; i++) {
    const StateDesc &d = STATES[i];
    uint32_t os = led_oneshot_ms(d.s);
    if (!os) continue;
    Run r = run_state(d.s, os + 1200, 16, 0, false, false);
    Frame f = r.f.back();
    bool lit = false; for (int k = 0; k < 9; k++) if (f[k]) lit = true;
    char msg[120];
    snprintf(msg, sizeof msg, "%s must be dark %u ms after it was fired",
             d.name, os + 1200);
    CHECK(!lit, msg);
  }
}

// ================================================================= selftest ==
// A prover that cannot fail is worse than none. Every law above is a function,
// and this runs those same functions against data that is known to break them.
// If this block ever passes silently, the rest of the file is decoration.
static void selftest() {
  printf("selftest\n");
  int before_caught = n_caught;
  expect_fail = true;
  const int PLANTED = 8;

  // 1. the hue classifier
  CHECK(!green_dom(0, 255, 0), "planted: pure green must classify as green-dominant");
  // 2. and it must not be so loose that cyan trips it
  CHECK(green_dom(0, 190, 255), "planted: cyan is blue-dominant, not green");
  // 3. the banned pairing, on synthetic frames
  {
    Frame blue{}; blue[0] = 0; blue[1] = 190; blue[2] = 255;
    Frame grn{};  grn[0] = 0;  grn[1] = 255;  grn[2] = 0;
    CHECK(!blue_then_green(blue, grn), "planted: cyan followed by green is the "
                                       "pairing this file exists to forbid");
  }
  // 4. the slew law, on a synthetic hard cut, the exact shape of the old
  //    LED_CONFIRM square wave.
  {
    std::vector<Frame> f(4); std::vector<uint32_t> t = {0, 16, 32, 48};
    for (int k = 0; k < 9; k++) { f[0][k] = 0; f[1][k] = 255; f[2][k] = 0; f[3][k] = 255; }
    Verdict v = judge(f, t);
    CHECK(v.max_delta <= slew_bound(16), "planted: a 0/255 square wave must "
                                         "violate the slew law at 16 ms");
  }
  // 5. and at the degraded rate too, where a naive per-tick bound goes blind
  {
    std::vector<Frame> f(3); std::vector<uint32_t> t = {0, 100, 200};
    for (int k = 0; k < 9; k++) { f[0][k] = 0; f[1][k] = 255; f[2][k] = 0; }
    Verdict v = judge(f, t);
    CHECK(v.max_delta <= 3, "planted: a square wave sampled at 100 ms is still "
                            "a square wave");
  }
  // 6. the loop-seam detector
  {
    std::vector<Frame> f(2); std::vector<uint32_t> t = {0, 1};
    for (int k = 0; k < 9; k++) { f[0][k] = 200; f[1][k] = 20; }
    Verdict v = judge(f, t);
    CHECK(v.max_delta <= slew_bound(1), "planted: a 180-count jump in 1 ms is a "
                                        "discontinuity");
  }
  // 7. the budget law
  CHECK(led_hold_max_ms(LED_MENU) == 0, "planted: LED_MENU has a hold budget");
  // 8. the begin-once counter
  CHECK(neo_begin_calls == 2, "planted: strip.begin() ran once, not twice");

  expect_fail = false;
  int caught = n_caught - before_caught;
  n_checks -= PLANTED;               // the planted ones are not real coverage
  char msg[100];
  snprintf(msg, sizeof msg, "the selftest must catch all %d planted failures "
           "(caught %d)", PLANTED, caught);
  CHECK(caught == PLANTED, msg);
  printf("    %d planted failures, %d caught\n", PLANTED, caught);
}

int main() {
  printf("\n========================= LED PROVER ==========================\n");
  eesim_reset();
  settings_load();
  cfg.led_mode   = 1;
  cfg.led_bright = 255;
  led_begin();

  selftest();
  t_begin_once();
  t_idle();
  t_smoothness();
  t_loops();
  t_transitions();
  t_dither();
  t_budgets();
  t_oneshots_end();
  t_gamma();
  t_headroom();
  t_priority();
  t_night();
  t_light_blank();

  printf("\n  worst per-sample step:  %d at 1 ms, %d at 16 ms, %d at 100 ms\n",
         worst_at[0], worst_at[1], worst_at[2]);
  printf("  states covered:         %d\n", N_STATES);
  printf("  checks run:             %d\n", n_checks);
  if (n_failures) {
    printf("\nRESULT: FAIL, %d of %d checks failed\n\n", n_failures, n_checks);
    return 1;
  }
  printf("\nRESULT: PASS\n\n");
  return 0;
}
