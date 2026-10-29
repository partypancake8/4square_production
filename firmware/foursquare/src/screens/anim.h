// anim.h, the DOWN page. The four 48x48 bitmap animations carried over from
// the old breadboard 2x2 clock (oled2x2x, Wokwi animator, art by icons8), plus
// seventeen PROCEDURAL ones drawn with GFX primitives.
//
// PURE, like faces.cpp and pages.cpp: handed a canvas and a tick, nothing
// else. That is what lets the prover enumerate every frame of every animation
// and prove none of them draws outside the shift envelope.
//
// WHY THE NEW ONES ARE PROCEDURAL AND NOT MORE BITMAPS. A bitmap animation is
// a list of initialisers, and a list of initialisers can be one short, which
// is exactly what happened to the heart (see anim.cpp). A procedural animation
// is a pure function of the tick, so there is no frame table to get out of step
// with a declared count, and it costs a few hundred bytes of flash instead of
// 8 KB. It also scales: a 48x48 bitmap cannot be shrunk (a scaled 1-bit bitmap
// is a smear), so four eyes on one panel was only ever going to be drawable.
//
// WHY EVERYTHING STAYS INSIDE THE SAFE AREA. The original walker walked off
// both edges of the panel. Here he turns around at the safe margin instead, and
// every procedural animation is composed through safe-area-clipped primitives.
// Ink outside the safe area would be clipped once the anti-burn-in wander
// reaches its extremes, and on an animation that would read as a rendering bug
// rather than as a policy. The safe area is SAFE_X0..SAFE_X1 by SAFE_Y0..SAFE_Y1
// from display.h, which on THIS build is x[6..121] y[6..57], 116x52. Nothing
// in anim.cpp types those numbers; every extent is derived from the constants.
// In the sibling tree the same code runs against a 110-column safe area (the
// calibration nudge is bought out of the columns there), and a bar row that
// assumed 116 is exactly how the last channel of the equaliser ended up under
// the clip. Derive, never type.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>

// THE IDS ARE STABLE. The original four keep 0..3, they are what
// pages.cpp's `slot & 3` fallback renders for variant 0, one animation per
// panel, and reordering them would silently reshuffle which face lives on
// which panel of a device already in a case. Everything new is appended.
enum AnimId : uint8_t {
  // -- the four bitmap animations, unchanged ids ----------------------------
  AN_WALK = 0,      // walking figure, ping-ponging across the safe area
  AN_EYE,           // one big blinking eye
  AN_FIDGET,        // fidget spinner
  AN_HEART,         // beating heart
  // -- procedural, appended -------------------------------------------------
  AN_EYES,          // four small eyes, each blinking on its own prime period
  AN_ORBIT,         // three dots orbiting a core at three radii and speeds
  AN_SWING,         // a pendulum on a rod
  AN_BALL,          // a bouncing ball that squashes on the floor
  AN_WAVE,          // a travelling sine
  AN_RIPPLE,        // concentric rings expanding from the centre
  AN_CUBE,          // rotating wireframe cube
  AN_SPIRAL,        // a rotating Archimedean spiral of dots
  AN_STARS,         // a starfield streaming left at three speeds
  AN_METRO,         // a metronome ticking over its plinth
  AN_SAND,          // an hourglass draining and refilling
  AN_LOAD,          // a twelve-spoke loading spinner
  AN_BREATHE,       // a ring breathing in and out
  AN_HELIX,         // a DNA double helix with rungs
  AN_SNAKE,         // a worm crawling a rectangular circuit
  AN_RAIN,          // rain falling into a puddle line
  AN_BARS,          // a nine-channel segmented equaliser
  AN_COUNT
};

static const int16_t ANIM_W = 48;
static const int16_t ANIM_H = 48;

// THE BUS SETS THIS, not taste. A frame is 1024 bytes; at 400 kHz with the
// per-byte ACK that is ~23 ms per panel, so all four cost ~92 ms. The original
// asked for 42 ms because it never had to share the bus with an RTC, a
// humidity sensor and an EEPROM.
//
// 125 ms is 8 fps and about 75% bus occupancy, which leaves the sensors their
// 1 Hz slots. Asking for more would not run faster, it would drop frames
// unevenly, which reads as a stutter rather than as a slower walk.
static const uint32_t ANIM_FRAME_MS = 125;

uint8_t     anim_count(uint8_t id);        // frames in this animation
const char *anim_name(uint8_t id);

// Draw animation `id` at `tick` onto the canvas. `tick` is a free-running
// counter, not a frame index, the walker derives its position from it too.
void anim_draw(GFXcanvas1 &c, uint8_t id, uint16_t tick);

// ===========================================================================
// THE SHOWCASE REEL API. All pure, all const, all safe to call from a prover.
// ===========================================================================
// A reel plays animations back to back. To do that without cutting one off
// mid-beat it needs three things it cannot work out for itself: how many there
// are, what order to play them in, and how long one whole idea lasts.

// How many animations exist. Identical to AN_COUNT; a function so a caller
// that only includes this header for the reel does not have to reach into the
// enum, and so the count can never be typed twice.
uint8_t anim_total();

// The animation at reel position `i`, 0 <= i < anim_total(). A PERMUTATION of
// 0..AN_COUNT-1, every animation appears exactly once, ordered so that
// consecutive entries look different from each other rather than in enum
// order, which would play the four bitmaps and then seventeen line drawings.
// Out of range returns AN_WALK.
uint8_t anim_reel(uint8_t i);

// How many ticks the reel should show to see the whole of this animation, in
// frames. Multiply by ANIM_FRAME_MS for milliseconds. Never zero.
//
// For everything except AN_EYES this is the EXACT repeat period: frame t and
// frame t + anim_cycle(id) are the same bitmap, for every t. AN_EYES is the
// exception on purpose, its four eyes blink on periods 17/23/29/37, whose
// true common period is 419,543 frames (14.6 hours), which is the point of it.
// What it returns instead is a length over which every eye blinks at least
// twice. anim_cycle_exact() is how a caller tells those two cases apart.
uint16_t anim_cycle(uint8_t id);

// True when anim_cycle(id) is a real period, anim_draw(c, id, t) and
// anim_draw(c, id, t + anim_cycle(id)) produce identical canvases for all t.
// False when it is only a suggested viewing length (AN_EYES). The layout
// prover checks this claim by rendering both frames and comparing them, so it
// cannot quietly become a lie.
bool anim_cycle_exact(uint8_t id);

// ===========================================================================
// SHOWREEL SCENE PIECES. The reel itself, the segmented attract-mode loop  - 
// lives in pages.cpp, because its sensor, stock and clock segments compose
// from PageData and from the page renderers. What lives HERE is the pieces
// that are ANIMATION: the walker's lap, the phase-locked heart, and the
// little weather drawings. All pure: canvas + tick in, pixels out.
// ===========================================================================

// ---- the walker's pass -----------------------------------------------------
// ONE RULE OF MOTION: he walks. Horizontally, at constant speed, in one
// direction, and that is ALL he does, he never stops, never turns, never
// moves vertically, never teleports. (The v3 lap had descent/ascent legs and
// a mid-panel turn; the user cut them.) A pass enters from OFF the glass at
// one side edge of a row, walks continuously across BOTH panels of that row
//, crossing the bezel with the established one-frame hidden beat, and
// exits fully off the far edge.
//
// GROUND SPEED IS THE ARTWORK'S, MEASURED, NOT CHOSEN. The physics of a
// non-sliding walk: while a foot is planted it stays at a fixed x on the
// GLASS, so the body must advance at exactly the rate the artwork sweeps the
// planted foot backward through the sprite box. This artwork (measured by
// a separate foot-plant measuring tool) is a HYBRID
// in-place cycle: each 14-frame half-step holds the planted foot ~5 frames,
// then sweeps it back ~1.3 px/frame over ~9 frames, a mean stride of
// SR_STRIDE_PX = 1 px per artwork frame (13-14 px per half-step). Rendered
// and measured at this stride: the planted foot's glass movement is 0 px on
// 66% of tick pairs, mean 0.37 px, max 2 px on the artwork's roughest
// transition, identical on all four pass variants, and the net foot drift
// is ~0 at tempos 1-4, so he cannot moonwalk at any setting. (1 px/frame is
// the same rate the roster's AN_WALK has always used, which is why that
// walker never read as skating.)
//
// ONE KNOB: WALKER_TEMPO, artwork frames per render tick. Ground speed and
// gait rate BOTH derive from it through the measured stride, so foot-plant
// sync holds at every setting, change the tempo and he walks faster, he
// never skates or moonwalks. At the walker segment's 63 ms tick (16 fps,
// affordable because a crossing dirties one panel per tick):
//   1 = walk, 16 px/s   (SHIPS, every artwork frame shown, 1 px steps)
//   2 = brisk, 32 px/s  (every 2nd frame)
//   3 = hurried, 48 px/s
//   4 = run-ish, 64 px/s
//
// The VARIANT is two bits, chosen at RUNTIME by ui.cpp's scheduler per pass:
//   bit 0  row        0 = top (TL,TR)     1 = bottom (BL,BR)
//   bit 1  direction  0 = left-to-right   1 = right-to-left (mirrored sprite)
// The renderer is a pure function of (slot, tick, variant), so the prover
// enumerates all four variants exhaustively, the randomness lives only in
// which variant the scheduler hands the snapshot, exactly the roster's
// arrangement with anim_ids.
static const uint8_t  WALKER_TEMPO   = 1;   // artwork frames per tick
static const int16_t  SR_STRIDE_PX   = 1;   // MEASURED stride, px per frame
static const int16_t  SR_SPEED       =
    (int16_t)(SR_STRIDE_PX * WALKER_TEMPO);           // ground px per tick
// One pass: the cell's x sweeps SR_PASS_X0..SR_PASS_X0+SR_PASS_SWEEP, which
// carries the ARTWORK from fully off one edge to fully off the other; then
// the one-frame bezel beat; then the same sweep on the second panel.
static const int16_t  SR_PASS_X0     = -26;
static const int16_t  SR_PASS_SWEEP  = 140;           // ends at x = 114
static const uint16_t SR_CROSS       =
    (uint16_t)(SR_PASS_SWEEP / SR_SPEED + 1);         // ticks per panel, 141
static const uint16_t SR_PASS_TICKS  = (uint16_t)(2 * SR_CROSS + 1);  // 283
static const uint8_t  SR_WALK_PASSES = 1;             // passes per reel loop
static const uint16_t SR_WALK_TICKS  =
    (uint16_t)(SR_PASS_TICKS * SR_WALK_PASSES);       // 283; ~17.8 s at 63 ms
void sr_pass_draw(GFXcanvas1 &c, uint8_t slot, uint16_t t, uint8_t variant);
// Whether this slot may be blank at this tick of the pass: true everywhere
// the walker is not fully on this panel (other row, other panel, mid-bezel,
// or a clipped entry/exit strip whose artwork may legitimately be empty).
bool sr_pass_blank_ok(uint8_t slot, uint16_t t, uint8_t variant);

// ---- the heart, phase-locked ----------------------------------------------
// One full 27-frame heart cycle, its two beats, identical on every panel at
// the same instant. The roster's scheduler deliberately never phase-locks
// four panels; the reel doing it is the contrast that reads as intentional.
static const uint16_t SR_HEART_TICKS = 27;
void sr_heart_draw(GFXcanvas1 &c, uint16_t t);     // t in 0..SR_HEART_TICKS-1

// ---- the weather drawings --------------------------------------------------
// Small, charming, 1-bit: a sun whose rays breathe, a cloud drifting across a
// peeking sun, rain falling from a cloud onto a catch line. Each stays inside
// the WX_BOX below so the weather panel's text bands can be laid out around
// it and the prover's overlap law holds by construction.
enum WxKind : uint8_t { WX_SUNNY = 0, WX_PARTLY, WX_RAIN, WX_KIND_COUNT };
static const int16_t WX_BOX_Y0 = 22;   // the drawing's rows, inclusive
static const int16_t WX_BOX_Y1 = 41;
void wx_anim_draw(GFXcanvas1 &c, uint8_t kind, uint16_t tick);

// Whether this animation is ALLOWED to draw a completely blank frame.
//
// THIS EXISTS BECAUSE OF A REAL BUG. heartFrames was declared
// [HEART_FRAME_COUNT][288] with HEART_FRAME_COUNT 28 and only 27 initialisers,
// so the compiler zero-filled the 28th: once every 28 frames, every 3.5 s  - 
// the heart rendered as nothing at all and came back the next frame. The user
// reported it as "sometimes skips a frame", which is exactly what it was.
//
// The frame tables are now unsized and counted by the compiler, and the count
// is static_asserted, so that particular hole is closed. This is the second
// lock: the prover requires every frame of every animation to light at least
// one pixel unless the animation DECLARES that it wants a blank one. Nothing
// declares it today. Getting a blank frame by accident is a failure; getting
// one on purpose costs an edit to this predicate and a sentence saying why.
bool anim_blank_ok(uint8_t id);
