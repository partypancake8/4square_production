// layoutcheck — prove, pixel by pixel, that nothing on any screen overlaps
// anything else on that screen, and that everything stays inside the area the
// anti-burn-in shift is allowed to move it around in.
//
// HOW IT PROVES RATHER THAN CHECKS
//
// The naive version of this tool renders a screen and looks at the result. That
// cannot detect an overlap at all: two elements that share a pixel produce a
// single lit pixel, indistinguishable from one element lighting it. By the time
// you have an image, the evidence is gone.
//
// So instead each element is lifted into its own bitmap as it is drawn. The
// firmware marks element boundaries with ELEM(); this tool installs a hook on
// that marker which snapshots and clears the canvas at every boundary. What
// comes out is N disjoint-by-construction bitmaps whose pairwise AND is the
// overlap, exactly and with coordinates.
//
// It compiles faces.cpp and ui_render.cpp — THE SOURCE THAT SHIPS — against the
// real Adafruit_GFX and the real 5x7 font. Nothing about the geometry is
// reimplemented here, so a pass is a statement about the device and not about
// a model of it.
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>

#include "faces.h"
#include "pages.h"
#include "anim.h"
#include "display.h"
#include "shift_tour.h"   // the shipping shift path, proven in selftest()
#include "blit_map.h"     // the shipping canvas->panel map, likewise

HostSerial Serial;

// ------------------------------------------------------------ element capture
static const int WBYTES = (SCR_W + 7) / 8;
typedef std::vector<uint8_t> Bits;

struct Elem { std::string name; Bits bits; };

static GFXcanvas1 *g_target = nullptr;
static std::vector<Elem> g_elems;
static std::string g_cur;

static bool bit_get(const Bits &b, int x, int y) {
  return (b[(size_t)y * WBYTES + (x >> 3)] >> (7 - (x & 7))) & 1;
}
static bool bits_any(const Bits &b) {
  for (uint8_t v : b) if (v) return true;
  return false;
}

// Everything currently on the canvas belongs to whichever element was named
// last. Take it, then wipe the canvas so the next element starts clean.
static void flush_current() {
  if (!g_target) return;
  Bits b(g_target->getBuffer(), g_target->getBuffer() + (size_t)WBYTES * SCR_H);
  if (bits_any(b)) g_elems.push_back({g_cur.empty() ? "<unnamed>" : g_cur, b});
  g_target->fillScreen(0);
}

static void elem_hook(const char *name) {
  flush_current();
  g_cur = name ? name : "?";
}

// ------------------------------------------------------------------- results
struct Failure { std::string kase, detail; };
static std::vector<Failure> failures, warnings;
static long n_cases = 0, n_frames_dumped = 0;

// ---- burn-in budgets, and where each number comes from ---------------------
// FILL_BUDGET_PCT is Wear OS WO-P7 (see judge() case 4) — an external, cited,
// enforced number, not a house rule.
// SHIFT_AMP_UNDER_TEST is the shipping default in defaults.cpp; the gate proves
// the setting the device actually runs, not the best one available.
// RELIEF_FLOOR is ours: below 1.10x the shift is doing nothing worth the code
// that implements it, and a mitigation that reads as "on" while relieving
// nothing is the exact failure firmware B shipped.
static const double FILL_BUDGET_PCT       = 15.0;
static const int    SHIFT_AMP_UNDER_TEST  = 6;
static const double RELIEF_FLOOR          = 1.10;
// The debt this gate inherited, measured 2026-08-11 on firmware C. These are
// NOT approvals — they are the high-water mark that must never rise. Lower
// them whenever a layout change pays some of it back.
// PAID DOWN 2026-08-19, from 109 screens / 23.2%, by naming the style in the
// clock page's label: the whole of that "debt" was the opt-in stops of the
// MODE cycle being booked as default-style screens because the label never
// said which style it was. Nothing about the layouts changed; the number is
// simply now true, and the measured figure today is ZERO default-style screens
// over budget.
//
// Held at 16 / 15.5 rather than 0 / 15.0 ONLY because the markets page is
// being rewritten in parallel as this lands and its final fill is not yet
// known. TAKE THESE TO 0 AND 15.0 once that settles — the gate is worth more
// with no slack in it.
static const long   DEBT_SCREENS_MAX      = 16;
static const double DEBT_WORST_PCT        = 15.5;
// ---- fill factor PER STOP OF THE MODE CYCLE --------------------------------
// The aggregate "worst screen" figure names one stop and hides the other
// thirteen. Fill factor is a burn-in budget the user spends by pressing MODE,
// so the number that matters is per LOOK: which stop costs the panel what, on
// its worst value, so a look can be judged rather than merely totalled. Keyed
// off the clock page's own label, which already names the style.
struct LookFill { std::string name; double worst; std::string worst_case; long over; };
static std::vector<LookFill> look_fill;
static void look_note(const std::string &kase, double ff, double budget) {
  static const char *P = "page CLOCK v";
  if (kase.rfind(P, 0) != 0) return;
  size_t a = kase.find(' ', strlen(P));          // after "v<N>"
  if (a == std::string::npos) return;
  size_t b = kase.find(" slot", a);
  if (b == std::string::npos) return;
  std::string name = kase.substr(a + 1, b - a - 1);
  for (auto &l : look_fill)
    if (l.name == name) {
      if (ff > l.worst) { l.worst = ff; l.worst_case = kase; }
      if (ff > budget) l.over++;
      return;
    }
  look_fill.push_back({name, ff, kase, ff > budget ? 1L : 0L});
}

static double fill_sum = 0.0, fill_max = 0.0, relief_min = 1e9;
static long   n_budget_cases = 0, fill_over_optin = 0, n_over_budget = 0;
static double over_max = 0.0;
static std::string fill_worst, relief_worst;
static FILE *dump_bin = nullptr;
static FILE *dump_txt = nullptr;

static void dump_frame(const std::string &label, const Bits &composite) {
  if (!dump_bin) return;
  fwrite(composite.data(), 1, composite.size(), dump_bin);
  fprintf(dump_txt, "%s\n", label.c_str());
  n_frames_dumped++;
}

// Dilate by one pixel in the four cardinal directions. Used for the
// "uncomfortably close" warning — elements that do not overlap but sit with no
// gap at all read as one smudged blob on a 128x64 panel.
static Bits dilate(const Bits &b) {
  Bits o(b.size(), 0);
  for (int y = 0; y < SCR_H; y++)
    for (int x = 0; x < SCR_W; x++) {
      if (!bit_get(b, x, y)) continue;
      for (int k = 0; k < 5; k++) {
        static const int dx[5] = {0, -1, 1, 0, 0}, dy[5] = {0, 0, 0, -1, 1};
        int nx = x + dx[k], ny = y + dy[k];
        if (nx < 0 || ny < 0 || nx >= SCR_W || ny >= SCR_H) continue;
        o[(size_t)ny * WBYTES + (nx >> 3)] |= (uint8_t)(0x80 >> (nx & 7));
      }
    }
  return o;
}

// The whole check for one rendered screen.
static void judge(const std::string &kase, bool dump) {
  n_cases++;
  Bits composite((size_t)WBYTES * SCR_H, 0);
  for (const Elem &e : g_elems)
    for (size_t i = 0; i < composite.size(); i++) composite[i] |= e.bits[i];

  // 1. OVERLAP — the headline check. Any pixel claimed by two elements.
  for (size_t i = 0; i < g_elems.size(); i++)
    for (size_t j = i + 1; j < g_elems.size(); j++) {
      int count = 0, fx = -1, fy = -1;
      for (int y = 0; y < SCR_H; y++)
        for (int x = 0; x < SCR_W; x++)
          if (bit_get(g_elems[i].bits, x, y) && bit_get(g_elems[j].bits, x, y)) {
            if (!count) { fx = x; fy = y; }
            count++;
          }
      if (count) {
        char d[256];
        snprintf(d, sizeof d,
                 "OVERLAP: '%s' and '%s' share %d px, first at (%d,%d)",
                 g_elems[i].name.c_str(), g_elems[j].name.c_str(), count, fx, fy);
        failures.push_back({kase, d});
      }
    }

  // 2. SAFE AREA — ink outside it is clipped once the burn-in shift wanders,
  //    which on a bench at shift (0,0) looks perfect and fails weeks later.
  int out = 0, ox = -1, oy = -1;
  for (int y = 0; y < SCR_H; y++)
    for (int x = 0; x < SCR_W; x++) {
      if (!bit_get(composite, x, y)) continue;
      if (x >= SAFE_X0 && x <= SAFE_X1 && y >= SAFE_Y0 && y <= SAFE_Y1) continue;
      if (!out) { ox = x; oy = y; }
      out++;
    }
  if (out) {
    char d[256];
    snprintf(d, sizeof d,
             "OUTSIDE SAFE AREA: %d px beyond x[%d..%d] y[%d..%d], first at (%d,%d)",
             out, (int)SAFE_X0, (int)SAFE_X1, (int)SAFE_Y0, (int)SAFE_Y1, ox, oy);
    failures.push_back({kase, d});
  }

  // 3. CROWDING — a warning, not a failure.
  for (size_t i = 0; i < g_elems.size(); i++) {
    Bits di = dilate(g_elems[i].bits);
    for (size_t j = i + 1; j < g_elems.size(); j++) {
      bool touch = false;
      for (size_t k = 0; k < di.size() && !touch; k++)
        if (di[k] & g_elems[j].bits[k]) touch = true;
      if (touch) {
        char d[256];
        snprintf(d, sizeof d, "CROWDED: '%s' and '%s' have no clear pixel between them",
                 g_elems[i].name.c_str(), g_elems[j].name.c_str());
        warnings.push_back({kase, d});
      }
    }
  }

  // 4. FILL FACTOR — burn-in policy, and now a HARD GATE at the only published
  //    number anybody actually enforces. Wear OS quality requirement WO-P7
  //    caps an always-on watch face at 15% lit pixels, sampled across a whole
  //    day, EVERY sample passing; Google's always-on docs put the same rule as
  //    "keep at least 85% of the screen black". A Play Store submission over
  //    that is rejected. Our own frames measure ~9%, so the budget is real but
  //    not tight, and the failure this catches is a FUTURE page that blows it.
  //    The old 25% here was a house number that nothing stood behind.
  //      salvage/10-oled-inversion-abl-aod.md section 3
  //
  //    TWO TIERS, DELIBERATELY. Blowing the budget is a FAILURE on the faces
  //    the device ships on, and a WARNING on FILLED — because FILLED is a
  //    style the USER chose, and a prover does not get to overrule that. What
  //    it does get to do is refuse to let it happen by accident somewhere else.
  //    FILLED measures up to 23.3% against a 15% budget, which is a real
  //    finding and is reported as one; capping or retiring it is a decision
  //    for the user, not for this file.
  const bool selftest_case = kase.rfind("SELFTEST", 0) == 0;
  // The shipping default is S_OUTLINE on all four slots (defaults.cpp), which
  // is also what Apple's always-on guidance recommends: stroked outlines and
  // dimmed interiors rather than large blocks of bright content. So OUTLINE is
  // what the device actually ages on, and OUTLINE is what gets held to the
  // budget. FILLED / SHADOW / SEGMENT / WORDS / STENCIL are opt-in.
  //
  // "INV" catches the inverted half of the MODE cycle. An inverted look is a
  // lit chip with the glyph knocked out of it, which is ~16-23% by
  // construction; it is reached only by pressing MODE past the six originals,
  // so it is opt-in in exactly the sense FILLED is, and it is measured and
  // reported rather than gated. The default at power-on is still stop 0,
  // OUTLINE, not inverted.
  //
  // THE FIVE PORTED THEMES ARE OPT-IN IN EXACTLY THE SAME SENSE. DIAL,
  // DATASHEET, QUIET, ROUNDEL and CAPSULE are stops 3 to 7 of the MODE cycle;
  // the device powers on at stop 1, OUTLINE, and nothing reaches a theme
  // except a button press. So they are measured and reported and not gated,
  // which is the policy already stated above rather than a new one — the
  // classification is by HOW THE SCREEN IS REACHED, and adding a look to the
  // cycle does not change how the DEFAULT is reached. What must not happen is
  // this list quietly growing to cover the default itself; OUTLINE is absent
  // from it and stays absent.
  const bool user_chose_it =
      kase.find("FILLED")    != std::string::npos ||
      kase.find("SHADOW")    != std::string::npos ||
      kase.find("SEGMENT")   != std::string::npos ||
      kase.find("WORDS")     != std::string::npos ||
      kase.find("STENCIL")   != std::string::npos ||
      kase.find("DIAL")      != std::string::npos ||
      kase.find("DATASHEET") != std::string::npos ||
      kase.find("QUIET")     != std::string::npos ||
      kase.find("ROUNDEL")   != std::string::npos ||
      kase.find("CAPSULE")   != std::string::npos ||
      kase.find("INV")       != std::string::npos;
  // Animation frames are shown for seconds during a transition, not held for
  // months. A duty metric that assumes the screen is up forever says nothing
  // about them, so they are measured and reported but never gated.
  const bool transient = kase.find("ANIM") != std::string::npos;

  int lit = 0;
  for (int y = 0; y < SCR_H; y++)
    for (int x = 0; x < SCR_W; x++) if (bit_get(composite, x, y)) lit++;
  double ff = 100.0 * lit / (SCR_W * SCR_H);
  if (!selftest_case) {
    look_note(kase, ff, FILL_BUDGET_PCT);
    fill_sum += ff; n_budget_cases++;
    if (ff > fill_max) { fill_max = ff; fill_worst = kase; }
    if (ff > FILL_BUDGET_PCT) {
      char d[160];
      snprintf(d, sizeof d, "FILL %.1f%% (%d px) — over the %.0f%% always-on budget (Wear OS WO-P7)",
               ff, lit, FILL_BUDGET_PCT);
      // A RATCHET, NOT A CLIFF. This budget arrived after the layouts did, and
      // 109 default-style screens are already over it — mostly the CLOCK v5
      // page. Failing all of them would just mean the gate gets switched off,
      // which is how a real gate becomes a comment. So the existing overage is
      // recorded as DEBT and the gate fails only if the debt GROWS: more
      // screens over budget, or a worse worst. Paying it down is a layout
      // decision (see the note in docs/), and the numbers below only ever move
      // in one direction.
      warnings.push_back({kase, d});
      if (user_chose_it || transient) fill_over_optin++;
      else { n_over_budget++; if (ff > over_max) over_max = ff; }
    }
  }

  // 5. SHIFT RELIEF — does the anti-burn-in wander actually buy anything on
  //    THIS screen? firmware B shipped shift_amp = 2, and +/-2 relieves
  //    precisely nothing: an envelope narrower than the glyph stroke cannot
  //    move a stroke off a pixel in its own interior. That was invisible for
  //    months because nothing measured it. Now something does.
  //
  //    Because the shift is a Hamiltonian tour (display.cpp), residency over
  //    the (2A+1)^2 offsets is EXACTLY uniform, so a pixel's duty is just the
  //    fraction of offsets that put ink on it — a uniform box sum of the
  //    composite, which an integral image gives us in O(W*H) per screen.
  //    d_max is the worst pixel's duty; 1/d_max is its life multiplier.
  //      salvage/09-oled-burnin-techniques.md section 3.1
  if (lit && !selftest_case) {
    static int integ[SCR_H + 1][SCR_W + 1];
    for (int x = 0; x <= SCR_W; x++) integ[0][x] = 0;
    for (int y = 0; y < SCR_H; y++) {
      integ[y + 1][0] = 0;
      for (int x = 0; x < SCR_W; x++)
        integ[y + 1][x + 1] = integ[y][x + 1] + integ[y + 1][x] - integ[y][x]
                            + (bit_get(composite, x, y) ? 1 : 0);
    }
    // The envelope is rectangular: the case hides four rows, so the vertical
    // wander is smaller than the horizontal one. Modelling it as a square box
    // would overstate the relief on every screen.
    const int A  = SHIFT_AMP_UNDER_TEST;
    const int AY = A < SHIFT_MAX_Y ? A : SHIFT_MAX_Y;
    const double M = (double)(2 * A + 1) * (2 * AY + 1);
    int peak = 0;
    for (int y = 0; y < SCR_H; y++)
      for (int x = 0; x < SCR_W; x++) {
        int x0 = x - A, y0 = y - AY, x1 = x + A + 1, y1 = y + AY + 1;
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
        if (x1 > SCR_W) x1 = SCR_W; if (y1 > SCR_H) y1 = SCR_H;
        int s = integ[y1][x1] - integ[y0][x1] - integ[y1][x0] + integ[y0][x0];
        if (s > peak) peak = s;
      }
    double dmax = peak / M;
    double mult = dmax > 0 ? 1.0 / dmax : 0.0;
    if (mult < relief_min && !transient && !user_chose_it) {
      relief_min = mult; relief_worst = kase;
    }
    if (mult < RELIEF_FLOOR && !transient && !user_chose_it) {
      char d[200];
      snprintf(d, sizeof d,
               "SHIFT RELIEF %.2fx at +/-%d (peak duty %.3f) — below the %.2fx floor; "
               "the envelope is narrower than this screen's stroke",
               mult, A, dmax, RELIEF_FLOOR);
      failures.push_back({kase, d});
    }
  }

  if (dump) dump_frame(kase, composite);
}

// Render one face case and judge it.
static GFXcanvas1 canvas(SCR_W, SCR_H);

static void run_face(uint8_t w, uint8_t s, uint8_t ov, const FaceData &d,
                     const std::string &label, bool dump) {
  g_elems.clear();
  g_cur.clear();
  g_target = &canvas;
  canvas.fillScreen(0);
  face_render(canvas, w, s, ov, d);
  flush_current();
  judge(label, dump);
}

static FaceData base_data() {
  FaceData d{};
  d.hour = 14; d.minute = 38; d.second = 47;
  d.day = 28; d.month = 9; d.year = 2026;
  d.weekday = fx_weekday(2026, 9, 28);
  d.temp_c10 = 231; d.humidity = 46;
  d.hour24 = false; d.temp_f = false; d.valid = true;
  return d;
}

// ===========================================================================
// THE FRAME-SEQUENCE LAWS — AN ANIMATION MAY NOT SKIP
// ===========================================================================
// WHY THIS EXISTS, IN ONE PARAGRAPH. heartFrames was declared [28][288] with 27
// initialisers. C++ zero-fills the rest, so heartFrames[27] was 288 bytes of
// nothing, anim_count(AN_HEART) went on saying 28, and anim_draw's `tick % n`
// reached it once every 28 frames — every 3.5 s at 8 fps — rendering the heart
// as an empty panel for one frame and bringing it back the next. The user
// reported it as "sometimes skips a frame".
//
// EVERY LAW ALREADY IN THIS FILE WAS GREEN THROUGH IT, and correctly so: a
// blank canvas has no overlap, nothing outside the safe area, no clipped
// glyphs and a fill factor of zero, which is the best score available. Judging
// frames ONE AT A TIME cannot see a defect that is only visible in the
// SEQUENCE. So these four laws judge the sequence:
//
//   BLANK      every frame of every cycle lights at least one pixel, unless
//              anim_blank_ok() declares that the animation wants a blank one.
//              Nothing declares it. This is the law that catches the heart.
//   COUNT      the frame count the firmware reports matches the count this
//              file expects — read from outside anim.cpp's translation unit,
//              so it is a second reading of the same fact rather than a
//              restatement of the static_assert next to the data.
//   PERIOD     anim_cycle_exact(id) is checked, not believed: frame t and
//              frame t + anim_cycle(id) are compared as bitmaps, for every t
//              in the cycle. A reel that trusts a wrong period cuts a beat.
//   CONTINUITY the change in lit pixels across the WRAP is no larger than the
//              largest change anywhere MID-cycle. A loop that jumps at the
//              seam is the same family of defect as one that goes blank —
//              a frame the eye reads as missing — and it is the one a blank
//              check alone would not see.
//
// And judge() runs on every frame too, over the WHOLE cycle rather than the
// first few hundred ticks the page sweep reaches, so the walker's 868-frame
// pace is now proven end to end. The case labels start with "ANIM" because
// that is how judge() recognises a transient frame and holds it to the
// reported-not-gated fill budget; the plants below start with "SELFTEST" for
// the same reason.

// What the four bitmap animations are, stated HERE. If anim_count() and this
// table disagree, one of them is wrong and the build says which.
struct AnimCountExp { uint8_t id; uint8_t frames; };
static const AnimCountExp ANIM_COUNT_EXPECT[] = {
  { AN_WALK,   28 },
  { AN_EYE,    28 },
  { AN_FIDGET, 28 },
  // 27, NOT 28. This is the number the bug was about. It is written out with
  // its reason so that changing it is a deliberate act.
  { AN_HEART,  27 },
};

// The renderer under test. The real run passes anim_draw; the selftest passes
// wrappers that plant the two defects, so the plants go through THIS law and
// not through a copy of it.
typedef void (*AnimRender)(GFXcanvas1 &c, uint8_t id, uint16_t tick);
static void anim_draw_real(GFXcanvas1 &c, uint8_t id, uint16_t tick) {
  anim_draw(c, id, tick);
}

static long n_anim_frames = 0;

static int anim_lit(GFXcanvas1 &c) {
  int lit = 0;
  for (int y = 0; y < SCR_H; y++)
    for (int x = 0; x < SCR_W; x++) if (c.getPixel(x, y)) lit++;
  return lit;
}

// Returns the number of findings it added.
static size_t anim_sequence_law(uint8_t id, AnimRender ren, const char *tag,
                                bool with_judge) {
  const size_t f0 = failures.size();
  const uint16_t cyc = anim_cycle(id);
  if (cyc == 0) {
    failures.push_back({std::string(tag) + " " + anim_name(id),
                        "CYCLE 0 — the reel would divide by it"});
    return failures.size() - f0;
  }

  std::vector<int> lit((size_t)cyc, 0);

  for (uint16_t t = 0; t < cyc; t++) {
    char kase[192];
    snprintf(kase, sizeof kase, "%s %s tick=%u/%u",
             tag, anim_name(id), (unsigned)t, (unsigned)cyc);
    g_elems.clear(); g_cur.clear();
    g_target = &canvas;
    canvas.fillScreen(0);
    ren(canvas, id, t);
    // Read the pixel count BEFORE flush_current() lifts the elements out and
    // wipes the canvas — after that the evidence is in g_elems, not here.
    lit[t] = anim_lit(canvas);
    flush_current();
    if (with_judge) { judge(kase, false); n_anim_frames++; }

    // ---- BLANK -----------------------------------------------------------
    if (lit[t] == 0 && !anim_blank_ok(id)) {
      char det[256];
      snprintf(det, sizeof det,
               "BLANK FRAME — %s frame %u of %u lights zero pixels. On the "
               "glass that is the animation vanishing for %u ms and coming "
               "back. If it is deliberate, say so in anim_blank_ok().",
               anim_name(id), (unsigned)t, (unsigned)cyc,
               (unsigned)ANIM_FRAME_MS);
      failures.push_back({kase, det});
    }
  }

  // ---- PERIOD ------------------------------------------------------------
  // The claim is that t and t + cyc are the same bitmap. Checked on the whole
  // cycle, not sampled: a bitmap animation whose artwork period divides the
  // cycle but whose POSITION does not (the walker) agrees at t=0 and disagrees
  // everywhere else.
  if (anim_cycle_exact(id)) {
    for (uint16_t t = 0; t < cyc; t++) {
      canvas.fillScreen(0);
      ren(canvas, id, t);
      std::vector<uint8_t> a(canvas.getBuffer(),
                             canvas.getBuffer() + (size_t)WBYTES * SCR_H);
      canvas.fillScreen(0);
      ren(canvas, id, (uint16_t)(t + cyc));
      if (memcmp(a.data(), canvas.getBuffer(), a.size()) != 0) {
        char kase[192], det[256];
        snprintf(kase, sizeof kase, "%s %s period", tag, anim_name(id));
        snprintf(det, sizeof det,
                 "PERIOD WRONG — anim_cycle_exact() claims %s repeats every %u "
                 "frames, but frame %u and frame %u differ. A reel that trusts "
                 "it cuts the animation off mid-beat.",
                 anim_name(id), (unsigned)cyc, (unsigned)t,
                 (unsigned)(t + cyc));
        failures.push_back({kase, det});
        break;
      }
    }
    canvas.fillScreen(0);
  }

  // ---- CONTINUITY --------------------------------------------------------
  // Only meaningful where the cycle IS the period. AN_EYES declares that its
  // returned length is a viewing length rather than a period (four blink
  // periods of 17/23/29/37 have no small common multiple), so it is exempt
  // from this one and from PERIOD above — and that exemption is not free: it
  // is the reason anim_cycle_exact() exists as a separate, checked claim
  // rather than as a comment.
  if (cyc >= 3 && anim_cycle_exact(id)) {
    int mid_max = 0;
    for (uint16_t t = 1; t < cyc; t++) {
      int d = lit[t] - lit[t - 1];
      if (d < 0) d = -d;
      if (d > mid_max) mid_max = d;
    }
    int wrap = lit[0] - lit[cyc - 1];
    if (wrap < 0) wrap = -wrap;
    if (wrap > mid_max) {
      char kase[192], det[320];
      snprintf(kase, sizeof kase, "%s %s wrap", tag, anim_name(id));
      snprintf(det, sizeof det,
               "DISCONTINUOUS LOOP — %s changes by %d lit pixels across the "
               "wrap (frame %u -> frame 0) against a mid-cycle maximum of %d. "
               "The loop visibly jumps every %u frames.",
               anim_name(id), wrap, (unsigned)(cyc - 1), mid_max,
               (unsigned)cyc);
      failures.push_back({kase, det});
    }
  }

  return failures.size() - f0;
}

// ---- the two plants, used ONLY by selftest() -------------------------------
// A ZERO-FILLED LAST FRAME. This is the heart bug, reproduced exactly: the
// animation renders normally for every frame but one, and that one is empty.
static void anim_draw_blank_plant(GFXcanvas1 &c, uint8_t id, uint16_t tick) {
  // THE LAST FRAME, because that is where a short initialiser list puts the
  // zero-fill. Reproducing the bug's position as well as its shape.
  if ((tick % anim_cycle(id)) == (uint16_t)(anim_cycle(id) - 1)) {
    ELEM("planted-blank");
    return;
  }
  anim_draw(c, id, tick);
}
// A LOOP THAT JUMPS AT THE SEAM. Same family, different symptom: nothing is
// blank, but the last frame does not flow into the first.
//
// IT HAS TO RAMP, NOT SPIKE, and getting that wrong the first time is worth
// recording. A single bad frame dropped into an otherwise steady animation
// produces TWO equal changes — one going in, one coming out — so the wrap is
// not an outlier against the mid-cycle maximum and the continuity law is
// right not to fire on it. (The blank law catches that shape; they are
// different defects and they have different detectors, which is the point of
// having both.) A DISCONTINUOUS LOOP is a drawing that grows steadily all
// cycle and then snaps back, so every mid-cycle step is small and only the
// seam is large. That is what this draws.
static void anim_draw_jump_plant(GFXcanvas1 &c, uint8_t id, uint16_t tick) {
  const uint16_t f = (uint16_t)(tick % anim_cycle(id));
  ELEM("planted-jump");
  c.fillRect(40, 16, 20, (int16_t)(1 + f), 1);
}

static void check_anims() {
  // ---- COUNT: the declared frame counts, read from outside anim.cpp -------
  for (const AnimCountExp &e : ANIM_COUNT_EXPECT) {
    if (anim_count(e.id) != e.frames) {
      char det[256];
      snprintf(det, sizeof det,
               "FRAME COUNT — anim_count(%s) is %u, this file expects %u. The "
               "count and the initialiser list disagreed once before and the "
               "difference rendered as a blank frame every 3.5 s.",
               anim_name(e.id), (unsigned)anim_count(e.id),
               (unsigned)e.frames);
      failures.push_back({std::string("anim count ") + anim_name(e.id), det});
    }
  }

  // ---- the reel's ordering is a PERMUTATION ------------------------------
  // ui.cpp's showcase plays anim_reel(0..anim_total()-1). If that table ever
  // repeats an entry it silently drops another animation from the reel, which
  // is invisible unless somebody counts.
  if (anim_total() != (uint8_t)AN_COUNT)
    failures.push_back({"anim reel",
                        "anim_total() disagrees with AN_COUNT"});
  {
    std::vector<int> seen((size_t)AN_COUNT, 0);
    for (uint8_t i = 0; i < anim_total(); i++) {
      const uint8_t id = anim_reel(i);
      if (id >= AN_COUNT) {
        failures.push_back({"anim reel", "anim_reel() returned an id past AN_COUNT"});
        break;
      }
      seen[id]++;
    }
    for (uint8_t id = 0; id < AN_COUNT; id++)
      if (seen[id] != 1) {
        char det[200];
        snprintf(det, sizeof det,
                 "REEL NOT A PERMUTATION — %s appears %d times in anim_reel(); "
                 "the showcase would play it twice and skip another.",
                 anim_name(id), seen[id]);
        failures.push_back({"anim reel", det});
      }
  }

  // ---- the names, which land on the glass in the reveal row --------------
  for (uint8_t id = 0; id < AN_COUNT; id++) {
    const char *n = anim_name(id);
    const size_t len = strlen(n);
    if (len == 0 || len > 6) {
      char det[200];
      snprintf(det, sizeof det,
               "NAME LENGTH — \"%s\" is %zu characters; the reveal row's value "
               "column carries six before the fit starts cutting.", n, len);
      failures.push_back({std::string("anim name ") + n, det});
    }
    for (const char *p = n; *p; p++)
      if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E) {
        char det[200];
        snprintf(det, sizeof det,
                 "NAME GLYPH — \"%s\" contains 0x%02X, which the fonts do not "
                 "have; it would measure as nothing and draw as nothing.",
                 n, (unsigned)(unsigned char)*p);
        failures.push_back({std::string("anim name ") + n, det});
        break;
      }
  }

  // ---- and every frame of every animation, over its WHOLE cycle ----------
  for (uint8_t id = 0; id < AN_COUNT; id++)
    anim_sequence_law(id, anim_draw_real, "ANIM", true);
}

// =========================================================== THE SHOWREEL ==
// Covered exactly the way the roster animations are covered -- every tick of
// every slot through judge(), plus the sequence laws -- with one difference
// that is the reel's own: blankness is CHOREOGRAPHY. The walker is only ever
// on one panel, every segment arrives staggered, and the final beat is dark
// everywhere, so the blank law consults showreel_blank_ok(slot, tick), the
// per-(slot,tick) map exported next to the renderer, instead of the
// per-animation anim_blank_ok(). Every undeclared frame must light, exactly
// as before; nothing got weaker, the declaration just gained an axis.
//
// The clock interlude gets its own law (blank map + exact period) but NOT the
// wrap-continuity law: it is played once per appearance and never wraps on
// the glass -- the roster resumes after it, and inside the reel the wrap
// belongs to the reel, whose own continuity law covers it.
static void run_page(uint8_t pg, uint8_t slot, uint8_t var, const PageData &d,
                     const std::string &label, bool dump);
static PageData base_page();

typedef void (*SrRender)(GFXcanvas1 &c, uint8_t slot, uint16_t tick,
                         const PageData &d);
static void sr_draw_real(GFXcanvas1 &c, uint8_t slot, uint16_t tick,
                         const PageData &d) {
  showreel_draw(c, slot, tick, d);
}
static long n_sr_frames = 0;

// The sequence laws -- BLANK (against the map), PERIOD, CONTINUITY -- for one
// slot of the reel, under one walker VARIANT. The variant is the one runtime
// input the scheduler randomizes, so the law runs once per (slot, variant):
// proving all four proves every schedule the scheduler can ever produce.
// Returns the number of findings it added.
static size_t showreel_sequence_law(uint8_t slot, uint8_t variant,
                                    SrRender ren, const char *tag) {
  const size_t f0 = failures.size();
  PageData d = base_page();              // representative data; the data
                                         // domains are swept in check_showreel
  d.sr_walk = variant;
  std::vector<int> lit((size_t)SHOWREEL_TICKS, 0);

  for (uint16_t t = 0; t < SHOWREEL_TICKS; t++) {
    g_elems.clear(); g_cur.clear();
    g_target = &canvas;
    canvas.fillScreen(0);
    ren(canvas, slot, t, d);
    lit[t] = anim_lit(canvas);

    // ---- BLANK, against the declared map ---------------------------------
    if (lit[t] == 0 && !showreel_blank_ok(slot, t, d)) {
      char kase[192], det[256];
      snprintf(kase, sizeof kase, "%s v%u slot%u tick=%u", tag, variant, slot,
               (unsigned)t);
      snprintf(det, sizeof det,
               "BLANK FRAME -- showreel slot %u frame %u of %u lights zero "
               "pixels and showreel_blank_ok() does not declare it. On the "
               "glass that panel dies for %u ms mid-reel.",
               slot, (unsigned)t, (unsigned)SHOWREEL_TICKS,
               (unsigned)ANIM_FRAME_MS);
      failures.push_back({kase, det});
    }

    // ---- PERIOD: tick and tick + SHOWREEL_TICKS are the same bitmap ------
    std::vector<uint8_t> a(canvas.getBuffer(),
                           canvas.getBuffer() + (size_t)WBYTES * SCR_H);
    canvas.fillScreen(0);
    ren(canvas, slot, (uint16_t)(t + SHOWREEL_TICKS), d);
    if (memcmp(a.data(), canvas.getBuffer(), a.size()) != 0) {
      char kase[192], det[256];
      snprintf(kase, sizeof kase, "%s slot%u period", tag, slot);
      snprintf(det, sizeof det,
               "PERIOD WRONG -- the showreel claims to repeat every %u frames "
               "but slot %u frame %u and frame %u differ. The loop the user "
               "films would drift take to take.",
               (unsigned)SHOWREEL_TICKS, slot, (unsigned)t,
               (unsigned)(t + SHOWREEL_TICKS));
      failures.push_back({kase, det});
      break;
    }
  }
  canvas.fillScreen(0);
  g_elems.clear(); g_cur.clear();

  // ---- CONTINUITY across the wrap ----------------------------------------
  // The reel ends on a deliberate blank beat and reopens on the first clock
  // slam; that change must stay inside what the reel already does mid-cycle
  // (the heart's swell and the segment arrivals are far larger).
  {
    int mid_max = 0;
    for (uint16_t t = 1; t < SHOWREEL_TICKS; t++) {
      int dd = lit[t] - lit[t - 1];
      if (dd < 0) dd = -dd;
      if (dd > mid_max) mid_max = dd;
    }
    int wrap = lit[0] - lit[SHOWREEL_TICKS - 1];
    if (wrap < 0) wrap = -wrap;
    if (wrap > mid_max) {
      char kase[192], det[320];
      snprintf(kase, sizeof kase, "%s slot%u wrap", tag, slot);
      snprintf(det, sizeof det,
               "DISCONTINUOUS LOOP -- showreel slot %u changes by %d lit "
               "pixels across the wrap against a mid-cycle maximum of %d. "
               "The loop visibly jumps every %u frames.",
               slot, wrap, mid_max, (unsigned)SHOWREEL_TICKS);
      failures.push_back({kase, det});
    }
  }
  return failures.size() - f0;
}

// The clock interlude's law: blank map and exact period. No wrap law -- see
// the header note.
static size_t showclock_sequence_law(uint8_t slot) {
  const size_t f0 = failures.size();
  for (uint16_t t = 0; t < SHOWCLOCK_TICKS; t++) {
    g_elems.clear(); g_cur.clear();
    g_target = &canvas;
    canvas.fillScreen(0);
    showclock_draw(canvas, slot, t, 12, 38);
    const int lit = anim_lit(canvas);
    if (lit == 0 && !showclock_blank_ok(slot, t)) {
      char kase[192], det[256];
      snprintf(kase, sizeof kase, "ANIM SHOWCLOCK law slot%u tick=%u",
               slot, (unsigned)t);
      snprintf(det, sizeof det,
               "BLANK FRAME -- clock interlude slot %u frame %u lights zero "
               "pixels and showclock_blank_ok() does not declare it.",
               slot, (unsigned)t);
      failures.push_back({kase, det});
    }
    std::vector<uint8_t> a(canvas.getBuffer(),
                           canvas.getBuffer() + (size_t)WBYTES * SCR_H);
    canvas.fillScreen(0);
    showclock_draw(canvas, slot, (uint16_t)(t + SHOWCLOCK_TICKS), 12, 38);
    if (memcmp(a.data(), canvas.getBuffer(), a.size()) != 0) {
      failures.push_back({"ANIM SHOWCLOCK law period",
                          "PERIOD WRONG -- the clock interlude does not "
                          "repeat at SHOWCLOCK_TICKS."});
      break;
    }
  }
  canvas.fillScreen(0);
  g_elems.clear(); g_cur.clear();
  return failures.size() - f0;
}

// The selftest plant: a frame that must light, rendered as nothing. Tick 720
// is mid-heartbeat, where the map promises ink on every slot.
static void sr_draw_blank_plant(GFXcanvas1 &c, uint8_t slot, uint16_t tick,
                                const PageData &d) {
  if ((uint16_t)(tick % SHOWREEL_TICKS) == 720) return;
  showreel_draw(c, slot, tick, d);
}
static_assert(720 >= SR_AT_HEART && 720 < SR_AT_BLANK,
              "the blank plant must sit in the heart segment, where ink is "
              "promised on every slot; the map moved out from under it.");

// ===================================================== THE FOOT-PLANT LAW ==
// THE WALKER MAY NOT SKATE. The physics of a non-sliding walk: while a foot
// is planted it stays at a FIXED x on the glass — the body translates over
// it. The stride constant SR_STRIDE_PX in anim.h is a MEASUREMENT of this
// artwork (a hybrid in-place cycle: the planted foot holds ~5 frames, then
// sweeps back ~10 px over ~9), and this law is what keeps that measurement
// from rotting: it renders consecutive ticks of the shipping pass, finds the
// ground-contact foot in each, and fails if the planted foot moves more than
// the artwork's own measured residual.
//
// The bound is 2 px — the edge-matched worst transition of this artwork at
// stride 1 — a ratchet, not a taste number. It failed twice by eye before it
// was measured (25 px/tick, then 8), which is why it is a law now.
static const int FOOT_SLIDE_MAX_PX = 2;
static long   n_footplant_pairs = 0;
static int    footplant_worst   = 0;

typedef void (*PassRender)(GFXcanvas1 &c, uint8_t slot, uint16_t t, uint8_t v);
static void pass_draw_real(GFXcanvas1 &c, uint8_t slot, uint16_t t, uint8_t v) {
  sr_pass_draw(c, slot, t, v);
}
// The plant: a renderer that skates — the whole sprite jumps +4 px on odd
// ticks. If the law cannot catch a 4 px slide it proves nothing.
static GFXcanvas1 fp_scratch(SCR_W, SCR_H);
static void pass_skate_plant(GFXcanvas1 &c, uint8_t slot, uint16_t t,
                             uint8_t v) {
  if (!(t & 1)) { sr_pass_draw(c, slot, t, v); return; }
  GFXcanvas1 *save = g_target;
  g_target = &fp_scratch;
  fp_scratch.fillScreen(0);
  sr_pass_draw(fp_scratch, slot, t, v);
  g_target = save;
  for (int y = 0; y < SCR_H; y++)
    for (int x = 0; x < SCR_W; x++)
      if (fp_scratch.getPixel(x, y) && x + 4 < SCR_W) c.drawPixel(x + 4, y, 1);
}

// Ground-contact foot edges: ink within 3 rows of the lowest lit row,
// clustered into blobs (a gap of 3+ clear columns separates feet), reduced
// to the blobs' left/right edges — the toe and heel, the stable landmarks.
static void fp_foot_edges(GFXcanvas1 &c, std::vector<int> &edges) {
  edges.clear();
  int gy = -1;
  for (int y = SCR_H - 1; y >= 0 && gy < 0; y--)
    for (int x = 0; x < SCR_W; x++)
      if (c.getPixel(x, y)) { gy = y; break; }
  if (gy < 0) return;
  bool col[SCR_W] = {false};
  for (int y = gy - 2; y <= gy; y++) {
    if (y < 0) continue;
    for (int x = 0; x < SCR_W; x++) if (c.getPixel(x, y)) col[x] = true;
  }
  int s = -1, p = -8;
  for (int x = 0; x < SCR_W; x++) {
    if (!col[x]) continue;
    if (x - p >= 3) {
      if (s >= 0) { edges.push_back(s); edges.push_back(p); }
      s = x;
    }
    p = x;
  }
  if (s >= 0) { edges.push_back(s); edges.push_back(p); }
}

static size_t footplant_law(uint8_t variant, PassRender ren, const char *tag,
                            bool record) {
  const size_t f0 = failures.size();
  std::vector<int> prev;
  int prev_t = -2;
  for (uint16_t t = 0; t < SR_PASS_TICKS; t++) {
    // Full visibility is the blank map's own signal: the one (slot, tick)
    // pair that PROMISES ink is the one where the whole sprite cell is on
    // that panel. Clipped entry/exit strips and the bezel beat are skipped —
    // partial artwork has no reliable feet to measure.
    int slot = -1;
    for (uint8_t s = 0; s < 4; s++)
      if (!sr_pass_blank_ok(s, t, variant)) slot = s;
    if (slot < 0) { prev_t = -2; continue; }
    g_elems.clear(); g_cur.clear();
    g_target = &canvas;
    canvas.fillScreen(0);
    ren(canvas, (uint8_t)slot, t, variant);
    std::vector<int> cur;
    fp_foot_edges(canvas, cur);
    if (cur.empty()) { prev_t = -2; continue; }
    if (prev_t == (int)t - 1) {
      int best = 1 << 20;
      for (int a : prev)
        for (int b : cur) { int dd = a - b; if (dd < 0) dd = -dd;
                            if (dd < best) best = dd; }
      if (record) {
        n_footplant_pairs++;
        if (best > footplant_worst) footplant_worst = best;
      }
      if (best > FOOT_SLIDE_MAX_PX) {
        char kase[192], det[320];
        snprintf(kase, sizeof kase, "%s v%u tick=%u", tag, variant,
                 (unsigned)t);
        snprintf(det, sizeof det,
                 "FOOT SLIDE — the planted foot moved %d px between ticks %u "
                 "and %u of pass variant %u, over the %d px the artwork's own "
                 "stance allows. Ground speed and gait have come unglued; see "
                 "WALKER_TEMPO / SR_STRIDE_PX in anim.h.",
                 best, (unsigned)(t - 1), (unsigned)t, variant,
                 FOOT_SLIDE_MAX_PX);
        failures.push_back({kase, det});
      }
    }
    prev = cur;
    prev_t = (int)t;
  }
  canvas.fillScreen(0);
  g_elems.clear(); g_cur.clear();
  return failures.size() - f0;
}

static void check_showreel() {
  // ---- every slot's sequence laws, reel and interlude, every variant -----
  for (uint8_t slot = 0; slot < 4; slot++) {
    for (uint8_t v = 0; v < 4; v++)
      showreel_sequence_law(slot, v, sr_draw_real, "ANIM SHOWREEL law");
    showclock_sequence_law(slot);
  }

  // ---- and the walker never skates, in any variant -----------------------
  for (uint8_t v = 0; v < 4; v++)
    footplant_law(v, pass_draw_real, "ANIM FOOTPLANT law", true);

  // ---- every reel tick through judge(), on representative data -----------
  // This is the sweep that holds the lap, the weather and the heart (whose
  // pixels do not depend on runtime data) to overlap/safe-area on every
  // frame; the data-bearing segments get their domains below. The label
  // starts with "page ANIM" so judge() books these as transient frames, the
  // same policy the roster animations get.
  for (uint8_t slot = 0; slot < 4; slot++) {
    bool dumped = false;
    for (uint16_t t = 0; t < SHOWREEL_TICKS; t++) {
      PageData d = base_page();
      d.anim_scene = SCENE_SHOWREEL;
      d.anim_frame = t;
      char lab[192];
      snprintf(lab, sizeof lab, "page ANIM SHOWREEL slot%u tick=%u",
               slot, (unsigned)t);
      // One frame of each flavour on the contact sheet: a weather tick.
      bool dump = !dumped && t == (uint16_t)(SR_AT_WX + 20);
      if (dump) dumped = true;
      run_page(PG_ANIM, slot, 0, d, lab, dump);
      n_sr_frames++;
    }
  }

  // ---- the walker segment, over ALL FOUR pass variants -------------------
  // The scheduler rolls (row, direction) at runtime, so no single schedule
  // can be proven — the whole space it draws from is swept instead, exactly
  // the roster's arrangement: every variant, every slot, every tick of the
  // segment through judge().
  for (uint8_t v = 0; v < 4; v++)
    for (uint8_t slot = 0; slot < 4; slot++)
      for (uint16_t t = SR_AT_WALK; t < SR_AT_SENS; t++) {
        PageData d = base_page();
        d.anim_scene = SCENE_SHOWREEL;
        d.anim_frame = t;
        d.sr_walk = v;
        char lab[192];
        snprintf(lab, sizeof lab,
                 "page ANIM SHOWREEL-WALK v%u slot%u tick=%u",
                 v, slot, (unsigned)t);
        run_page(PG_ANIM, slot, 0, d, lab, false);
        n_sr_frames++;
      }

  // ---- the clock interlude, over the whole digit domain ------------------
  // Through the SCENE_CLOCK page path, which is also the path the shuffled
  // roster's interlude takes; the reel's two clock segments delegate to the
  // same renderer with the same local ticks, so this domain covers those
  // too. Each slot renders ONE digit of the pair, so hours x fixed minute
  // plus minutes x fixed hour covers every digit either can carry.
  for (uint8_t slot = 0; slot < 4; slot++) {
    for (int hour = 0; hour < 24; hour++)
      for (uint16_t t = 0; t < SHOWCLOCK_TICKS; t++) {
        PageData d = base_page();
        d.anim_scene = SCENE_CLOCK;
        d.anim_frame = t;
        d.clock.hour = (uint8_t)hour;
        d.clock.minute = 38;
        char lab[192];
        snprintf(lab, sizeof lab,
                 "page ANIM SHOWCLOCK slot%u tick=%u h=%d m=38",
                 slot, (unsigned)t, hour);
        run_page(PG_ANIM, slot, 0, d, lab, false);
        n_sr_frames++;
      }
    for (int min = 0; min < 60; min++)
      for (uint16_t t = 0; t < SHOWCLOCK_TICKS; t++) {
        PageData d = base_page();
        d.anim_scene = SCENE_CLOCK;
        d.anim_frame = t;
        d.clock.hour = 12;
        d.clock.minute = (uint8_t)min;
        char lab[192];
        snprintf(lab, sizeof lab,
                 "page ANIM SHOWCLOCK slot%u tick=%u h=12 m=%d",
                 slot, (unsigned)t, min);
        run_page(PG_ANIM, slot, 0, d, lab, false);
        n_sr_frames++;
      }
  }

  // ---- the sensors segment, over the sensor domain -----------------------
  // The same axes the sensor page sweep drives, aimed at the reel ticks that
  // read them. Length changes are what break layouts: the -40.0 temperature,
  // 100%, the uptime gaining a digit.
  for (uint8_t slot = 0; slot < 4; slot++)
    for (uint16_t t = SR_AT_SENS; t < SR_AT_CLK2; t++)
      for (int i = 0; i < 48; i++) {
        PageData d = base_page();
        d.anim_scene = SCENE_SHOWREEL;
        d.anim_frame = t;
        d.sens.temp_f   = (i & 1) != 0;
        d.sens.sht_ok   = (i % 7) != 0;
        d.sens.humidity = (uint8_t)((i * 100) / 47);
        d.sens.light_pct= (uint8_t)((i * 100) / 47);
        d.sens.temp_c10 = (int16_t)(-400 + i * 30);
        static const uint32_t UPS[] = {0, 59, 60, 3599, 3600, 86399, 86400,
                                       9 * 86400, 99 * 86400, 400 * 86400};
        d.sens.uptime_s = UPS[i % 10];
        char lab[192];
        snprintf(lab, sizeof lab, "page ANIM SHOWREEL-SENS slot%u tick=%u i=%d",
                 slot, (unsigned)t, i);
        run_page(PG_ANIM, slot, 0, d, lab, false);
        n_sr_frames++;
      }

  // ---- the stocks segment, over the quote domain -------------------------
  // The same extremes the markets page sweep drives -- longest price, widest
  // change, invalid quotes -- aimed at the reel ticks that render them.
  static const char *SR_PRICES[] = {"0.01", "9.99", "313.33", "7757.6",
                                    "65043", "123456", "1234567"};
  static const char *SR_SYMS[]   = {"SPX", "NVDA", "AAPL", "BTC", "TSLA",
                                    "MSFT", "AMZN", "ABCDEFG"};
  for (uint8_t slot = 0; slot < 4; slot++)
    for (uint16_t t = SR_AT_STK; t < SR_AT_HEART; t++)
      for (int i = 0; i < 14; i++) {
        PageData d = base_page();
        d.anim_scene = SCENE_SHOWREEL;
        d.anim_frame = t;
        for (int k = 0; k < 4; k++) {
          d.q[k].valid = (i % 9) != 0;
          snprintf(d.q[k].sym, sizeof d.q[k].sym, "%s", SR_SYMS[(i + k) % 8]);
          snprintf(d.q[k].price, sizeof d.q[k].price, "%s",
                   SR_PRICES[(i + k) % 7]);
          static const int16_t BP[] = {0, 5, -5, 999, -999, 32000, -32000, 1234};
          d.q[k].chg_bp = BP[(i + k) % 8];
        }
        char lab[192];
        snprintf(lab, sizeof lab, "page ANIM SHOWREEL-STK slot%u tick=%u i=%d",
                 slot, (unsigned)t, i);
        run_page(PG_ANIM, slot, 0, d, lab, false);
        n_sr_frames++;
      }
}

// ---------------------------------------------------------------- selftest
// A PROVER THAT CANNOT FAIL PROVES NOTHING. Before checking any real screen,
// draw two deliberately overlapping elements and one deliberately outside the
// safe area, and confirm both are reported. If this does not fire, the element
// hook is not wired up and every "PASS" below would be vacuous.
static GFXcanvas1 st_canvas(SCR_W, SCR_H);

static bool selftest() {
  size_t f0 = failures.size(), w0 = warnings.size();
  g_elems.clear(); g_cur.clear();
  g_target = &st_canvas;
  st_canvas.fillScreen(0);

  face_elem_hook("selftest-a");
  st_canvas.fillRect(20, 20, 30, 20, 1);
  face_elem_hook("selftest-b");
  st_canvas.fillRect(35, 25, 30, 20, 1);      // overlaps a
  face_elem_hook("selftest-edge");
  st_canvas.drawPixel(0, 0, 1);               // outside the safe area
  flush_current();
  judge("SELFTEST (this one is supposed to fail)", false);

  size_t found = failures.size() - f0;
  bool saw_overlap = false, saw_edge = false;
  for (size_t i = f0; i < failures.size(); i++) {
    if (failures[i].detail.find("OVERLAP") == 0) saw_overlap = true;
    if (failures[i].detail.find("OUTSIDE") == 0) saw_edge = true;
  }
  // Remove the deliberate findings so they do not pollute the real report.
  failures.resize(f0);
  warnings.resize(w0);
  n_cases--;

  if (!saw_overlap || !saw_edge) {
    printf("\n  SELFTEST FAILED: the prover did not detect its own planted "
           "bugs (overlap=%d edge=%d, %zu findings).\n"
           "  Every result below would be meaningless. Fix the element hook.\n",
           (int)saw_overlap, (int)saw_edge, found);
    return false;
  }

  // ---- and prove the shift path itself --------------------------------
  // The relief metric above models residency as a UNIFORM box. That model is
  // only honest if the shift really does visit every offset equally, so the
  // shipping tour (shift_tour.h, the same header display.cpp calls) is checked
  // here for exactly that, at every amplitude the settings allow.
  //
  // This is the check firmware B never had. Its shift was a biased random walk
  // -- P(-1)=1/2 against P(+1)=1/4 -- which parked the image in the -6 corner
  // 25% of the time while the comment above it claimed even spreading. Nothing
  // in the build could tell the difference between that and working code.
  for (int16_t amp = SHIFT_MIN; amp <= SHIFT_MAX; amp++) {
    // Same rectangular envelope display.cpp uses, derived the same way, so a
    // future change to the vertical cap cannot leave the prover checking a
    // shape the firmware no longer walks.
    const int16_t ampy = amp < SHIFT_MAX_Y ? amp : SHIFT_MAX_Y;
    const int sw = 2 * amp + 1, sh = 2 * ampy + 1;
    const uint16_t len = shift_tour_len(amp, ampy);
    if (len != (uint16_t)(2 * sw * sh)) {
      printf("\n  SELFTEST FAILED: tour length %u at amp %dx%d, expected %d.\n",
             len, (int)amp, (int)ampy, 2 * sw * sh);
      return false;
    }
    std::vector<int> visits((size_t)sw * sh, 0);
    int8_t px = 0, py = 0;
    for (uint16_t i = 0; i < len; i++) {
      int8_t x, y;
      shift_tour_at(i, amp, ampy, &x, &y);
      if (x < -amp || x > amp || y < -ampy || y > ampy) {
        printf("\n  SELFTEST FAILED: tour left the %dx%d envelope at (%d,%d).\n",
               (int)amp, (int)ampy, (int)x, (int)y);
        return false;
      }
      visits[(size_t)(y + ampy) * sw + (x + amp)]++;
      if (i) {
        int dx = x - px, dy = y - py;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx + dy > 1) {          // one axis, one pixel, or stand still
          printf("\n  SELFTEST FAILED: tour jumped %d px at amp %d "
                 "(%d,%d) -> (%d,%d). The image would visibly twitch.\n",
                 dx + dy, (int)amp, (int)px, (int)py, (int)x, (int)y);
          return false;
        }
      }
      px = x; py = y;
    }
    for (size_t k = 0; k < visits.size(); k++)
      if (visits[k] != 2) {         // exactly twice per forward+back cycle
        printf("\n  SELFTEST FAILED: offset %d of %zu visited %d times at "
               "amp %d, expected 2. Residency is not uniform, so the relief\n"
               "  numbers below would be fiction.\n",
               (int)k, visits.size(), visits[k], (int)amp);
        return false;
      }
  }

  // ---- and prove the shift survives the mounting -----------------------
  // THE TOP ROW IS MOUNTED UPSIDE DOWN (OLED_ROT = {2,2,0,0}), and the blit's
  // rotation exists to cancel that. So the question this checks is NOT "do all
  // four panels shift the same way in GDDRAM" -- that is the wrong frame, and
  // asking it in that frame is precisely how the top row ended up sliding down
  // while the bottom row slid up, shipped, and was caught by eye rather than by
  // this file.
  //
  // The property is stated in VIEWER space: what a person standing in front of
  // the clock sees. Panel coordinates are converted back through the physical
  // mounting before comparing, which for a flipped module is another 180 turn.
  // A correct blit makes those two rotations cancel; a blit that shifts in
  // panel space does not.
  // The per-slot bias is swept alongside, because it is a displacement in the
  // same frame: if it were applied on the wrong side of the rotation it would
  // break this property exactly as the shift once did.
  for (int16_t sy = -SHIFT_MAX; sy <= SHIFT_MAX; sy++)
    for (int16_t sx = -SHIFT_MAX; sx <= SHIFT_MAX; sx++)
    for (int16_t bias = -BIAS_MAX; bias <= BIAS_MAX; bias++) {
      const int16_t x = 40, y = 30;               // well inside the safe area
      int16_t p[4][2];
      const bool flips[2] = {false, true};
      for (int f = 0; f < 2; f++)
        for (int s = 0; s < 2; s++) {             // s=0 at rest, s=1 shifted
          int16_t qx, qy;
          if (!blit_map(x, y, flips[f], s ? sx : 0, s ? sy : 0, bias,
                        &qx, &qy)) {
            printf("\n  SELFTEST FAILED: blit_map clipped a safe-area pixel at "
                   "shift (%d,%d).\n", (int)sx, (int)sy);
            return false;
          }
          // Panel -> what the eye sees. A module mounted upside down turns its
          // own GDDRAM through 180 degrees on the way to the glass.
          if (flips[f]) { qx = (int16_t)(SCR_W - 1 - qx);
                          qy = (int16_t)(SCR_H - 1 - qy); }
          p[f * 2 + s][0] = qx; p[f * 2 + s][1] = qy;
        }
      const int16_t up_dx = (int16_t)(p[1][0] - p[0][0]),
                    up_dy = (int16_t)(p[1][1] - p[0][1]),
                    fl_dx = (int16_t)(p[3][0] - p[2][0]),
                    fl_dy = (int16_t)(p[3][1] - p[2][1]);
      if (up_dx != fl_dx || up_dy != fl_dy) {
        printf("\n  SELFTEST FAILED: shift (%d,%d) moves an upright panel by "
               "(%d,%d) as seen by the viewer, but a flipped one by (%d,%d).\n"
               "  The top and bottom rows would drift apart as the image "
               "wanders. Shift in canvas space, then rotate.\n",
               (int)sx, (int)sy, up_dx, up_dy, fl_dx, fl_dy);
        return false;
      }
      if (up_dx != sx || up_dy != sy) {
        printf("\n  SELFTEST FAILED: shift (%d,%d) displaces the image by "
               "(%d,%d) in viewer space.\n", (int)sx, (int)sy, up_dx, up_dy);
        return false;
      }
    }

  // ---- the safe area survives the bias AND the shift at once -------------
  // These two displacements are independent and CAN point the same way: a slot
  // biased +2 while the tour is at +6 puts safe-area ink 8 rows lower. If the
  // safe area had not given up BIAS_MAX at each end, the bottom row of a
  // layout would clip on the two biased slots only -- a defect that shows up
  // on half the clock and looks like a layout bug rather than a contract one.
  for (uint8_t i = 0; i < N_SCREENS; i++)
    for (int16_t sy = -SHIFT_MAX_Y; sy <= SHIFT_MAX_Y; sy++)
      for (int16_t sx = -SHIFT_MAX; sx <= SHIFT_MAX; sx++)
        for (int f = 0; f < 2; f++) {
          const int16_t xs[2] = {SAFE_X0, SAFE_X1};
          const int16_t ys[2] = {SAFE_Y0, SAFE_Y1};
          for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) {
              int16_t qx, qy;
              if (!blit_map(xs[a], ys[b], f == 1, sx, sy, SLOT_BIAS_Y[i],
                            &qx, &qy)) {
                printf("\n  SELFTEST FAILED: safe-area corner (%d,%d) on slot "
                       "%u clips at shift (%d,%d) with bias %+d.\n"
                       "  The safe area must give up BIAS_MAX at each end in "
                       "y; it is currently y[%d..%d].\n",
                       (int)xs[a], (int)ys[b], i, (int)sx, (int)sy,
                       (int)SLOT_BIAS_Y[i], (int)SAFE_Y0, (int)SAFE_Y1);
                return false;
              }
            }
        }

  // ---- and "centred" means something, given that the image wanders -------
  // A layout is centred on the middle of the safe area, but the burn-in shift
  // moves it every minute, so "centred" is only well defined if the shift adds
  // no SYSTEMATIC offset -- i.e. its mean over a full tour is exactly (0,0).
  // That is true of the serpentine tour and was NOT true of the biased random
  // walk firmware B shipped, whose mean sat about -5 px per axis: on that
  // build every layout was permanently off-centre and no amount of care in
  // the layout code could have fixed it.
  for (int16_t amp = SHIFT_MIN; amp <= SHIFT_MAX; amp++) {
    const int16_t ampy = amp < SHIFT_MAX_Y ? amp : SHIFT_MAX_Y;
    const uint16_t len = shift_tour_len(amp, ampy);
    long sx = 0, sy = 0;
    for (uint16_t k = 0; k < len; k++) {
      int8_t x, y;
      shift_tour_at(k, amp, ampy, &x, &y);
      sx += x; sy += y;
    }
    if (sx != 0 || sy != 0) {
      printf("\n  SELFTEST FAILED: at amplitude %d the shift tour has mean "
             "offset (%ld/%u, %ld/%u), not zero.\n"
             "  Every layout would sit permanently off-centre by that much.\n",
             (int)amp, sx, len, sy, len);
      return false;
    }
  }

  // ---- and THE FRAME-SEQUENCE LAWS, planted with the heart bug itself ------
  // THIS IS THE PLANT THAT MATTERS MOST IN THIS FILE, because the defect it
  // reproduces went out on hardware and was reported by the user. heartFrames
  // was [28][288] with 27 initialisers; C++ zero-filled the 28th and the heart
  // rendered as an empty panel once every 3.5 seconds. Every other law in this
  // file was green through it — a blank canvas has no overlap, nothing outside
  // the safe area and the best fill factor available — so a law that only ever
  // passed would look exactly like this one does now.
  //
  // So: run the real law over a renderer that blanks the last frame of the
  // cycle, and over one whose last frame jumps, and require both to be caught.
  // Then require the UNMODIFIED renderer to be silent on the same animation,
  // because a law that fires on everything is as useless as one that fires on
  // nothing.
  {
    // AN_LOAD: twelve frames, so a plant costs twelve renders rather than 868,
    // and its mid-cycle lit-pixel change is small, which leaves no argument
    // about what an outlier is.
    const size_t f1 = failures.size();
    auto fired = [&](const char *needle) {
      for (size_t i = f1; i < failures.size(); i++)
        if (failures[i].detail.find(needle) == 0) return true;
      return false;
    };
    // NAMING THE LAW, not counting the findings. "something failed" would be
    // satisfied by the wrong law firing, which is how a detector quietly
    // stops detecting the thing it is named after.
    anim_sequence_law(AN_LOAD, anim_draw_blank_plant, "SELFTEST ANIM", false);
    const bool caught_blank = fired("BLANK FRAME");
    failures.resize(f1);
    anim_sequence_law(AN_LOAD, anim_draw_jump_plant, "SELFTEST ANIM", false);
    const bool caught_jump = fired("DISCONTINUOUS LOOP");
    failures.resize(f1);
    // And the case that must stay SILENT. A law that fires on everything is as
    // useless as one that fires on nothing, and this is the only thing that
    // tells them apart.
    anim_sequence_law(AN_LOAD, anim_draw_real, "SELFTEST ANIM", false);
    const bool quiet = failures.size() == f1;
    failures.resize(f1);
    if (!caught_blank || !caught_jump || !quiet) {
      printf("\n  SELFTEST FAILED: blank-frame law caught=%d, "
             "discontinuous-loop law caught=%d, clean renderer quiet=%d.\n"
             "  The heart shipped a blank frame every 3.5 s past every other "
             "law in this file; without these it would again.\n",
             (int)caught_blank, (int)caught_jump, (int)quiet);
      return false;
    }
    // anim_sequence_law() borrows the shared canvas; hand the selftest back
    // the one it was using.
    canvas.fillScreen(0);
    g_elems.clear(); g_cur.clear();
    g_target = &st_canvas;
  }

  // ---- and the SHOWREEL's blank law, planted the same way ------------------
  // The reel's blank law consults a per-(slot,tick) map instead of the
  // per-animation predicate, which makes it a NEW code path — and a law that
  // has never been seen to fire proves nothing. So: render the reel with one
  // undeclared frame blanked, require the law to catch it, then require it
  // silent on the real renderer.
  {
    const size_t f1 = failures.size();
    auto sr_fired = [&](const char *needle) {
      for (size_t i = f1; i < failures.size(); i++)
        if (failures[i].detail.find(needle) == 0) return true;
      return false;
    };
    showreel_sequence_law(0, 0, sr_draw_blank_plant, "SELFTEST SHOWREEL");
    const bool sr_caught = sr_fired("BLANK FRAME");
    failures.resize(f1);
    showreel_sequence_law(0, 0, sr_draw_real, "SELFTEST SHOWREEL");
    const bool sr_quiet = failures.size() == f1;
    failures.resize(f1);
    if (!sr_caught || !sr_quiet) {
      printf("\n  SELFTEST FAILED: showreel blank-frame law caught=%d, "
             "clean renderer quiet=%d.\n"
             "  The reel's blank map is a new axis on the law; unproven, a "
             "dead panel mid-loop would ship.\n",
             (int)sr_caught, (int)sr_quiet);
      return false;
    }
    canvas.fillScreen(0);
    g_elems.clear(); g_cur.clear();
    g_target = &st_canvas;
  }

  // ---- and the FOOT-PLANT law, planted with a skater -----------------------
  // The stride constant was wrong twice before it was measured, both times
  // past every other law in this file — nothing here looks at MOTION. So the
  // new law gets the same treatment as the rest: a renderer whose sprite
  // jumps 4 px on odd ticks must be caught, and the shipping renderer must
  // stay quiet at the artwork's own measured bound.
  {
    const size_t f1 = failures.size();
    auto fp_fired = [&]() {
      for (size_t i = f1; i < failures.size(); i++)
        if (failures[i].detail.find("FOOT SLIDE") == 0) return true;
      return false;
    };
    footplant_law(0, pass_skate_plant, "SELFTEST FOOTPLANT", false);
    const bool fp_caught = fp_fired();
    failures.resize(f1);
    footplant_law(0, pass_draw_real, "SELFTEST FOOTPLANT", false);
    const bool fp_quiet = failures.size() == f1;
    failures.resize(f1);
    if (!fp_caught || !fp_quiet) {
      printf("\n  SELFTEST FAILED: foot-plant law caught the 4 px skater=%d, "
             "shipping walker quiet=%d.\n"
             "  The walker skated twice before this was measured; without "
             "this law it would again.\n",
             (int)fp_caught, (int)fp_quiet);
      return false;
    }
    canvas.fillScreen(0);
    g_elems.clear(); g_cur.clear();
    g_target = &st_canvas;
  }

  return true;
}


// ---------------------------------------------------------------- pages ----
static void run_page(uint8_t pg, uint8_t slot, uint8_t var, const PageData &d,
                     const std::string &label, bool dump) {
  g_elems.clear();
  g_cur.clear();
  g_target = &canvas;
  canvas.fillScreen(0);
  page_render(canvas, pg, slot, var, d);
  flush_current();
  judge(label, dump);
}

static PageData base_page() {
  PageData d{};
  d.clock = base_data();
  d.sens.temp_c10 = 231;
  d.sens.humidity = 46;
  d.sens.sht_ok = true;
  d.sens.wifi_up = true;
  d.sens.rssi = -52;
  snprintf(d.sens.ssid, sizeof d.sens.ssid, "%s", "somewifi");
  snprintf(d.sens.ip, sizeof d.sens.ip, "%s", "192.168.86.52");
  d.sens.uptime_s = 3 * 86400 + 4 * 3600;
  d.sens.contrast = 140;
  d.sens.light_pct = 42;
  return d;
}

static void check_pages() {
  // ---- sensors ----
  // The strings here are what vary, so the enumeration drives LENGTH: an SSID
  // of 31 characters and an uptime that has just gained a digit are the cases
  // that overrun, not a plausible-looking middle value.
  static const char *SSIDS[] = {"", "x", "somewifi",
                                "a-31-character-network-name-abc"};
  static const char *IPS[]   = {"", "10.0.0.1", "192.168.100.200"};
  for (uint8_t var = 0; var < page_variants(PG_SENSOR); var++) {
    for (uint8_t slot = 0; slot < 4; slot++) {
      bool dumped = false;
      for (int i = 0; i < 48; i++) {
        PageData d = base_page();
        d.sens.temp_f   = (i & 1) != 0;
        d.sens.sht_ok   = (i % 7) != 0;
        d.sens.wifi_up  = (i % 5) != 0;
        d.sens.night    = (i & 2) != 0;
        d.sens.rssi     = (int16_t)(-(30 + i));
        d.sens.humidity = (uint8_t)((i * 100) / 47);
        d.sens.contrast = (uint8_t)((i * 255) / 47);
        d.sens.light_pct= (uint8_t)((i * 100) / 47);
        d.sens.temp_c10 = (int16_t)(-400 + i * 30);
        // 1 s, then minutes, hours, days, and a year — every place the format
        // switches shape or grows a digit.
        static const uint32_t UPS[] = {0, 59, 60, 3599, 3600, 86399, 86400,
                                       9 * 86400, 99 * 86400, 400 * 86400};
        d.sens.uptime_s = UPS[i % 10];
        snprintf(d.sens.ssid, sizeof d.sens.ssid, "%s", SSIDS[i % 4]);
        snprintf(d.sens.ip, sizeof d.sens.ip, "%s", IPS[i % 3]);
        char lab[192];
        snprintf(lab, sizeof lab, "page SENSORS v%u slot%u  i=%d %s%s",
                 var, slot, i, d.sens.wifi_up ? "wifi " : "nowifi ",
                 d.sens.sht_ok ? "sht" : "nosht");
        bool dump = !dumped && i == 3;
        if (dump) dumped = true;
        run_page(PG_SENSOR, slot, var, d, lab, dump);
      }
    }
  }

  // ---- stopwatch ----
  // UP's page since 2026-10-15 (it replaced the stock ticker). The renderer is
  // pure in (state, ms), so the sweep drives every minute and second value
  // through both digit panels, the three state words through BL, and the hour
  // count across every width it can take through BR: 0, one digit, two,
  // three, and uint32 ms's ceiling of 1193 h. Named cases on top: 00:00 zero,
  // 59:59 (the last value before the hour carries), and the wide pairs 08:08
  // and 48:48.
  {
    static const uint32_t HOURS[] = {0, 1, 9, 10, 99, 100, 999, 1193};
    static const char *const ST[3] = {"zero", "run", "pause"};
    long n_sw = 0;
    for (uint8_t st = 0; st < 3; st++) {
      for (uint8_t slot = 0; slot < 4; slot++) {
        for (int v = 0; v < 60; v++) {
          for (int hi = 0; hi < 8; hi++) {
            PageData d = base_page();
            d.sw.state = st;
            uint64_t ms = ((uint64_t)HOURS[hi] * 3600u + (uint64_t)v * 60u +
                           (uint64_t)((v * 7) % 60)) * 1000u + (uint64_t)(v * 13);
            if (st == STW_ZERO) ms = 0;
            if (ms > 0xFFFFFFFFull) ms = 0xFFFFFFFFull;
            d.sw.ms = (uint32_t)ms;
            char lab[192];
            snprintf(lab, sizeof lab, "page STOPWATCH %s slot%u ms=%lu",
                     ST[st], slot, (unsigned long)d.sw.ms);
            run_page(PG_STOPWATCH, slot, 0, d, lab, v == 38 && hi == 1);
            n_sw++;
            if (st == STW_ZERO) break;      // zero is one value; no hour axis
          }
          if (st == STW_ZERO) break;
        }
      }
    }
    static const uint32_t NAMED[] = {0u, 3599000u, 3599999u, 8u * 60000u + 8000u,
                                     48u * 60000u + 48000u, 3600000u,
                                     0xFFFFFFFFu};
    for (uint8_t st = 0; st < 3; st++)
      for (uint8_t slot = 0; slot < 4; slot++)
        for (uint32_t ms : NAMED) {
          PageData d = base_page();
          d.sw.state = st;
          d.sw.ms = st == STW_ZERO ? 0 : ms;
          char lab[192];
          snprintf(lab, sizeof lab, "page STOPWATCH %s slot%u ms=%lu named",
                   ST[st], slot, (unsigned long)d.sw.ms);
          run_page(PG_STOPWATCH, slot, 0, d, lab, ms == 3599999u);
          n_sw++;
        }
    printf("  stopwatch       : %ld screens (zero / run / pause, MM:SS full "
           "range, hours 0..1193)\n", n_sw);
  }

  // ---- animations ----
  // EVERY tick of every animation, not a sample. The walker's position is
  // derived from the tick, so the frame that clips is at one specific point in
  // the ping-pong and nowhere near the frame you would spot-check.
  // ---- the animation page, which PLAYS ITSELF -----------------------------
  // THE SCHEDULE IS NOT PROVEN. THE SPACE IT DRAWS FROM IS.
  //
  // ui.cpp picks four animations and four phases at random every five seconds.
  // Sampling one schedule here would be strictly weaker than what follows AND
  // would need exactly the wall-clock nondeterminism a prover may not have.
  // So this sweeps EVERY ANIMATION ON EVERY PANEL ACROSS A FULL CYCLE: any
  // selection the scheduler can ever make is four points already inside this
  // set, so proving the set proves every possible schedule at once, forever,
  // including ones a sampled run would never have produced.
  //
  // The phase offset needs no separate axis: `anim_frame + anim_phase` is what
  // the renderer adds, and sweeping the sum over a whole cycle covers every
  // value the pair can produce.
  for (uint8_t id = 0; id < AN_COUNT; id++) {
    for (uint8_t slot = 0; slot < 4; slot++) {
      bool dumped = false;
      // A FULL CYCLE IS COMPLETE COVERAGE ONLY WHERE THE CYCLE IS EXACT.
      // anim_cycle_exact(id) says frame t and frame t+cycle are the same
      // bitmap, so sweeping one cycle has then seen every frame that exists.
      // Where it is NOT exact -- AN_EYES blinks on four coprime periods, whose
      // true period is 17*23*29*37 = 419,543 frames -- one "cycle" is a
      // presentation length, not a period, so a fixed generous span is swept
      // instead. 600 is well past the old blanket 280 and past every
      // individual blink period.
      int span = (int)anim_cycle(id);
      if (span <= 0) span = 1;
      if (!anim_cycle_exact(id) && span < 600) span = 600;
      if (span < 280) span = 280;
      for (int tick = 0; tick < span; tick++) {
        PageData d = base_page();
        d.anim_frame = (uint16_t)tick;
        // Every panel carries THIS animation, which is also the scheduler's
        // "all four the same" case. The differing-panels case is covered
        // because each panel is rendered independently from its own slot of
        // this array and run_page draws one slot at a time.
        for (int k = 0; k < 4; k++) { d.anim_ids[k] = id; d.anim_phase[k] = 0; }
        char lab[192];
        snprintf(lab, sizeof lab, "page ANIM %s slot%u tick=%d",
                 anim_name(id), slot, tick);
        bool dump = !dumped && tick == 0;
        if (dump) dumped = true;
        run_page(PG_ANIM, slot, 0, d, lab, dump);
      }
    }
  }

  // ---- settings -----------------------------------------------------------
  // The settings page is the only page whose rows are chosen at RENDER time
  // from a cursor, so the enumeration has to walk the cursor as well as the
  // slot: which item lands on which panel is a function of both, and the
  // scrolling window means item 4 is only ever drawn on some of them.
  //
  // Values are enumerated for their WIDTH. `i * 15` sweeps every quarter hour
  // of the day, which is every string the time formatter can produce, and the
  // two times are driven in opposite directions so a wide one is never paired
  // only with another wide one.
  for (uint8_t cur = 0; cur < SI_COUNT; cur++) {
    for (uint8_t slot = 0; slot < 4; slot++) {
      bool dumped = false;
      for (int i = 0; i < 96; i++) {
        PageData d = base_page();
        d.set.cursor      = cur;
        d.set.sens_level  = (uint8_t)(i % 5);       // OFF, LOW, MED, HIGH, MAX
        d.set.temp_f      = (uint8_t)(i & 1);
        d.set.off_enable  = (uint8_t)((i >> 1) & 1);
        d.set.off_start_h = (uint8_t)(((i * 15) / 60) % 24);
        d.set.off_start_m = (uint8_t)((i * 15) % 60);
        d.set.off_end_h   = (uint8_t)(23 - (((i * 15) / 60) % 24));
        d.set.off_end_m   = (uint8_t)((95 - i) * 15 % 60);
        char lab[192];
        snprintf(lab, sizeof lab,
                 "page SETTINGS cursor=%u slot%u  i=%d win=%u sens=%u",
                 cur, slot, i, set_window_start(cur), d.set.sens_level);
        bool dump = !dumped && i == 0;
        if (dump) dumped = true;
        run_page(PG_SETTINGS, slot, 0, d, lab, dump);
      }
    }
  }

  // ---- the clock page, as the buttons actually reach it -------------------
  // The faces are already enumerated above over their full value domains, but
  // that proves the WIDGETS. This proves the PAGE: the specific widget, style
  // and overlay each slot gets, including the fallback when a slot's widget
  // cannot honour the selected style.
  for (uint8_t var = 0; var < page_variants(PG_CLOCK); var++) {
    for (uint8_t slot = 0; slot < 4; slot++) {
      for (int v = 0; v < 60; v++) {
        PageData d = base_page();
        d.clock.hour = (uint8_t)(v % 24);
        d.clock.minute = (uint8_t)v;
        d.clock.second = (uint8_t)v;
        d.clock.day = (uint8_t)(1 + (v % 31));
        d.clock.month = (uint8_t)(1 + (v % 12));
        d.clock.year = (uint16_t)(2000 + v);
        d.clock.weekday = fx_weekday(d.clock.year, d.clock.month, d.clock.day);
        d.clock.hour24 = (v & 1) != 0;
        char lab[192];
        // NAME THE STYLE. The label is what the fill-factor policy above
        // classifies on, and "v7" tells it nothing: every clock-page screen
        // was being booked as a DEFAULT-STYLE screen, including the FILLED
        // stop, which is a style the user chose by pressing MODE. Naming it
        // makes the debt figure mean what it says.
        snprintf(lab, sizeof lab, "page CLOCK v%u %s slot%u v=%d",
                 var, style_name(CLOCK_STYLES[var % CLOCK_STYLE_N]), slot, v);
        // ONE FRAME PER STOP PER PANEL ON THE CONTACT SHEET. The face sweep
        // above dumps the six original glyph styles but never the INVERTED
        // half of the cycle — the invert is a bit on the style and that loop
        // walks bare styles — so half the MODE cycle was proven and none of it
        // was ever looked at. Every stop the button can reach now appears on
        // the sheet. v=38 for the same reason the face sweep picks it: a
        // two-digit value in every field.
        run_page(PG_CLOCK, slot, var, d, lab, v == 38);
      }
    }
  }
}

int main(int argc, char **argv) {
  face_elem_hook = elem_hook;

  const char *outdir = argc > 1 ? argv[1] : ".";
  char p1[512], p2[512];
  snprintf(p1, sizeof p1, "%s/frames.bin", outdir);
  snprintf(p2, sizeof p2, "%s/frames.txt", outdir);
  dump_bin = fopen(p1, "wb");
  dump_txt = fopen(p2, "w");

  if (!selftest()) return 2;

  // ---- every clock face, over its entire value domain --------------------
  // Exhaustive, not sampled. The domains are small enough that there is no
  // reason to guess which value is the worst case — and the worst case is
  // rarely the one you would guess (it is usually a width change, like the
  // minute going from 9 to 10, or SEVENTEEN being nine characters).
  //
  // AND EVERY MODIFIER BIT, NOT JUST THE BARE STYLE. A style byte carries two
  // flags on top of its enum value — S_HOLLOW (swap the display tier for its
  // outline cut) and S_INVERT (composite the element as a negative inside a
  // lit chip) — and this sweep used to walk neither. That was survivable only
  // while CLOCK_STYLES[] happened to contain every combination anybody could
  // reach, and it stopped being true the moment the MODE cycle was re-cut:
  // the inverted stops came out of the table and instantly had NOTHING
  // proving them, while their renderer went on compiling and shipping.
  //
  // ENUMERATION MUST NOT DEPEND ON WHAT THE CYCLE CURRENTLY OFFERS. What the
  // renderer can draw is what has to be checked, so that re-cutting the cycle
  // is a table edit rather than a silent loss of coverage. S_HOLLOW is walked
  // only on the themed styles because only they have a `bigo` tier; the
  // classic renderer ignores the bit entirely and walking it there would
  // enumerate thousands of pixel-identical screens.
  for (uint8_t w = 0; w < W_COUNT; w++) {
    for (uint8_t bs = 0; bs < S_COUNT; bs++) {
      if (!widget_allows(w, bs)) continue;
      const bool themed = bs >= S_THEME_FIRST &&
                          bs < (uint8_t)(S_THEME_FIRST + S_THEME_N);
      uint8_t mods[3];
      int nm = 0;
      mods[nm++] = bs;
      if (themed) mods[nm++] = (uint8_t)(bs | S_HOLLOW);
      mods[nm++] = (uint8_t)(bs | S_INVERT);
      for (int mi = 0; mi < nm; mi++) {
      const uint8_t s = mods[mi];
      for (uint8_t ov = 0; ov < OV_COUNT; ov++) {
        bool dumped = false;
        for (int v = 0; v < 60; v++) {
          FaceData d = base_data();
          // Drive the primary field of this widget across its whole range,
          // and move the overlay's own value with it so their widths vary
          // independently of each other.
          d.hour   = (uint8_t)(v % 24);
          d.minute = (uint8_t)v;
          d.second = (uint8_t)v;
          d.day    = (uint8_t)(1 + (v % 31));
          d.month  = (uint8_t)(1 + (v % 12));
          d.year   = (uint16_t)(2000 + v);
          d.weekday = fx_weekday(d.year, d.month, d.day);
          d.humidity = (uint8_t)((v * 100) / 59);
          // Temperature across a real indoor-to-silly span, including
          // negative, which is the case that grows the string by a character.
          d.temp_c10 = (int16_t)(-200 + v * 12);
          for (int f = 0; f < 2; f++) {
            d.temp_f = (f == 1);
            for (int h = 0; h < 2; h++) {
              d.hour24 = (h == 1);
              char lab[192];
              snprintf(lab, sizeof lab, "face %s / %s / ov=%s  v=%d %s %s",
                       widget_name(w), style_name(s), overlay_name(ov), v,
                       d.temp_f ? "F" : "C", d.hour24 ? "24h" : "12h");
              // Dump one representative frame per widget/style/overlay for
              // the contact sheet; judge every single one.
              bool dump = !dumped && v == 38 && f == 0 && h == 0;
              if (dump) dumped = true;
              run_face(w, s, ov, d, lab, dump);
            }
          }
        }
      }
      }
    }
  }

  // ---- the RTC-unreadable path -------------------------------------------
  {
    FaceData d = base_data();
    d.valid = false;
    for (uint8_t w = 0; w < W_COUNT; w++)
      for (uint8_t ov = 0; ov < OV_COUNT; ov++)
        run_face(w, S_OUTLINE, ov, d, std::string("face ") + widget_name(w) +
                 " / invalid RTC", false);
  }

  // ---- every page, slot and variant --------------------------------------
  // Start clean. Without this the first page case is judged together with
  // whatever the last face render left in g_elems, and reports a phantom
  // overlap between a clock overlay and a page element.
  g_elems.clear();
  g_cur.clear();
  canvas.fillScreen(0);

  check_pages();

  // EVERY frame of EVERY animation, over its whole cycle — which for the
  // walker is 868 ticks, far past where the page sweep above stops.
  check_anims();

  // And the showreel: its sequence laws per slot, then every tick of every
  // slot through judge() across the whole digit domain.
  check_showreel();

  if (dump_bin) fclose(dump_bin);
  if (dump_txt) fclose(dump_txt);

  // ------------------------------------------------------------- report ----
  printf("\n");
  printf("=================== LAYOUT PROVER ===================\n");
  printf("  selftest        : PASS (planted overlap, edge, BLANK-FRAME and "
         "DISCONTINUOUS-LOOP caught,\n                    showreel blank-map "
         "plant and 4 px FOOT-SLIDE skater caught, clean\n                    "
         "renderers quiet; shift tour uniform and 1 px/step; the shift "
         "survives\n                    the flipped mounting)\n");
  printf("  screens checked : %ld\n", n_cases);
  printf("  animations      : %u, %ld frames over their full cycles\n",
         (unsigned)anim_total(), n_anim_frames);
  printf("  showreel        : %u ticks x 4 slots, %ld judged frames over the "
         "walker's 4 variants\n                    and the digit, sensor and "
         "quote domains; blank maps honoured,\n                    periods "
         "exact, loop continuous\n",
         (unsigned)SHOWREEL_TICKS, n_sr_frames);
  printf("  foot plant      : %ld consecutive-tick pairs across 4 variants, "
         "worst slide %d px\n                    (artwork residual bound "
         "%d px; ground speed is the measured stride)\n",
         n_footplant_pairs, footplant_worst, FOOT_SLIDE_MAX_PX);
  printf("  frames dumped   : %ld\n", n_frames_dumped);
  printf("  safe area       : x[%d..%d] y[%d..%d]  (%dx%d of %dx%d)\n",
         (int)SAFE_X0, (int)SAFE_X1, (int)SAFE_Y0, (int)SAFE_Y1,
         (int)SAFE_W, (int)SAFE_H, SCR_W, SCR_H);
  // WHERE DEAD CENTRE ACTUALLY IS, per screen, stated rather than assumed.
  // A layout centres on the middle of the safe area; the slot's measured bias
  // then moves the whole panel onto the middle of what the eye can SEE through
  // the case; and the burn-in shift wanders about that point with a mean of
  // exactly zero (proven in selftest), so it moves the image without moving
  // its centre. The three together are what "centred" means on this device.
  printf("  dead centre     : safe %.1f,%.1f  ->  per slot y",
         (SAFE_X0 + SAFE_X1) / 2.0, (SAFE_Y0 + SAFE_Y1) / 2.0);
  for (uint8_t i = 0; i < N_SCREENS; i++)
    printf(" %.1f", (SAFE_Y0 + SAFE_Y1) / 2.0 + SLOT_BIAS_Y[i]);
  printf("   (bias %+d %+d %+d %+d, measured)\n",
         (int)SLOT_BIAS_Y[0], (int)SLOT_BIAS_Y[1],
         (int)SLOT_BIAS_Y[2], (int)SLOT_BIAS_Y[3]);
  printf("  lit pixels      : %.1f%% mean, %.1f%% worst (budget %.0f%%)  %s\n",
         n_budget_cases ? fill_sum / n_budget_cases : 0.0, fill_max, FILL_BUDGET_PCT,
         fill_worst.empty() ? "" : fill_worst.c_str());
  if (fill_over_optin)
    printf("                    %ld opt-in style / animation frames over budget — warned, not blocked\n",
           fill_over_optin);
  if (n_over_budget)
    printf("                    %ld DEFAULT-STYLE screens over budget, worst %.1f%% (debt, ratcheted)\n",
           n_over_budget, over_max);
  // The MODE cycle, stop by stop, in the order the button walks them.
  if (!look_fill.empty()) {
    printf("  MODE cycle fill : %zu stops, worst panel per stop "
           "(budget %.0f%%; every stop past the first is opt-in)\n",
           look_fill.size(), FILL_BUDGET_PCT);
    for (size_t i = 0; i < look_fill.size(); i++)
      printf("                    %2zu. %-14s %5.1f%%  %s\n", i + 1,
             look_fill[i].name.c_str(), look_fill[i].worst,
             look_fill[i].over ? "" : "(within budget on every value)");
  }
  printf("  shift relief    : %.2fx worst-screen at +/-%d  (floor %.2fx)  %s\n",
         relief_min > 1e8 ? 0.0 : relief_min, SHIFT_AMP_UNDER_TEST, RELIEF_FLOOR,
         relief_worst.empty() ? "" : relief_worst.c_str());

  // Collapse identical detail strings — one geometry bug shows up in hundreds
  // of value cases and a wall of duplicates hides the second bug.
  auto summarise = [](std::vector<Failure> &v, const char *title) {
    if (v.empty()) return;
    printf("\n  %s (%zu):\n", title, v.size());
    std::vector<std::pair<std::string, std::pair<std::string, int>>> uniq;
    for (auto &f : v) {
      bool found = false;
      for (auto &u : uniq)
        if (u.first == f.detail) { u.second.second++; found = true; break; }
      if (!found) uniq.push_back({f.detail, {f.kase, 1}});
    }
    for (auto &u : uniq)
      printf("    x%-5d %s\n            e.g. %s\n",
             u.second.second, u.first.c_str(), u.second.first.c_str());
  };
  // The ratchet is judged once, on totals, not per screen.
  if (n_over_budget > DEBT_SCREENS_MAX) {
    char d[220];
    snprintf(d, sizeof d,
             "BURN-IN DEBT GREW: %ld default-style screens over the %.0f%% budget, "
             "was %ld. A layout change added over-budget screens.",
             n_over_budget, FILL_BUDGET_PCT, DEBT_SCREENS_MAX);
    failures.push_back({"burn-in budget ratchet", d});
  }
  if (over_max > DEBT_WORST_PCT + 0.05) {
    char d[220];
    snprintf(d, sizeof d,
             "BURN-IN DEBT GREW: worst default-style screen is now %.1f%% lit, was %.1f%%.",
             over_max, DEBT_WORST_PCT);
    failures.push_back({"burn-in budget ratchet", d});
  }

  summarise(warnings, "WARNINGS");
  summarise(failures, "FAILURES");

  printf("\n");
  if (failures.empty()) {
    printf("  RESULT: PASS — no screen has two things on the same pixel,\n");
    printf("          and nothing is drawn where the shift would clip it.\n");
  } else {
    printf("  RESULT: FAIL — %zu findings above.\n", failures.size());
  }
  printf("=====================================================\n\n");
  return failures.empty() ? 0 : 1;
}
