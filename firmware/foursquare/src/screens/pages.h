// pages.h — version A: four buttons, four pages.
//
// THE GRAMMAR, in one sentence:
//   each button owns a page, and pressing it again cycles a variant of it.
//
//   MODE  -> the clock        (again: next clock style)
//   SET   -> sensor readings  (again: next reading set)
//   UP    -> stopwatch        (start / resume; DOWN there pauses, then clears)
//   DOWN  -> animations       (again: next animation)
//
// All four panels render the same page, one slot each. There is no menu, no
// modal editor and no "PREVIEW" caption — a press changes what you are looking
// at, immediately, and that is the whole interface.
//
// EVERYTHING IN HERE IS PURE, for exactly the reason faces.h is: it is handed
// a GFXcanvas1 and a snapshot, so tools/layoutcheck compiles THIS source and
// proves no two elements share a pixel on any page, slot or variant.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>
#include "faces.h"
#include "anim.h"     // the showreel's scene pieces and their tick lengths

// PG_SETTINGS is NOT owned by a button. The four short presses still mean the
// four pages above and nothing else; settings are reached by HOLDING SET, and
// while you are in there the same four buttons mean next-item / change /
// change / exit. That is the one deliberate exception to "a button is always a
// direct jump", and it is bounded: the page times out back to the clock, so it
// is still true that you cannot get stuck anywhere.
enum Page : uint8_t {
  PG_CLOCK = 0, PG_SENSOR, PG_STOPWATCH, PG_ANIM, PG_SETTINGS, PG_COUNT
};
// PG_STOPWATCH TOOK THE MARKETS PAGE'S SLOT, index 2, on purpose: the page
// index is what serial `W` and the acceptance script report, and keeping the
// numbering means CLOCK, SENSOR, ANIM and SETTINGS did not move.

// ---- the stopwatch page ----------------------------------------------------
// A SNAPSHOT, like everything else here: the state and the elapsed ms, already
// computed by ui.cpp (which owns millis()). The renderer never reads a clock,
// so the prover can enumerate every value it can show.
enum StwState : uint8_t { STW_ZERO = 0, STW_RUN, STW_PAUSE };
struct StopwatchData {
  uint8_t  state;         // StwState
  uint32_t ms;            // elapsed, frozen while paused, 0 when zero
};

// The clock styles MODE cycles, in order. OUTLINE is first because it is both
// the look the user picked and the lowest-fill one, so the default costs the
// least panel life. BARS and DOTS are deliberately absent — they were tried
// and rejected; SHADOW and STENCIL replace them.
extern const uint8_t CLOCK_STYLES[];
extern const uint8_t CLOCK_STYLE_N;

struct SensorData {
  int16_t  temp_c10;      // tenths of a degree C
  uint8_t  humidity;      // %RH
  bool     sht_ok;
  bool     wifi_up;
  int16_t  rssi;          // dBm, negative
  char     ssid[24];
  char     ip[20];
  uint32_t uptime_s;
  uint8_t  contrast;      // what display.cpp is actually driving, 0-255
  bool     night;
  uint8_t  light_pct;     // ambient, 0-100
  bool     temp_f;
};

// Prices arrive as TEXT, already formatted. Since the stock ticker page was
// removed (2026-10-15) the only reader is the showreel's STOCKS segment, fed
// from a fixed basket in ui.cpp; nothing fetches quotes any more.
//
// Prices arrive as TEXT, already formatted by the fetcher. A renderer that
// did its own float formatting would have to be handed a float, and then the
// prover could not enumerate it — there is no finite set of floats. A short
// string has a finite set of lengths, which is what makes the layout provable.
struct Quote {
  char    sym[8];
  char    price[12];
  int16_t chg_bp;         // change, hundredths of a percent
  bool    valid;
};

// ---- the settings page -----------------------------------------------------
// The items, in the order SET walks them.
enum SetItem : uint8_t {
  SI_SENS = 0,        // how hard room light moves the panel brightness
  SI_TEMP,            // C or F, everywhere on the device
  SI_OFF_EN,          // the daily screens-off schedule, on or off
  SI_OFF_START,       // what time it goes dark
  SI_OFF_END,         // and what time it comes back
  SI_COUNT
};

// A SNAPSHOT of the settings the page displays, for the same reason FaceData
// is a snapshot: the renderer must produce identical pixels for identical
// input or the prover proves nothing about it. Reading the live `cfg` global
// from here would also make this the first impure page, and the prover cannot
// enumerate a global it does not control.
struct SettingsData {
  // 0 = OFF, then DimSens 1..4. OFF is a real position on this dial even
  // though it is not a value of cfg.dim_sens — it is cfg.autodim == 0. The UI
  // flattens the two fields into one knob here so the renderer sees a single
  // ordinal and the prover has one axis to walk.
  uint8_t sens_level;
  uint8_t temp_f;
  uint8_t off_enable;
  uint8_t off_start_h, off_start_m;
  uint8_t off_end_h,   off_end_m;
  uint8_t cursor;                   // which SetItem is selected, 0..SI_COUNT-1
};

// The label and value strings for one item. In pages.cpp — the PURE layer —
// and not in ui.cpp, so the prover enumerates THE STRINGS THAT SHIP rather
// than a second set written to match. Widths are what break a layout, and a
// reimplementation that agreed today would drift the first time a label grew.
void set_row_text(const SettingsData &s, uint8_t item,
                  char *label, uint8_t label_sz, char *value, uint8_t value_sz);

// Which item lands on slot 0. There are more items than panels, so the four
// visible rows follow the cursor. Exported because the prover walks it.
uint8_t set_window_start(uint8_t cursor);

struct PageData {
  FaceData     clock;
  SensorData   sens;
  Quote        q[4];
  StopwatchData sw;
  uint16_t     anim_frame;
  // ---- the animation page, which PLAYS ITSELF -----------------------------
  // FOUR INDEPENDENT STREAMS, one per panel: which drawing that panel shows,
  // and its own phase offset into the shared frame counter. The offset is what
  // stops four copies of the same animation beating in lockstep -- the page
  // deliberately shows the same drawing on all four panels some of the time,
  // and identical phase would make that read as one wide picture rather than
  // as four clocks doing the same thing.
  //
  // The SCHEDULE (what to show, when to change) belongs to ui.cpp, which has
  // `now`. Everything here is a pure input, so the prover can sweep the whole
  // space of what these fields can hold instead of one particular schedule.
  uint8_t      anim_ids[4];
  uint16_t     anim_phase[4];
  // ---- which scene the DOWN page is playing -------------------------------
  // The page has three scenes and ui.cpp owns the schedule between them:
  //   SCENE_ROSTER    the four shuffled streams above
  //   SCENE_SHOWREEL  the segmented attract-mode loop (showreel_draw below),
  //                   the page's FIRST stop — arrival lands here
  //   SCENE_CLOCK     the giant one-digit-per-panel clock interlude, which
  //                   ui.cpp cuts to every ~10 s of ROSTER so the page always
  //                   tells the time; inside the SHOWREEL the same interlude
  //                   is a segment of the reel itself
  // All three read `anim_frame` for their tick; the reel and the clock also
  // read `clock` (and the reel `sens` and `q`), all fields this snapshot
  // already carries — a scene is one byte, not a new data path.
  uint8_t      anim_scene;
  // The walker pass VARIANT for this reel loop — bit 0 row, bit 1 direction
  // (anim.h). ui.cpp re-rolls it at every reel wrap; the renderer and the
  // blank map are pure in it, so the prover enumerates all four.
  uint8_t      sr_walk;
  SettingsData set;
};

enum AnimScene : uint8_t { SCENE_ROSTER = 0, SCENE_SHOWREEL, SCENE_CLOCK };

// ===========================================================================
// THE SHOWREEL — the segmented attract-mode loop, and the clock interlude.
// ===========================================================================
// Everything is pure and per-slot, so the prover enumerates it exactly like
// the pages: showreel_draw is handed the SAME PageData snapshot page_render
// gets, and the blank maps are data-independent functions of (slot, tick)
// that the sequence laws hold the renderer to.
//
// A TICK IS NO LONGER ONE DURATION. Dirty-panel commits in display.cpp mean
// a tick's bus cost is only the panels that CHANGED, so segments that dirty
// one or two panels per tick (the walker's crossing, the weather skies) run
// at SR_FAST_MS = 63 ms (~16 fps) while segments that repaint all four
// (the heartbeat) or barely move (clock hold, sensors, stocks) stay at
// ANIM_FRAME_MS = 125. showreel_tick_ms() is the one statement of which is
// which; ui.cpp schedules from it and wall-clock durations below follow.
//
// The reel: SHOWREEL_TICKS = 747, ~66 s, alternating content and the clock:
//
//   segment    ticks        ms/tick   seconds   what
//   CLOCK      0..79        125       10.0      digit slam + hold
//   WALKER     80..362      63        17.8      one randomized row crossing
//   SENSORS    363..418     125        7.0      live temp/RH/light/uptime
//   CLOCK      419..498     125       10.0      the time again
//   WEATHER    499..658     63        10.1      baked forecast, animated sky
//   STOCKS     659..714     125        7.0      markets renderer, demo quotes
//   HEART      715..741     125        3.4      one heartbeat, phase-locked
//   BLANK      742..746     125        0.6      a clean beat, then the wrap
static const uint16_t SHOWREEL_TICKS = 747;
static const uint16_t SR_FAST_MS     = 63;    // the dirty-commit tick

// The boundaries, derived by summation — EXPORTED so the prover can aim its
// data-domain sweeps at the segment that actually reads the data (the sensor
// sweep at the sensor ticks, the quote sweep at the stock ticks) instead of
// multiplying every axis by every tick.
static const uint16_t SR_SENS_LEN = 56, SR_WX_LEN = 160, SR_STK_LEN = 56;
static const uint16_t SR_BLANK_LEN = 5;
static const uint16_t SR_AT_CLK1  = 0;
static const uint16_t SR_AT_WALK  = (uint16_t)(SR_AT_CLK1 + 80);  // SHOWCLOCK_TICKS
static const uint16_t SR_AT_SENS  = (uint16_t)(SR_AT_WALK + SR_WALK_TICKS);
static const uint16_t SR_AT_CLK2  = (uint16_t)(SR_AT_SENS + SR_SENS_LEN);
static const uint16_t SR_AT_WX    = (uint16_t)(SR_AT_CLK2 + 80);
static const uint16_t SR_AT_STK   = (uint16_t)(SR_AT_WX + SR_WX_LEN);
static const uint16_t SR_AT_HEART = (uint16_t)(SR_AT_STK + SR_STK_LEN);
static const uint16_t SR_AT_BLANK = (uint16_t)(SR_AT_HEART + SR_HEART_TICKS);
static_assert(SR_AT_BLANK + SR_BLANK_LEN == SHOWREEL_TICKS,
              "the reel's segments no longer sum to SHOWREEL_TICKS. Retime "
              "one of them; the timing map in the comment above is then "
              "stale too.");

void showreel_draw(GFXcanvas1 &c, uint8_t slot, uint16_t tick,
                   const PageData &d);
// How long THIS tick of the reel should be shown, in ms. Pure, so the wall
// clock of the reel is stated once, here, and ui.cpp only schedules it.
uint16_t showreel_tick_ms(uint16_t tick);
// The blank map: whether THIS slot at THIS tick may light nothing. Blankness
// in the reel is choreography — the walker is elsewhere, a stagger has not
// reached this panel yet, the final beat — and the map is derived from the
// same segment table the renderer walks. Data-independent given d.sr_walk
// (the one field it reads): every segment's renderer lights ink on every
// input once its stagger has arrived.
bool showreel_blank_ok(uint8_t slot, uint16_t tick, const PageData &d);

// The clock interlude on its own: 12-hour with a leading zero, TL=hour tens,
// TR=hour units, BL=minute tens, BR=minute units, each digit slamming in
// size 3 -> 5 -> 7 staggered one tick per slot, then HELD to the end.
// Exported separately because it serves twice: as the reel's clock segments,
// and as the interlude ui.cpp cuts to every ~10 s of the shuffled roster —
// one clock, both modes. It is played ONCE per interlude, never wrapped on
// the glass (the roster resumes after it; inside the reel the wrap belongs
// to the reel), which is why its prover law checks the blank map and the
// period but not wrap continuity — there is no wrap to be continuous.
static const uint16_t SHOWCLOCK_TICKS = 80;   // 10.0 s
static_assert(SHOWCLOCK_TICKS == 80,
              "the SR_AT_* map above types 80 for the two clock segments; "
              "change them together.");
void showclock_draw(GFXcanvas1 &c, uint8_t slot, uint16_t tick,
                    uint8_t hour, uint8_t minute);
bool showclock_blank_ok(uint8_t slot, uint16_t tick);

// The one entry point. `slot` is which of the four panels (0-3).
void page_render(GFXcanvas1 &c, uint8_t page, uint8_t slot, uint8_t variant,
                 const PageData &d);

// How many variants a page cycles through. Used by the button handler and by
// the prover, so what is reachable and what is checked cannot drift apart.
uint8_t page_variants(uint8_t page);

const char *page_name(uint8_t page);
