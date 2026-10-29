#include "faces.h"
#include "display.h"        // for the safe-area constants only
#include <string.h>
#include <stdio.h>

ElemHook face_elem_hook = nullptr;

const char *const FX_WDAY[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char *const FX_MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Weekday from the DATE, by Sakamoto — NOT from the DS3231's day-of-week
// register. That register is just a counter the host has to set correctly and
// nothing here ever has, so trusting it would print a weekday that is right
// only by luck. Verified against `date`: 2026-08-07 = FRI both ways.
uint8_t fx_weekday(uint16_t y, uint8_t m, uint8_t d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 1 || m > 12) return 0;
  if (m < 3) y -= 1;
  return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

// The built-in GFX font is a 6x8 cell of which 5x7 is ink: one blank column on
// the right and one blank row underneath. Measuring the CELL rather than the
// INK is the classic way to end up with layouts that look off-centre and safe
// areas that are wrong by a pixel at every size.
int16_t fx_text_w(const char *s, uint8_t size) {
  int16_t n = (int16_t)strlen(s);
  return n ? (int16_t)(n * 6 * size - size) : 0;
}
int16_t fx_text_h(uint8_t size) { return (int16_t)(7 * size); }

// ---- outline glyphs -------------------------------------------------------
// "The same clock, but way less pixels in the middle." Rather than hand-draw a
// second font, text is rendered into an off-screen 1-bit canvas and HOLLOWED:
// a pixel survives only if it is lit AND at least one of its four neighbours
// is not. That leaves a 1 px trace of exactly the same typeface at exactly the
// same metrics, so every fit calculation still holds.
//
// It is also the single biggest burn-in win available. A size-7 '8' goes from
// ~800 lit pixels to ~170 — and lifetime on these panels runs as roughly the
// inverse 1.4 power of drive current, on glyphs that sit in the same place for
// years.
//
// File-scope, not a local: a 1 KB malloc and free on every repaint would
// fragment the heap on a device expected to run for months.
static GFXcanvas1 scratch(SCR_W, SCR_H);

// ---- inverted looks -------------------------------------------------------
// An inverted style is the SAME glyph work, composited as a negative inside a
// chip that hugs the ink. Two things about that are deliberate.
//
// FIRST: IT HAPPENS INSIDE THE ELEMENT, between one ELEM() and the next. The
// obvious implementation — draw the whole face, then invert the canvas at the
// end of face_render() — cannot be proven. The host prover snapshots AND
// CLEARS the canvas at every element boundary, so a post-pass would invert a
// nearly-empty canvas on the host and a full one on the device: the prover
// would be proving a different program from the one that ships. Every negative
// below is composited before the next ELEM() call, so the element the prover
// lifts out is exactly the block of pixels the panel gets.
//
// SECOND: A CHIP, NEVER THE SAFE AREA. Inverting the whole 116x52 is 73% lit,
// which on an OLED that shows the same face for years is a lifetime problem
// rather than a style choice. Hugging the ink with a few pixels of padding and
// dropping the glyph a size or two lands the inverted looks at ~28-33% —
// still over the 15% always-on budget, reported as a warning, and a trade the
// user makes by pressing MODE. Panel-level inversion (SSD1306 0xA7) stays OFF
// for the same reason: it multiplies fill factor across the entire panel.
//
// THIRD, and this one the prover found rather than the design: THE CHIP IS
// SCREENED, not solid. A solid block gets NO relief from the anti-burn-in
// shift — a pixel in its interior is lit at every offset in the envelope, so
// its duty is 1.000 and the shift buys it exactly nothing. The prover measures
// that (SHIFT RELIEF, floor 1.10x) and failed 1261 screens on the first solid
// version. Dropping one pixel in every 2x2 cell fixes it at the root: because
// the whole canvas is what shifts, the screen pattern travels with the image,
// so every pixel in the chip is dark for a quarter of the offsets. Relief
// comes out at ~1.33x, fill drops by a quarter, and at this size a 3-in-4
// screen reads as a solid lit block from anywhere but nose-to-glass.
//
// The composite is ADDITIVE — it only ever sets pixels, never clears them.
// The knockout is achieved by NOT DRAWING the glyph, exactly like the stencil
// bands above, so an inverted element cannot damage a neighbouring one in a
// way the prover structurally cannot see.
static GFXcanvas1 neg(SCR_W, SCR_H);
// Lit unless BOTH coordinates are odd: 3 pixels of every 4.
static inline bool chip_screen(int16_t x, int16_t y) {
  return !((x & 1) && (y & 1));
}
static const int16_t CHIP_PAD_X = 3;
static const int16_t CHIP_PAD_Y = 2;
// The same sideways nudge OVERLAY_NUDGE applies to SHADOW and STENCIL, needed
// here for the inverted seven-segment block: a chip is wider than the glyph it
// wraps, so it reaches further towards the corner overlay than the glyph does.
static const int16_t CHIP_NUDGE = 4;

// Inverted glyphs run smaller. A chip around a size-7 digit pair covers 52% of
// the panel; the same pair at size 5 covers 28%, and inside a lit block a
// smaller glyph reads just as far across a room.
static uint8_t inv_size(uint8_t s) {
  if (s >= 7) return 5;
  if (s >= 4) return (uint8_t)(s - 1);
  return s;
}

// ---- where a chip is allowed to reach -------------------------------------
// THE CHIP IS CLAMPED TO A WINDOW, AND THE WINDOW IS NOT ALWAYS THE SAFE AREA.
// For the six original styles it is: nothing else is on the panel, so the only
// thing a chip can run into is the edge. The ported themes changed that. Their
// chrome is drawn at NORMAL polarity — a lit rule, a lit card, a lit tick
// scale — and a plate that reached it would be lit underneath it, so the
// chrome would vanish into its own plate. On DIAL, whose entire chrome IS the
// scale, that loses the theme.
//
// So the clamp is a variable rather than the SAFE_* constants, RESET TO THE
// SAFE AREA AT THE TOP OF EVERY face_render() CALL. That reset is what keeps
// faces.cpp pure in the sense the prover needs: the same inputs draw the same
// pixels, because nothing here survives a call.
static int16_t chip_x0 = SAFE_X0, chip_y0 = SAFE_Y0;
static int16_t chip_x1 = SAFE_X1, chip_y1 = SAFE_Y1;
static void chip_clamp_reset() {
  chip_x0 = SAFE_X0; chip_y0 = SAFE_Y0;
  chip_x1 = SAFE_X1; chip_y1 = SAFE_Y1;
}

// ---- THE SCRATCH BUFFERS CARRY STATE, AND THAT STATE LEAKS ----------------
// `scratch` and `neg` are file-scope canvases — deliberately, because a 1 KB
// malloc and free on every repaint would fragment the heap on a device
// expected to run for months. The cost of that decision is that a GFXcanvas1
// REMEMBERS ITS FONT AND TEXT SIZE, so whatever the last render left set is
// what the next one starts from.
//
// That went unnoticed for as long as every renderer used the same built-in
// 5x7 font. The themed faces broke it: an INVERTED themed element draws into
// `neg` with a 46 px custom font, and the next classic inverted element —
// stack2_impl, face_words, face_shadow — calls fx_left(neg, ...) with
// setTextSize(3) and no setFont(), so the 5x7 glyph it asked for came out as
// a 138 px theme glyph smeared across the panel. Measured, not reasoned: the
// DATE panel's month rendered as a full-panel screened blob, 4328 findings.
//
// It is invisible on the device TODAY only because the MODE cycle currently
// offers no inverted stop, which is luck rather than design and is one table
// edit from being false. So every buffer this file owns is put back to the
// built-in font at the top of every render, in one place, rather than each
// drawing path being trusted to tidy up after itself.
static void face_scratch_reset() {
  scratch.setFont(nullptr); scratch.setTextSize(1); scratch.setTextWrap(false);
  neg.setFont(nullptr);     neg.setTextSize(1);     neg.setTextWrap(false);
}

// Where an inverted element draws: the negative buffer if it is inverting,
// the real canvas if it is not. One call site, so no path can forget to clear.
static GFXcanvas1 &inv_target(GFXcanvas1 &c, bool inv) {
  if (!inv) return c;
  neg.fillScreen(0);
  return neg;
}

// Composite whatever is in `neg` into `c` as a filled chip with that content
// knocked out of it. Clamped into the safe area, because a chip that hangs
// over the edge looks perfect on a bench and is clipped the moment the
// anti-burn-in shift wanders.
// `box_w`/`box_h` force the chip to a known CELL rather than to the ink. A
// seven-segment '1' lights two segments at the right-hand edge of its cell, so
// a chip fitted to its ink is a thin sliver next to the full-width chip of the
// digit beside it — "13" came out as a splinter and a block. Everything else
// wants the ink, because a chip fitted to a fixed cell would leave a lit
// margin that changes with the string.
static void emit_negative_box(GFXcanvas1 &c, bool inv, int16_t bx, int16_t by,
                              int16_t bw, int16_t bh) {
  if (!inv) return;
  int16_t lox = SCR_W, hix = -1, loy = SCR_H, hiy = -1;
  if (bw > 0) {
    lox = bx; hix = (int16_t)(bx + bw - 1);
    loy = by; hiy = (int16_t)(by + bh - 1);
  } else {
    for (int16_t y = 0; y < SCR_H; y++)
      for (int16_t x = 0; x < SCR_W; x++)
        if (neg.getPixel(x, y)) {
          if (x < lox) lox = x;
          if (x > hix) hix = x;
          if (y < loy) loy = y;
          if (y > hiy) hiy = y;
        }
  }
  if (hix < lox) return;                    // nothing drawn, nothing to invert
  int16_t x0 = (int16_t)(lox - CHIP_PAD_X), x1 = (int16_t)(hix + CHIP_PAD_X);
  int16_t y0 = (int16_t)(loy - CHIP_PAD_Y), y1 = (int16_t)(hiy + CHIP_PAD_Y);
  if (x0 < chip_x0) x0 = chip_x0;
  if (y0 < chip_y0) y0 = chip_y0;
  if (x1 > chip_x1) x1 = chip_x1;
  if (y1 > chip_y1) y1 = chip_y1;
  // THE KNOCKOUT IS DILATED BY ONE PIXEL. A hollow glyph is a 1 px trace, and
  // one dark pixel inside a 3-in-4 screen is indistinguishable from the screen
  // itself — inverted STENCIL and inverted FILLED came out as texture rather
  // than as digits. Widening the hole to 3 px makes them read, and it takes
  // lit pixels OFF the panel rather than adding them.
  for (int16_t y = y0; y <= y1; y++)
    for (int16_t x = x0; x <= x1; x++) {
      if (!chip_screen(x, y)) continue;
      if (neg.getPixel(x, y)) continue;
      if (x > 0          && neg.getPixel((int16_t)(x - 1), y)) continue;
      if (x < SCR_W - 1  && neg.getPixel((int16_t)(x + 1), y)) continue;
      if (y > 0          && neg.getPixel(x, (int16_t)(y - 1))) continue;
      if (y < SCR_H - 1  && neg.getPixel(x, (int16_t)(y + 1))) continue;
      c.drawPixel(x, y, 1);
    }
}

static void emit_negative(GFXcanvas1 &c, bool inv) {
  emit_negative_box(c, inv, 0, 0, 0, 0);
}

// `band_period`/`band_h` cut horizontal slots out of the glyph for S_STENCIL.
// The bands are SKIPPED DURING THE COPY, never erased afterwards. Erasing
// would also clear whatever else already occupied those rows, and because the
// host prover clears the canvas between elements, an erasure that damages a
// neighbouring element is the one class of layout bug it structurally cannot
// see. Skipping keeps the renderer additive, which is what the proof assumes.
static void outline_masked(GFXcanvas1 &c, const char *s, uint8_t size,
                           int16_t x, int16_t y,
                           int16_t band_period, int16_t band_h) {
  scratch.fillScreen(0);
  scratch.setTextSize(size);
  scratch.setTextColor(1);
  scratch.setTextWrap(false);
  scratch.setCursor(x, y);
  scratch.print(s);

  int16_t x0 = x - 1, y0 = y - 1;
  int16_t x1 = x + fx_text_w(s, size) + 1, y1 = y + fx_text_h(size) + 1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SCR_W) x1 = SCR_W;
  if (y1 > SCR_H) y1 = SCR_H;

  for (int16_t py = y0; py < y1; py++) {
    if (band_period > 0) {
      int16_t r = py - y;
      // Offset by half a period so a band never lands on the glyph's top or
      // bottom edge, which would just look like a clipped digit.
      if (r >= 0 && ((r + band_period / 2) % band_period) < band_h) continue;
    }
    for (int16_t px = x0; px < x1; px++) {
      if (!scratch.getPixel(px, py)) continue;
      // Anything off the canvas counts as unlit, so a glyph touching a border
      // still gets an outline rather than a solid edge.
      bool interior = px > 0 && px < SCR_W - 1 && py > 0 && py < SCR_H - 1 &&
                      scratch.getPixel(px - 1, py) && scratch.getPixel(px + 1, py) &&
                      scratch.getPixel(px, py - 1) && scratch.getPixel(px, py + 1);
      if (!interior) c.drawPixel(px, py, 1);
    }
  }
}

void fx_outline_text(GFXcanvas1 &c, const char *s, uint8_t size,
                     int16_t x, int16_t y) {
  outline_masked(c, s, size, x, y, 0, 0);
}

void fx_left(GFXcanvas1 &c, const char *s, uint8_t size, int16_t x, int16_t y,
             bool hollow) {
  // Size 1 is never hollowed. A 5x7 glyph with its interior removed is
  // scattered dots, not a letter — and at that size the lit area it would save
  // is a rounding error anyway.
  if (hollow && size >= 2) { fx_outline_text(c, s, size, x, y); return; }
  c.setTextSize(size);
  c.setTextColor(1);
  c.setTextWrap(false);
  c.setCursor(x, y);
  c.print(s);
}

// OPTICAL CENTRING — the ink is centred, not the advance box.
//
// A GFX classic glyph occupies a 6*size cell but does not fill it, and how much
// whitespace sits on each side is PER CHARACTER. '1' is the extreme case: it is
// drawn towards the right of its cell, so "12" centred on its advance width
// puts the visible ink 3 px right of the panel centre — measured, not guessed
// (tools/pageshot on CLOCK v5 slot0: ink 32..101, centre 66.5 against 63.5).
// Every string built from digits of differing widths lands somewhere slightly
// different, which reads as "the numbers are not quite centred" and is exactly
// what it is.
//
// This was invisible while the shift envelope was +/-2, because the image was
// pinned in one corner and being consistently off looks like being deliberate.
// At the +/-6 the burn-in work needs, it wanders, and a static offset on top of
// a moving image is what you notice.
//
// So: lay the string out, measure where the INK actually lands, and correct.
// One extra render into `scratch` per centred string (~0.2 ms) buys a layout
// whose centre does not depend on which digits happen to be showing.
static int16_t ink_dx(const char *s, uint8_t size, int16_t x, int16_t y) {
  scratch.fillScreen(0);
  scratch.setTextSize(size);
  scratch.setTextColor(1);
  scratch.setTextWrap(false);
  scratch.setCursor(x, y);
  scratch.print(s);

  int16_t lo = SCR_W, hi = -1;
  int16_t y0 = y < 0 ? 0 : y;
  int16_t y1 = (int16_t)(y + fx_text_h(size));
  if (y1 > SCR_H) y1 = SCR_H;
  for (int16_t py = y0; py < y1; py++)
    for (int16_t px = 0; px < SCR_W; px++)
      if (scratch.getPixel(px, py)) {
        if (px < lo) lo = px;
        if (px > hi) hi = px;
      }
  if (hi < lo) return 0;                       // nothing drawn: nothing to fix
  // Where the ink's midpoint should be, versus where it is. Rounded toward
  // zero so a half-pixel error never becomes a one-pixel jitter between values.
  int16_t want = (int16_t)(SAFE_X0 + SAFE_X1);  // 2 * centre, kept in integers
  return (int16_t)((want - (lo + hi)) / 2);
}

// Centred in the SAFE AREA, not on the panel. Those differ by nothing at all
// when the safe margins are equal — which they are — but writing it this way
// means a future asymmetric margin stays correct for free.
void fx_center(GFXcanvas1 &c, const char *s, uint8_t size, int16_t dy,
               bool hollow) {
  int16_t w = fx_text_w(s, size), h = fx_text_h(size);
  int16_t x = SAFE_X0 + (SAFE_W - w) / 2;
  int16_t y = SAFE_Y0 + (SAFE_H - h) / 2 + dy;
  x = (int16_t)(x + ink_dx(s, size, x, y));
  fx_left(c, s, size, x, y, hollow);
}

// Largest size at which the string still fits the safe width. Hand-picked
// sizes are how a layout ends up fine for "23" and clipped for "-20.0".
uint8_t fx_fit(const char *s, uint8_t want, int16_t avail_w) {
  while (want > 1 && fx_text_w(s, want) > avail_w) want--;
  return want;
}

// Two lines centred AS A BLOCK. The alternative — centring each line and then
// nudging it with a hand-tuned dy — is what put the humidity readout a pixel
// above the safe area and the date a pixel below it. Both were invisible on a
// bench and would have clipped the moment the burn-in shift wandered.
static void stack2_impl(GFXcanvas1 &c, const char *a, uint8_t sa, const char *b,
                   uint8_t sb, int16_t gap, bool hollow,
                   const char *tag_a, const char *tag_b, bool inv) {
  if (inv) {
    // Smaller glyphs, and a wider gap. Two chips each padded by CHIP_PAD_Y
    // would otherwise meet in the middle of a two-line date, which the prover
    // would report as an overlap — correctly, because on the panel it is one
    // merged block and not two lines.
    sa = inv_size(sa);
    sb = inv_size(sb);
    gap = (int16_t)(gap + 2 * CHIP_PAD_Y + 1);
    hollow = !hollow;      // a solid knockout inside a lit chip, see draw_text_styled
  }
  int16_t ha = fx_text_h(sa), hb = fx_text_h(sb);
  int16_t y0 = SAFE_Y0 + (SAFE_H - (ha + gap + hb)) / 2;
  int16_t y1 = (int16_t)(y0 + ha + gap);
  // Same optical correction as fx_center, applied per LINE. Two stacked lines
  // of different digits would otherwise sit a pixel or two apart horizontally,
  // which on a two-line date reads as the block leaning.
  int16_t xa = SAFE_X0 + (SAFE_W - fx_text_w(a, sa)) / 2;
  int16_t xb = SAFE_X0 + (SAFE_W - fx_text_w(b, sb)) / 2;
  xa = (int16_t)(xa + ink_dx(a, sa, xa, y0));
  xb = (int16_t)(xb + ink_dx(b, sb, xb, y1));
  ELEM(tag_a);
  fx_left(inv_target(c, inv), a, sa, xa, y0, hollow);
  emit_negative(c, inv);
  ELEM(tag_b);
  fx_left(inv_target(c, inv), b, sb, xb, y1, hollow);
  emit_negative(c, inv);
}

void stack2(GFXcanvas1 &c, const char *a, uint8_t sa, const char *b,
            uint8_t sb, int16_t gap, bool hollow,
            const char *tag_a, const char *tag_b) {
  stack2_impl(c, a, sa, b, sb, gap, hollow, tag_a, tag_b, false);
}

// ---- seven segment --------------------------------------------------------
// Drawn from rectangles rather than a font, so it scales to whatever the safe
// area allows instead of whatever sizes a bitmap font happens to have.
static const uint8_t SEG_MASK[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66,
                                     0x6D, 0x7D, 0x07, 0x7F, 0x6F};

// OUTLINE ONLY, and deliberately. `hollow` is derived as (style != S_FILLED),
// so for S_SEG it is always true — the filled arm this used to carry could
// never execute. Keeping it would have been dead code pretending to be a
// feature. It is also the right call on its own merits: filled segments run
// well over the 25% fill-factor budget the layout prover enforces, and fill
// factor is what ages these panels.
static void seg_digit(GFXcanvas1 &c, uint8_t v, int16_t x, int16_t y,
                      int16_t w, int16_t h, int16_t t) {
  if (v > 9) return;
  uint8_t m = SEG_MASK[v];
  int16_t hh = h / 2;
  struct S { int16_t x, y, w, h; };
  // The casts are not decoration: int16_t arithmetic promotes to int, and a
  // braced initialiser narrowing back to int16_t is a hard error under the
  // host prover's warning settings. Being explicit keeps one source compiling
  // for both targets.
  const int16_t xr = (int16_t)(x + w - t);
  const S seg[7] = {
    {x,  y,                            w, t},     // a  top
    {xr, y,                            t, hh},    // b  upper right
    {xr, (int16_t)(y + hh),            t, hh},    // c  lower right
    {x,  (int16_t)(y + h - t),         w, t},     // d  bottom
    {x,  (int16_t)(y + hh),            t, hh},    // e  lower left
    {x,  y,                            t, hh},    // f  upper left
    {x,  (int16_t)(y + hh - t / 2),    w, t},     // g  middle
  };
  for (uint8_t i = 0; i < 7; i++)
    if (m & (1 << i)) c.drawRect(seg[i].x, seg[i].y, seg[i].w, seg[i].h, 1);
}

// 2W + GAP must fit the 116 px safe width: 2*50 + 8 = 108. H is the full 52.
static void face_seg(GFXcanvas1 &c, uint8_t val, bool two, bool has_overlay,
                     bool inv) {
  // The right-hand digit runs to x=117, which is inside the overlay's corner.
  // When there is an overlay, shorten the digits so they finish above it
  // rather than narrowing them, because height is what makes them readable.
  //
  // INVERTED runs a smaller digit on a wider pitch. A chip round the shipping
  // 50x52 digit is two thirds of the panel; and the two chips have to clear
  // each other by more than CHIP_PAD_X or they merge into one block, which is
  // both an overlap the prover fails and a pair of digits nobody can read.
  const int16_t W   = inv ? 28 : 50;
  const int16_t T   = inv ?  5 :  6;
  const int16_t GAP = inv ? 10 :  8;
  const int16_t H   = inv ? 34 : (has_overlay ? (int16_t)(SAFE_H - 10) : SAFE_H);
  int16_t total = two ? (2 * W + GAP) : W;
  int16_t x = SAFE_X0 + (SAFE_W - total) / 2;
  // Inverted blocks are shorter than the safe height, so they get centred —
  // except when the corner overlay is there, where they sit high and shift
  // left for the same reason every other style does.
  int16_t y = SAFE_Y0;
  if (inv) {
    y = has_overlay ? SAFE_Y0 : (int16_t)(SAFE_Y0 + (SAFE_H - H) / 2);
    if (has_overlay) x = (int16_t)(x - CHIP_NUDGE);
  }
  auto digit = [&](uint8_t v, int16_t dx, const char *tag) {
    ELEM(tag);
    seg_digit(inv_target(c, inv), v, dx, y, W, H, T);
    emit_negative_box(c, inv, dx, y, W, H);
  };
  if (two) {
    digit((uint8_t)(val / 10), x, "seg-tens");
    digit((uint8_t)(val % 10), (int16_t)(x + W + GAP), "seg-ones");
  } else {
    digit((uint8_t)(val % 10), x, "seg-ones");
  }
}

// ---- bars -----------------------------------------------------------------
// 12 columns of 7 px with 2 px gaps is 106 px, inside the 116 px safe width.
// The obvious 8 px column comes to 118 and overruns it by two — which is the
// kind of thing that looks fine on a bench and clips once the burn-in shift
// wanders, and is exactly why the prover exists.
// ---- shadow ---------------------------------------------------------------
// The same hollow glyph drawn twice, offset diagonally. Reads as depth from
// across the room and still costs well under a filled glyph.
//
// ONE ELEMENT, not two. The front and back copies genuinely do share pixels
// wherever a stroke runs diagonally, and that is intended — declaring them
// separately would make the prover fail a build over a collision that is the
// whole point of the style.
static const int16_t SHADOW_OFF = 3;

static void face_shadow(GFXcanvas1 &c, const char *s, uint8_t size, int16_t dx,
                        bool inv) {
  if (inv) size = inv_size(size);
  int16_t w = fx_text_w(s, size), h = fx_text_h(size);
  // Centre the PAIR. Centring the front copy and letting the shadow hang off
  // it puts the visual block half the offset off-centre.
  int16_t x = SAFE_X0 + (SAFE_W - (w + SHADOW_OFF)) / 2 + dx;
  int16_t y = SAFE_Y0 + (SAFE_H - (h + SHADOW_OFF)) / 2;
  if (x < SAFE_X0) x = SAFE_X0;
  ELEM("shadow");
  GFXcanvas1 &t = inv_target(c, inv);
  fx_outline_text(t, s, size, x + SHADOW_OFF, y + SHADOW_OFF);
  fx_outline_text(t, s, size, x, y);
  emit_negative(c, inv);
}

// ---- words ----------------------------------------------------------------
static const char *const ONES[20] = {
  "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT",
  "NINE", "TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN",
  "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN"};
static const char *const TENS[6] = {"", "", "TWENTY", "THIRTY", "FORTY", "FIFTY"};

// Longest string is SEVENTEEN at 9 characters: 9*12-2 = 106 px at size 2,
// inside the 116 px safe width.
static void face_words(GFXcanvas1 &c, uint8_t v, bool hollow, bool inv) {
  if (inv) hollow = !hollow;
  char l1[12] = "", l2[12] = "";
  if (v < 20) snprintf(l1, sizeof(l1), "%s", ONES[v]);
  else {
    uint8_t tens = (uint8_t)(v / 10);
    snprintf(l1, sizeof(l1), "%s", tens < 6 ? TENS[tens] : "");
    if (v % 10) snprintf(l2, sizeof(l2), "%s", ONES[v % 10]);
  }
  auto put = [&](const char *t, int16_t y, const char *tag) {
    if (!*t) return;
    ELEM(tag);
    fx_left(inv_target(c, inv), t, 2, SAFE_X0 + (SAFE_W - fx_text_w(t, 2)) / 2,
            y, hollow);
    emit_negative(c, inv);
  };
  if (*l2) { put(l1, SAFE_Y0 + 8, "word-1"); put(l2, SAFE_Y0 + 30, "word-2"); }
  else     { put(l1, SAFE_Y0 + (SAFE_H - fx_text_h(2)) / 2, "word-1"); }
}

// ---- stencil --------------------------------------------------------------
// A hollow glyph with horizontal slots cut through it, like a cut stencil
// plate. Lowest-fill style of the six, so it is also the kindest to the panel.
static const int16_t STENCIL_PERIOD = 16;
static const int16_t STENCIL_BAND   = 3;

static void face_stencil(GFXcanvas1 &c, const char *s, uint8_t size, int16_t dx,
                         bool inv) {
  if (inv) size = inv_size(size);
  int16_t w = fx_text_w(s, size), h = fx_text_h(size);
  int16_t x = SAFE_X0 + (SAFE_W - w) / 2 + dx;
  int16_t y = SAFE_Y0 + (SAFE_H - h) / 2;
  if (x < SAFE_X0) x = SAFE_X0;
  ELEM("stencil");
  outline_masked(inv_target(c, inv), s, size, x, y, STENCIL_PERIOD, STENCIL_BAND);
  emit_negative(c, inv);
}

// ===========================================================================
// THE FIVE PORTED THEMES
// ===========================================================================
// Ported from firmware D (screens/theme.h, which is the design document for
// all of this and worth reading before touching anything below).
//
// WHAT A THEME IS. Not a glyph trick — a TYPE SYSTEM. Five tiers of one real
// outline face, generated by D's fontgen.py, which SOLVES FOR THE PIXEL HEIGHT
// rather than taking a point size, so "46" means the same thing in every
// family and the themes are comparable rather than merely all present:
//
//   big   46-48 px  digits only (0-9)   hour, minute
//   bigo  46-48 px  the true-outline variant of big
//   lg    28 px     0x20-0x5A           weekday, the day number
//   md    20 px     0x20-0x5A           the inverted weekday
//   sm    10 px     0x20-0x5A           the month label, the corner overlay
//
// Plus the CHROME the family wears. Porting a theme means porting both; a
// lookalike drawn with the built-in 5x7 font would be neither.
//
// ---------------------------------------------------------------------------
// ⛔ THE ARCHITECTURAL TRANSLATION — FIRMWARE C HAS NO SHEET ⛔
// ---------------------------------------------------------------------------
// Firmware D draws in SHEET coordinates: the four panels are windows onto one
// 339x295 canvas, so a rule can be one line crossing the bezel and a tick
// scale can be one instrument spanning two panels. FIRMWARE C HAS NO SUCH
// THING. Each panel is an independent 128x64 GFXcanvas1 and this file draws
// one widget per panel knowing nothing whatever about its neighbours — it is
// not even told which slot it is. So:
//
//   CH_CARD and CH_BRACKET port EXACTLY. They were already per-window in D;
//   D drew them at win_x0..win_x1 of one window, which is this panel's safe
//   area with the theme inset applied. Same pixels.
//
//   CH_RULE ports EXACTLY TOO, and that is arithmetic rather than luck. D's
//   rule looks sheet-wide but sheet_hline() clips every span to the UNION OF
//   THE WINDOW RECTANGLES, so what actually reaches a panel is a hairline from
//   that window's own left inset to its own right inset. That is what is drawn
//   here. Both panels of a row still put it on the same canvas row, so on the
//   real glass the two segments are collinear and the eye completes the line
//   across the bezel exactly as it did in D. Nothing is lost.
//
//   CH_RING IS THE ONE THAT CANNOT PORT, AND HERE IS EXACTLY WHAT CHANGED.
//   In D the tick scale is ONE instrument laid across the whole sheet: 25
//   ticks from sheet x 6 to x 338, filled up to the current minute. Seven of
//   them land in the bezel and are never drawn, so the LEFT panel shows the
//   first nine ticks (roughly minutes 0-22) and the RIGHT panel the last nine
//   (roughly 38-60), and the fill sweeps across the whole sheet once an hour.
//   A panel is a WINDOW ONTO a scale that is bigger than it.
//
//   C cannot express that. This file is not told which panel it is drawing, so
//   it cannot know whether to show the low ticks or the high ones, and there
//   is no sheet to be a window onto. THE ADAPTATION: every panel carries its
//   OWN COMPLETE minute scale — nine ticks across this panel's safe width, at
//   the same visual density D's sheet scale had once the bezel ate two thirds
//   of it (~14 px pitch), filled to the same fraction of the hour.
//
//   WHAT IS LOST: the one-instrument illusion. In D the fill crossed the bezel
//   and swept the sheet once per hour; here all four panels show the same
//   scale filling in parallel. The INFORMATION is identical — how far through
//   the hour we are, to about seven minutes — and so is the density and the
//   3 px/2 px long-short tick treatment. What is gone is the statement that
//   the four panels are one surface. That statement belongs to a sheet, and
//   faking it here would mean inventing a slot number this file is not given.
// ---------------------------------------------------------------------------

#include "fonts/F_OUTFIT_B.h"
#include "fonts/F_OUTFIT_BO.h"
#include "fonts/F_OUTFIT_L.h"
#include "fonts/F_OUTFIT_M.h"
#include "fonts/F_OUTFIT_S.h"
#include "fonts/F_IOS_B.h"
#include "fonts/F_IOS_BO.h"
#include "fonts/F_IOS_L.h"
#include "fonts/F_IOS_M.h"
#include "fonts/F_IOS_S.h"
#include "fonts/F_QUICK_B.h"
#include "fonts/F_QUICK_BO.h"
#include "fonts/F_QUICK_L.h"
#include "fonts/F_QUICK_M.h"
#include "fonts/F_QUICK_S.h"
#include "fonts/F_BALOO_B.h"
#include "fonts/F_BALOO_BO.h"
#include "fonts/F_BALOO_L.h"
#include "fonts/F_BALOO_M.h"
#include "fonts/F_BALOO_S.h"
#include "fonts/F_FREDOKA_B.h"
#include "fonts/F_FREDOKA_BO.h"
#include "fonts/F_FREDOKA_L.h"
#include "fonts/F_FREDOKA_M.h"
#include "fonts/F_FREDOKA_S.h"

// The chrome a theme wears. D's CH_* with the same meanings; the sheet-wide /
// per-window distinction it carried is gone because C has no sheet, and what
// replaced it is documented above.
enum FaceChrome : uint8_t {
  CH_PLAIN = 0,   // nothing at all
  CH_CARD,        // a 1 px rounded card on the panel's safe boundary
  CH_RULE,        // a hairline on the last safe row
  CH_BRACKET,     // registration corner ticks
  CH_RING,        // a tick scale on the last safe row, filled to the minute
};

// ---- WHAT EACH LOOK COSTS THE PANEL, MEASURED ON FIRMWARE C ---------------
// tools/layoutcheck prints this table every run ("MODE cycle fill"), worst
// panel of the four over every value the widget can hold, against the 15%
// Wear OS WO-P7 always-on budget. THESE ARE THE C NUMBERS, not D's — D's
// theme.cpp quotes 23.5% for filled ROUNDEL and 26.0% for filled CAPSULE and
// this build measures 29.8% and 29.2% for the same families at the same
// generated sizes. The cause of the gap has NOT been established (D measured
// through its own design-pass gate, on a sheet, with softkeys); what is
// recorded here is what this prover measures on this renderer, because that is
// the thing that ships.
//
//     stop  look             fill      stop  look             fill
//       1   OUTLINE           8.1%       8   OUTLINE INV      13.8%
//       2   FILLED           23.2%       9   FILLED INV       14.9%
//       3   DIAL             21.3%      10   DIAL INV         10.4%
//       4   DATASHEET        20.1%      11   DATASHEET INV     8.5%
//       5   QUIET            18.4%      12   QUIET INV        11.4%
//       6   ROUNDEL          29.8%      13   ROUNDEL INV       9.2%
//       7   CAPSULE          29.2%      14   CAPSULE INV      12.8%
//
// ONLY STOP 1 IS HELD TO THE BUDGET, and that is the existing policy rather
// than a concession made for this port: OUTLINE is the power-on default and
// the only look the device ages on unless someone presses MODE. Every other
// stop is measured, reported and left to the user.
//
// ROUNDEL AND CAPSULE ARE THE TWO THAT COST THE MOST, and they are the same
// two D singled out — its theme.h answers them with a CONTRAST CAP
// (theme_wants_contrast_cap) rather than with a smaller glyph, on the argument
// that total emitted light is what ages the stack. C has no such cap and the
// panel-driving code that would carry one is display.cpp, which is out of
// scope for this port. IT IS NOT DONE HERE AND SHOULD BE CONSIDERED.
// Every inverted stop is comfortably inside budget, which was not obvious in
// advance: the chip is fitted to a tier-smaller glyph and screened 3-in-4, so
// the negatives cost the panel LESS than the positives they mirror.
//
// A THEME IS A TABLE ROW. Adding one must not touch a layout function below,
// and none of them do: every themed layout asks the table for a tier and a
// chrome kind and draws the same geometry whichever answer it gets. That is
// what keeps five looks from becoming five layout engines.
struct FaceTheme {
  const GFXfont *big, *lg, *md, *sm, *bigo;
  uint8_t chrome;
  int16_t track;
};
static const FaceTheme FTHEME[S_THEME_N] = {
  // DIAL — Outfit numerals over a tick scale. The only theme whose chrome is
  // also a readout. Digits are 48 px of the 52-row safe area.
  {&F_OUTFIT_B,  &F_OUTFIT_L,  &F_OUTFIT_M,  &F_OUTFIT_S,  &F_OUTFIT_BO,
   CH_RING, 2},
  // DATASHEET — Iosevka mono with registration corner ticks. An instrument,
  // not an ornament, and the register this project already speaks.
  {&F_IOS_B,     &F_IOS_L,     &F_IOS_M,     &F_IOS_S,     &F_IOS_BO,
   CH_BRACKET, 2},
  // QUIET — Quicksand geometric, one hairline, the widest tracking in the set.
  {&F_QUICK_B,   &F_QUICK_L,   &F_QUICK_M,   &F_QUICK_S,   &F_QUICK_BO,
   CH_RULE, 3},
  // ROUNDEL — Baloo 2 ExtraBold on bare glass. The fattest, most
  // legible-across-a-room numerals here, and no chrome: at this weight any
  // rule is noise.
  {&F_BALOO_B,   &F_BALOO_L,   &F_BALOO_M,   &F_BALOO_S,   &F_BALOO_BO,
   CH_PLAIN, 1},
  // CAPSULE — Fredoka inside a 1 px rounded card. Each panel reads as a
  // physical tile: the one theme that leans on the aperture instead of
  // ignoring it.
  {&F_FREDOKA_B, &F_FREDOKA_L, &F_FREDOKA_M, &F_FREDOKA_S, &F_FREDOKA_BO,
   CH_CARD, 1},
};

// Is this a themed style, and which one. Out-of-range returns null rather than
// indexing off the end: this is fed from an EEPROM byte, and settings_sanitize
// is belt while this is braces.
static const FaceTheme *theme_of(uint8_t s) {
  return (s >= S_THEME_FIRST && s < S_THEME_FIRST + S_THEME_N)
             ? &FTHEME[s - S_THEME_FIRST] : nullptr;
}

// ---- per-panel text, in PANEL coordinates ---------------------------------
// D's sheet_* text primitives with the Pen taken out. The Pen existed to
// translate sheet coordinates to panel ones; there is nothing to translate
// here, so what is left is the measurement and centring logic — which is the
// part that was actually load-bearing.
//
// LAYOUT POSITIONS INK BOXES, NEVER BASELINES. A baseline-specified layout
// changes meaning the moment a font with different metrics is dropped in,
// which is exactly what a five-family table does every time MODE is pressed.
struct GfxTB { int16_t dx, dy, w, h; };

static const GFXglyph *gfx_glyph(const GFXfont *f, char ch) {
  if (!f) return nullptr;
  if ((uint8_t)ch < f->first || (uint8_t)ch > f->last) return nullptr;
  return &f->glyph[(uint8_t)ch - f->first];
}

static GfxTB gfx_tb(GFXcanvas1 &c, const GFXfont *f, const char *s) {
  c.setFont(f);
  c.setTextSize(1);
  c.setTextWrap(false);
  int16_t x1, y1;
  uint16_t w, h;
  c.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return GfxTB{(int16_t)-x1, (int16_t)-y1, (int16_t)w, (int16_t)h};
}

// Draw with the string's INK BOX top-left at (x, y).
static void gfx_at(GFXcanvas1 &c, const GFXfont *f, const char *s,
                   int16_t x, int16_t y) {
  if (!s || !*s) return;
  GfxTB t = gfx_tb(c, f, s);
  c.setFont(f);
  c.setTextSize(1);
  c.setTextColor(1);
  c.setTextWrap(false);
  c.setCursor((int16_t)(x + t.dx), (int16_t)(y + t.dy));
  c.print(s);
}

static int16_t gfx_adv(const GFXfont *f, char ch) {
  const GFXglyph *g = gfx_glyph(f, ch);
  return g ? (int16_t)g->xAdvance : 0;
}

// Letter-spaced width. Tracking is most of what makes a small all-caps label
// look deliberate rather than cramped, and GFX has no notion of it.
//
// A SPACE HAS NO INK, SO IT CANNOT BE MEASURED BY ITS INK BOX — that gives it
// width zero and every tracked label comes out as one run-together word. Blanks
// advance by the FONT's xAdvance; everything else by its ink. (D paid a whole
// render pass to find this; it is carried over rather than rediscovered.)
static int16_t gfx_track_w(GFXcanvas1 &c, const GFXfont *f, const char *s,
                           int16_t tr) {
  if (!s || !*s) return 0;
  int16_t tot = 0;
  for (const char *q = s; *q; q++) {
    char b[2] = {*q, 0};
    int16_t w = (*q == ' ') ? gfx_adv(f, ' ') : gfx_tb(c, f, b).w;
    tot = (int16_t)(tot + w + tr);
  }
  return (int16_t)(tot - tr);
}

// ---- optical centring, and the two defects it removes ----------------------
// Both are D's findings and both are free here:
//
//   1. HORIZONTAL JITTER. Centring the ink box of "10" and then of "11" moves
//      the pair, because '1' carries far more side bearing than '0'. The clock
//      twitches on the minute, every minute, independently of any burn-in
//      wander. gfx_cells() gives every digit an identical cell equal to the
//      widest digit's xAdvance, so the pair occupies the same columns for all
//      100 values it can hold. (This is the GFX-font analogue of ink_dx()
//      above, which does the same job for the built-in 5x7 font.)
//
//   2. VERTICAL BOUNCE. Round glyphs overshoot the cap line; '8' is taller
//      than '1' in every one of these faces, so centring each string on its
//      own ink box hangs "10" and "42" at different heights. One reference
//      glyph, one baseline, every string on the panel shares it.
static int16_t gfx_digit_cell(const GFXfont *f) {
  int16_t cw = 0;
  for (char d = '0'; d <= '9'; d++) {
    const GFXglyph *g = gfx_glyph(f, d);
    if (g && (int16_t)g->xAdvance > cw) cw = (int16_t)g->xAdvance;
  }
  return cw;
}
static int16_t gfx_ref_baseline(const GFXfont *f, char ref, int16_t cy) {
  const GFXglyph *g = gfx_glyph(f, ref);
  if (!g) return cy;
  return (int16_t)(cy - (int16_t)g->height / 2 - (int16_t)g->yOffset);
}
// The width a fixed-cell numeral run occupies.
static int16_t gfx_cells_w(const GFXfont *f, const char *s) {
  return (int16_t)(gfx_digit_cell(f) * (int16_t)strlen(s));
}
static void gfx_cells(GFXcanvas1 &c, const GFXfont *f, const char *s,
                      int16_t cx, int16_t cy) {
  if (!s || !*s) return;
  const int16_t cw = gfx_digit_cell(f);
  const int n = (int)strlen(s);
  const int16_t base = gfx_ref_baseline(f, '8', cy);
  int16_t x = (int16_t)(cx - (int16_t)(cw * n) / 2);
  c.setFont(f);
  // setTextSize(1) IS NOT DECORATION. GFXcanvas1 keeps the text size across
  // calls and the classic paths in this file set it to 7; a custom font drawn
  // at size 7 is a 336 px glyph, which the prover reported as 47% fill and
  // 572 px outside the safe area rather than as anything that looked like a
  // font bug. Every gfx_* entry point sets it.
  c.setTextSize(1);
  c.setTextColor(1);
  c.setTextWrap(false);
  for (int i = 0; i < n; i++) {
    const GFXglyph *g = gfx_glyph(f, s[i]);
    if (!g) { x = (int16_t)(x + cw); continue; }
    // The glyph is centred IN its cell, so a narrow '1' sits where a '0' would
    // and the pair never re-flows.
    c.setCursor((int16_t)(x + (cw - (int16_t)g->xAdvance) / 2), base);
    c.write((uint8_t)s[i]);
    x = (int16_t)(x + cw);
  }
}
// Tracked caps on a shared CAP baseline rather than on their own ink box.
static void gfx_trk_cap(GFXcanvas1 &c, const GFXfont *f, const char *s,
                        int16_t tr, int16_t cx, int16_t cy) {
  if (!s || !*s) return;
  const int16_t base = gfx_ref_baseline(f, 'H', cy);
  int16_t x = (int16_t)(cx - gfx_track_w(c, f, s, tr) / 2);
  c.setFont(f);
  c.setTextSize(1);
  c.setTextColor(1);
  c.setTextWrap(false);
  for (const char *q = s; *q; q++) {
    if (*q == ' ') { x = (int16_t)(x + gfx_adv(f, ' ') + tr); continue; }
    char b[2] = {*q, 0};
    GfxTB t = gfx_tb(c, f, b);
    c.setFont(f);
    c.setTextSize(1);
    c.setTextColor(1);
    c.setTextWrap(false);
    c.setCursor((int16_t)(x + t.dx), base);
    c.write((uint8_t)*q);
    x = (int16_t)(x + t.w + tr);
  }
}

// ---- the theme's geometry, RE-EXPRESSED AGAINST C's PANEL -----------------
// D's theme_inset / theme_cx0..cy1 / theme_availw are stated against a sheet
// WINDOW. A window IS the safe area of one panel — win_x0(col) is
// SHEET_COLX[col] + SAFE_X0 — so with the sheet origin dropped these become
// the safe area shrunk by the same insets, which is what is written here.
//
// A WINDOW'S CHROME LIVES ON THE WINDOW'S OWN EDGE, so content that also runs
// to that edge collides with it. CAPSULE's card is a rectangle drawn AT the
// boundary and DATASHEET's registration marks are 7 px arms in its corners.
// A theme that wears chrome therefore declares how much room it needs, and
// everything that could touch a boundary is placed against the INSET panel.
// Themes with no boundary chrome keep a 2 px courtesy margin and nothing more.
static int16_t th_inset_x(const FaceTheme &t) {
  switch (t.chrome) {
    case CH_CARD:    return 6;   // clears the radius-12 arc at every row
    case CH_BRACKET: return 8;   // the registration arms are 7 px
    default:         return 2;
  }
}
static int16_t th_inset_y(const FaceTheme &t) {
  return t.chrome == CH_CARD ? (int16_t)3 : (int16_t)0;
}

// ---- the vertical budget, which is EXACTLY spent --------------------------
// VERIFIED AGAINST THE SHIPPED FONT HEADERS, not taken from D's table — D's
// theme.h says QUIET is 50 px and the generated F_QUICK_B's '8' is 48. The
// measured heights of the `big` tier are:
//
//   theme       digits   chrome   gap   total   of the 52-row safe area
//   DIAL          48       3       1      52    exactly spent
//   QUIET         48       1       1      50
//   DATASHEET     46       0       0      46
//   ROUNDEL       46       0       0      46
//   CAPSULE       46       0       0      46
//
// So every family fits, and DIAL spends the area to the last row — which is
// why its tick scale is 3 px rather than 4 and why the rule sits ON the last
// safe row rather than one above it.
static int16_t th_chrome_h(const FaceTheme &t) {
  switch (t.chrome) {
    case CH_RING: return 3;
    case CH_RULE: return 1;
    default:      return 0;
  }
}
static const int16_t TH_RULE_Y = SAFE_Y1;    // the LAST safe row, not the one above
// The numerals are centred in whatever is left ABOVE the chrome, DERIVED from
// the measured glyph rather than from a hand-tuned drop — a constant drop is
// what put QUIET's tall digits a row outside the safe area in D.
static int16_t th_clock_cy(const FaceTheme &t) {
  const int16_t ch = th_chrome_h(t);
  // Reserve the chrome and one blank row above it. One blank row is what stops
  // the digits and the scale reading as a single smudged object on a 128x64
  // panel, and one is all the budget affords.
  const int16_t bot = (int16_t)(TH_RULE_Y - ch - (ch ? 1 : 0));
  return (int16_t)((SAFE_Y0 + bot + 1) / 2);
}

// ---- chrome ---------------------------------------------------------------
// Its own ELEM(), always, so the prover can see it collide with the content.
// Drawn at NORMAL polarity even on an inverted look — see face_themed().
static void th_chrome(GFXcanvas1 &c, const FaceTheme &t, uint8_t minute) {
  const int16_t ix = th_inset_x(t);
  switch (t.chrome) {
    case CH_CARD:
      ELEM("chrome-card");
      c.drawRoundRect(SAFE_X0, SAFE_Y0, SAFE_W, SAFE_H, 12, 1);
      break;
    case CH_BRACKET: {
      // Registration corner ticks. 7 px arms, drawn from the safe corners.
      const int16_t L = 7;
      ELEM("chrome-ticks");
      c.drawFastHLine(SAFE_X0, SAFE_Y0, L, 1);
      c.drawFastVLine(SAFE_X0, SAFE_Y0, L, 1);
      c.drawFastHLine((int16_t)(SAFE_X1 - L + 1), SAFE_Y0, L, 1);
      c.drawFastVLine(SAFE_X1, SAFE_Y0, L, 1);
      c.drawFastHLine(SAFE_X0, SAFE_Y1, L, 1);
      c.drawFastVLine(SAFE_X0, (int16_t)(SAFE_Y1 - L + 1), L, 1);
      c.drawFastHLine((int16_t)(SAFE_X1 - L + 1), SAFE_Y1, L, 1);
      c.drawFastVLine(SAFE_X1, (int16_t)(SAFE_Y1 - L + 1), L, 1);
      break;
    }
    case CH_RULE:
      ELEM("chrome-rule");
      c.drawFastHLine((int16_t)(SAFE_X0 + ix), TH_RULE_Y,
                      (int16_t)(SAFE_W - 2 * ix), 1);
      break;
    case CH_RING: {
      // THE PER-PANEL MINUTE SCALE. Nine ticks, filled to the current minute.
      // See the long note at the top of this section for what this is standing
      // in for and what that costs.
      //
      // DRAWN UPWARD from the rule row. Downward would put the long ticks
      // below it, outside the safe area on the very row the rule sits on.
      ELEM("chrome-scale");
      const int16_t x0 = (int16_t)(SAFE_X0 + ix), x1 = (int16_t)(SAFE_X1 - ix);
      const int lit = (minute > 59 ? 59 : minute) * 8 / 60;
      for (int i = 0; i < 9; i++) {
        const int16_t x = (int16_t)(x0 + (int32_t)(x1 - x0) * i / 8);
        // 3 PX, NOT 4, AND THAT IS ARITHMETIC RATHER THAN TASTE: DIAL's
        // numerals are 48 of the 52 rows, so four rows are all there is, and a
        // 4 px tick spends them all and leaves the scale touching the digits.
        const int16_t len = (i <= lit) ? 3 : 2;
        c.drawFastVLine(x, (int16_t)(TH_RULE_Y - len + 1), len, 1);
      }
      break;
    }
    default: break;
  }
}

// ---- the inverted variant of a theme --------------------------------------
// C ALREADY HAS A WORKING, STYLE-AGNOSTIC INVERSION and this reuses it whole:
// the element draws into `neg`, emit_negative() composites a screened chip
// with the glyph knocked out of it, and all of that happens BETWEEN one ELEM()
// and the next. That timing is not incidental — the host prover snapshots and
// CLEARS the canvas at every element boundary, so a post-pass over the whole
// face would invert a nearly-empty canvas on the host and a full one on the
// device, and the prover would be proving a different program from the one
// that ships.
//
// FIRMWARE D DID EXACTLY THAT POST-PASS. Its inverted render is two passes
// over the whole page with a global `sheet_fg` flipped to 0 in between. That
// is unprovable here by construction, so the mechanism was NOT ported — only
// the POLICY was, and the policy is the part with the findings behind it:
//
//   1. THE PLATE IS BOUNDED BY THE TEXT INK, NOT BY THE PANEL. C's chip
//      already does this.
//   2. CHROME IS MEASURED OUT OF THE PLATE AND DRAWN AT NORMAL POLARITY.
//      Otherwise a card or a rule spanning the panel drags the plate to full
//      width and turns a bounded tile into a lit field — D measured 26% vs
//      62% on that exact question. Here the chrome is simply drawn on `c`
//      rather than on `neg`, so it never enters the ink box the chip is
//      fitted to, and the chip is clamped off it besides (chip_clamp_reset).
//   3. INVERTED GLYPHS RUN SMALLER. C drops a size-7 digit to size 5 for the
//      same reason; a themed panel drops from `big` to `lg` and a themed
//      weekday from `lg` to `md`. A chip around 46 px numerals is two thirds
//      of the panel.
//
// WHICH THEMES' CHROME DOES NOT SURVIVE THIS CLEANLY: none is dropped, but
// CAPSULE's card is the one that changes meaning. Filled, the card reads as
// the tile's own edge with the numerals floating inside it. Inverted, there
// are now two nested rounded shapes — the 1 px card on the boundary and the
// lit plate inside it — and the plate, not the card, is what the eye reads as
// the tile. The card survives as a hairline frame around it. That is the
// honest per-panel equivalent of what D got, and it is called out here rather
// than left for someone to notice on the glass.
struct ThemeTiers { const GFXfont *num, *cap, *lbl; };
static ThemeTiers th_tiers(const FaceTheme &t, bool inv, bool hollow) {
  // Inverted steps every tier down one, because the chip is what costs the
  // panel and the chip is fitted to the ink.
  if (inv) return ThemeTiers{t.lg, t.md, t.sm};
  // OUTLINE swaps `big` for `bigo` AND CHANGES NOTHING ELSE. The two are the
  // same glyph at the same pixel height, so every measurement, ladder step and
  // nudge below is unaffected -- which is exactly why the outline variant is a
  // tier swap rather than a second layout. Only the display tier has an
  // outline cut; the caption and label tiers stay solid, because a 10 px
  // outlined label is mush at 1 bit.
  return hollow ? ThemeTiers{t.bigo, t.lg, t.sm}
                : ThemeTiers{t.big,  t.lg, t.sm};
}

// ---- fitting, because stepping sideways is not always enough --------------
// The nudge below moves a block away from the corner overlay, and on most
// theme/widget/overlay combinations that is all it takes: a two-digit block is
// 64-88 px of the 116 px safe width and has real slack across. It is NOT
// always enough. CAPSULE gives up 6 px of inset at each end and its weekday is
// 83 px of tracked caps, so beside a three-character temperature there is
// nowhere left to go, and a nudge that runs out of room silently CLAMPS —
// which puts the block back against the overlay it was moved off. The prover
// caught exactly that, as CROWDED on the widest weekday of the widest theme.
//
// So a block that cannot clear the overlay steps DOWN A TIER instead. Fitting
// rather than assuming a size is also what makes this right on all five
// families at once: ROUNDEL's display face is a third wider than DATASHEET's,
// and a hand-picked step would clip one or waste the other.
static const GFXfont *th_fit_trk(GFXcanvas1 &c, const char *s, int16_t maxw,
                                 const GFXfont *const *fs, int n, int16_t tr) {
  for (int i = 0; i < n - 1; i++)
    if (gfx_track_w(c, fs[i], s, tr) <= maxw) return fs[i];
  return fs[n - 1];      // no tier below the smallest; the nudge takes it from here
}
static const GFXfont *th_fit_cells(const char *s, int16_t maxw,
                                   const GFXfont *const *fs, int n) {
  for (int i = 0; i < n - 1; i++)
    if (gfx_cells_w(fs[i], s) <= maxw) return fs[i];
  return fs[n - 1];
}

// The corner overlay's own box, so the numerals can be moved clear of it
// BEFORE they are drawn rather than after the prover complains. Returns the
// left edge the content must stay clear of, or SCR_W when there is no overlay.
static int16_t th_overlay_x0(GFXcanvas1 &c, const FaceTheme &t, uint8_t ov,
                             const FaceData &d, char *buf, size_t bufsz);

// One themed panel, start to finish.
static void face_themed(GFXcanvas1 &c, const FaceTheme &t, uint8_t w,
                        uint8_t ov, const FaceData &d, bool inv, bool hollow) {
  const ThemeTiers ti = th_tiers(t, inv, hollow);
  const int16_t ix = th_inset_x(t), iy = th_inset_y(t);
  const int16_t ch = th_chrome_h(t);
  const int16_t cy = th_clock_cy(t);

  // THE CHIP'S WINDOW. The inset panel, and on a ruled theme two further rows
  // off the bottom so the plate can never be lit underneath its own chrome.
  chip_x0 = (int16_t)(SAFE_X0 + ix);
  chip_x1 = (int16_t)(SAFE_X1 - ix);
  chip_y0 = (int16_t)(SAFE_Y0 + iy);
  chip_y1 = (int16_t)(SAFE_Y1 - iy - (ch ? ch + 2 : 0));

  // Chrome FIRST and at normal polarity, for the reason above.
  th_chrome(c, t, d.minute);

  // The overlay's footprint, measured before anything is placed.
  char ovb[10] = "";
  const int16_t ovx0 = th_overlay_x0(c, t, ov, d, ovb, sizeof ovb);
  // How far left the content must move to keep a clear column between it and
  // the corner value. SIDEWAYS RATHER THAN UP, for the reason OVERLAY_NUDGE
  // documents: at these sizes the numerals already spend the safe height, so
  // lifting them pushes them out through the top, while a two-digit block is
  // 64-88 px of the 116 px safe width and has genuine slack across.
  // AND THE CHIP IS WIDER THAN THE GLYPH IT WRAPS. An inverted element is a
  // plate fitted to the ink plus CHIP_PAD_X, so it reaches further toward the
  // corner than the numerals do — the same fact CHIP_NUDGE exists for on the
  // seven-segment block. Measuring the nudge against the ink alone put the
  // plate through the overlay on 96 screens.
  const int16_t pad = inv ? CHIP_PAD_X : (int16_t)0;
  // The columns a block may actually spend: the inset panel, stopped short of
  // the corner value, less the chip's own padding at both ends.
  const int16_t xlimit = (ovx0 >= SCR_W) ? (int16_t)(SAFE_X1 - ix)
                                         : (int16_t)(ovx0 - 5);
  const int16_t avail  = (int16_t)(xlimit - (SAFE_X0 + ix) - 2 * pad);
  // The ladder every themed element steps down. `md` and `sm` are the same two
  // tiers D falls back through, and on an inverted look the top of the ladder
  // is already one step down.
  const GFXfont *num_lad[3] = {ti.num, ti.cap, ti.lbl};
  const GFXfont *cap_lad[3] = {ti.cap, t.md,   t.sm};

  auto nudged_cx = [&](int16_t blockw) {
    const int16_t centre = (int16_t)((SAFE_X0 + SAFE_X1 + 1) / 2);
    const int16_t right  = (int16_t)(centre + blockw / 2 + pad);
    // Three clear columns, not one. A tracked cap string is placed from its
    // summed ink widths and lands within a pixel of that; one clear column is
    // inside the rounding, and the prover reported the pair as CROWDED — no
    // clear pixel at all — on the widest weekday of the widest theme.
    const int16_t limit  = (int16_t)(ovx0 - 5);
    int16_t cx = centre;
    if (right > limit) cx = (int16_t)(cx - (right - limit));
    // Never past the left inset: a nudge that clips is worse than a crowd.
    const int16_t leftmin = (int16_t)(SAFE_X0 + ix + pad + blockw / 2);
    if (cx < leftmin) cx = leftmin;
    return cx;
  };

  char b[16];
  const bool ok = d.valid;

  switch (w) {
    case W_HOUR:
    case W_MINUTE: {
      if (w == W_HOUR) {
        uint8_t h = d.hour;
        if (!d.hour24) { h = (uint8_t)(d.hour % 12); if (!h) h = 12; }
        // No leading zero on a 12-hour clock — it reads "4", not "04". The
        // tabular cell keeps the pair in register anyway, so a one-digit hour
        // does not move the two-digit ones.
        snprintf(b, sizeof b, d.hour24 ? "%02u" : "%u", (unsigned)h);
      } else {
        snprintf(b, sizeof b, "%02u", (unsigned)d.minute);
      }
      const char *s = ok ? b : "--";
      // "--" is not in the digits-only `big` tier — it holds 0x30-0x39 and
      // nothing else — so the RTC-unreadable path borrows the full-charset
      // one. Same height class, and it is the only string on this panel that
      // is not a numeral.
      const GFXfont *f = ok ? th_fit_cells(s, avail, num_lad, 3) : ti.cap;
      const int16_t bw = ok ? gfx_cells_w(f, s) : gfx_tb(c, f, s).w;
      ELEM(w == W_HOUR ? "hour" : "minute");
      {
        GFXcanvas1 &tc = inv_target(c, inv);
        if (ok) gfx_cells(tc, f, s, nudged_cx(bw), cy);
        else {
          GfxTB tb = gfx_tb(tc, f, s);
          gfx_at(tc, f, s, (int16_t)(nudged_cx(bw) - tb.w / 2),
                 (int16_t)(cy - tb.h / 2));
        }
      }
      emit_negative(c, inv);
      break;
    }
    case W_WEEKDAY: {
      const char *s = ok ? FX_WDAY[d.weekday % 7] : "---";
      const GFXfont *f = th_fit_trk(c, s, avail, cap_lad, 3, t.track);
      const int16_t bw = gfx_track_w(c, f, s, t.track);
      ELEM("weekday");
      gfx_trk_cap(inv_target(c, inv), f, s, t.track, nudged_cx(bw), cy);
      emit_negative(c, inv);
      break;
    }
    case W_DATE: {
      // THE MONTH IS A LABEL AND THE DAY IS THE NUMBER, at two different
      // weights. Setting them at the same weight made a block that filled the
      // whole safe height and could not coexist with a rule — it clipped the
      // moment a theme wanted one.
      snprintf(b, sizeof b, "%u", (unsigned)d.day);
      const char *mon = FX_MON[(d.month ? d.month : 1) - 1];
      const GFXfont *mf = ti.lbl;
      const GFXfont *df = th_fit_cells(b, avail, num_lad + 1, 2);
      const int16_t mh = gfx_tb(c, mf, "AUG").h;
      const int16_t dh = gfx_tb(c, df, "88").h;
      // Two chips each padded by CHIP_PAD_Y would meet in the middle of a
      // two-line date, which is one merged block on the panel and an overlap
      // the prover would fail. Same fix as stack2_impl: widen the gap.
      const int16_t gap = inv ? (int16_t)(4 + 2 * CHIP_PAD_Y + 1) : (int16_t)4;
      const int16_t total = (int16_t)(mh + gap + dh);
      const int16_t y = (int16_t)(cy - total / 2);
      const int16_t mw = gfx_track_w(c, mf, mon, t.track);
      const int16_t dw = gfx_cells_w(df, b);
      ELEM("date-month");
      gfx_trk_cap(inv_target(c, inv), mf, mon, t.track, nudged_cx(mw),
                  (int16_t)(y + mh / 2));
      emit_negative(c, inv);
      ELEM("date-day");
      gfx_cells(inv_target(c, inv), df, b, nudged_cx(dw),
                (int16_t)(y + mh + gap + dh / 2));
      emit_negative(c, inv);
      break;
    }
    default:
      // widget_allows() does not offer any other widget in a themed style, so
      // this is unreachable from the menu and from the clock page alike. It is
      // not an assert because this file cannot assert; it is simply blank,
      // which is the darkest and least surprising thing a panel can be.
      break;
  }

  // The corner value, at normal polarity even on an inverted look — it is a
  // second independent thing on the panel, not part of the plate, and putting
  // it inside the chip would either grow the plate to the corner or knock a
  // hole in it where no glyph is.
  if (*ovb) {
    ELEM("overlay");
    GfxTB tb = gfx_tb(c, t.sm, ovb);
    gfx_at(c, t.sm, ovb, (int16_t)(SAFE_X1 - ix - tb.w),
           (int16_t)(SAFE_Y1 - iy - (ch ? ch + 1 : 0) - tb.h));
  }

  // LEAVE THE CANVAS AS THE CLASSIC PATHS EXPECT TO FIND IT. GFXcanvas1 keeps
  // the font pointer, and fx_left() below draws with setTextSize() and print()
  // — which silently uses whatever custom font was left set. One line here
  // instead of a defensive setFont(nullptr) scattered through six call sites.
  c.setFont(nullptr);
  chip_clamp_reset();
}

// The corner value, formatted and measured. Shares its formatting rules with
// draw_overlay() below — including the THREE CHARACTERS, HARD clamp on the
// temperature, which is a layout invariant rather than a consequence of the
// reading.
static int16_t th_overlay_x0(GFXcanvas1 &c, const FaceTheme &t, uint8_t ov,
                             const FaceData &d, char *buf, size_t bufsz) {
  buf[0] = 0;
  switch (ov) {
    case OV_SECONDS: snprintf(buf, bufsz, "%02u", (unsigned)d.second); break;
    case OV_AMPM:    snprintf(buf, bufsz, "%s", d.hour < 12 ? "AM" : "PM"); break;
    case OV_TEMP: {
      int16_t tt = d.temp_f ? (int16_t)(d.temp_c10 * 9 / 5 + 320) : d.temp_c10;
      int16_t deg = (int16_t)(tt / 10);
      if (deg >  99) deg =  99;
      if (deg <  -9) deg =  -9;
      snprintf(buf, bufsz, "%d%c", (int)deg, d.temp_f ? 'F' : 'C');
      break;
    }
    default: return SCR_W;
  }
  if (!*buf) return SCR_W;
  const int16_t w = gfx_tb(c, t.sm, buf).w;
  return (int16_t)(SAFE_X1 - th_inset_x(t) - w);
}

// ---- overlays -------------------------------------------------------------
// The corner value. This is the ONLY place two independent things share a
// panel, so it is the only real overlap risk on the device — and the reason
// the prover walks every widget x style x overlay combination rather than
// spot-checking the ones that look risky.
//
// Bottom-right, size 1, hard against the safe corner. Size 1 is also the one
// size that is never hollowed.
static void draw_overlay(GFXcanvas1 &c, uint8_t ov, const FaceData &d) {
  if (ov == OV_NONE) return;
  char b[10] = "";
  switch (ov) {
    case OV_SECONDS: snprintf(b, sizeof(b), "%02u", (unsigned)d.second); break;
    case OV_AMPM:    snprintf(b, sizeof(b), "%s", d.hour < 12 ? "AM" : "PM"); break;
    case OV_TEMP: {
      // THREE CHARACTERS, HARD. The overlay lives in a corner the main content
      // is only 4 px clear of, so its width is a layout invariant and not a
      // consequence of the reading. "-40F" is four characters and collides with
      // a size-7 numeral; clamping keeps the corner the size the rest of the
      // layout was designed around. The unclamped value is a widget of its own
      // for anyone who wants it.
      int16_t t = d.temp_f ? (int16_t)(d.temp_c10 * 9 / 5 + 320) : d.temp_c10;
      int16_t deg = (int16_t)(t / 10);
      if (deg >  99) deg =  99;
      if (deg <  -9) deg =  -9;
      snprintf(b, sizeof(b), "%d%c", (int)deg, d.temp_f ? 'F' : 'C');
      break;
    }
    default: return;
  }
  if (!*b) return;
  ELEM("overlay");
  fx_left(c, b, 1, SAFE_X1 - fx_text_w(b, 1) + 1, SAFE_Y1 - fx_text_h(1) + 1,
          false);
}

// A text widget rendered in whichever glyph style is selected. SHADOW and
// STENCIL do their own centring — they have to, because the shadow offset
// changes the block's width — so they cannot go through fx_center.
// THE OVERLAY NUDGE, and why it moves sideways rather than up.
//
// The shadow offset grows the glyph block down and to the right, straight at
// the corner overlay: the prover reported 122 screens with no clear pixel
// between the two. The obvious fix is to lift the block, and it is wrong — at
// size 7 the glyph already fills the 52 px safe height exactly, so lifting it
// pushed 8760 screens out through the TOP. The prover caught that immediately,
// which is the entire reason it exists.
//
// Sideways there is genuine slack: a two-digit size-7 block is 80 px of the
// 116 px safe width. Shifting left both increases the gap to a bottom-right
// overlay and stays inside the envelope at every size.
//
// It applies to SHADOW and STENCIL only. OUTLINE and FILLED were never
// crowded, and moving them would have been a change with no finding behind it.
static const int16_t OVERLAY_NUDGE = 4;

// INVERTED FLIPS `hollow`, and that is the point rather than an accident.
// OUTLINE inverted knocks the glyph out SOLID — a dark digit on a lit chip,
// the look everyone means by "inverted", and the lower-fill of the two because
// a solid glyph removes more lit pixels than a 1 px trace does. FILLED
// inverted knocks out the OUTLINE instead, which reads as an embossed digit.
// So the two stay visibly different stops on the MODE cycle, and the one
// nearest the default is the kinder one to the panel.
static void draw_text_styled(GFXcanvas1 &c, const char *t, uint8_t size,
                             uint8_t style, bool hollow, const char *elem,
                             bool has_overlay, bool inv) {
  int16_t dx = has_overlay ? -OVERLAY_NUDGE : 0;
  if (style == S_SHADOW)  { face_shadow(c, t, size, dx, inv);  return; }
  if (style == S_STENCIL) { face_stencil(c, t, size, dx, inv); return; }
  ELEM(elem);
  fx_center(inv_target(c, inv), t, inv ? inv_size(size) : size, 0,
            inv ? !hollow : hollow);
  emit_negative(c, inv);
}

// ---- the widget dispatcher ------------------------------------------------
// A numeric widget in a numeric style is the common path; everything else
// falls back to text, and widget_allows() in store.cpp is what stops the menu
// ever offering a combination that would have to.
static void draw_numeric(GFXcanvas1 &c, uint8_t style, uint8_t val,
                         uint8_t range, bool two_digit, bool hollow,
                         const char *fmt_text, bool has_overlay, bool inv) {
  (void)range;
  switch (style) {
    case S_SEG:     face_seg(c, val, two_digit, has_overlay, inv); return;
    case S_WORDS:   face_words(c, val, hollow, inv);               return;
    case S_SHADOW:  face_shadow(c, fmt_text, 7,
                                has_overlay ? -OVERLAY_NUDGE : 0, inv);  return;
    case S_STENCIL: face_stencil(c, fmt_text, 7,
                                 has_overlay ? -OVERLAY_NUDGE : 0, inv); return;
    default: break;
  }
  ELEM("big");
  // Size 7 is the largest that fits: two digits are 11*7 = 77 px wide against
  // 116 available, and 7*7 = 49 px tall against 52. Inverted drops to 5, where
  // the chip that wraps it costs the panel 28% instead of 52%.
  fx_center(inv_target(c, inv), fmt_text, inv ? inv_size(7) : 7, 0,
            inv ? !hollow : hollow);
  emit_negative(c, inv);
}

void face_render(GFXcanvas1 &c, uint8_t w, uint8_t s, uint8_t ov,
                 const FaceData &d) {
  // The INVERT bit is stripped here and threaded down as a flag, so every
  // switch below still sees one of the six real styles and no case has to
  // learn about compositing.
  const bool inv = fx_is_inverted(s);
  // BOTH MODIFIER BITS ARE READ BEFORE THE MASK, and that ordering is the
  // whole of a bug this file has now had twice. fx_base_style() clears 0x20
  // and 0x40 both; reading fx_is_hollow(s) after the assignment below always
  // answers false, so the five OUTLINE stops of the MODE cycle rendered as
  // their FILLED twins — pixel for pixel, which is why nothing failed. The
  // prover's per-stop fill table is what showed it: "DIAL OUT" and "DIAL"
  // reported the same 21.3% to the tenth.
  const bool hollow_tier = fx_is_hollow(s);
  s = fx_base_style(s);
  c.fillScreen(0);
  // The classic paths draw with the built-in 5x7 font. A themed render leaves
  // a custom font set on the canvas, and print() would silently keep using it.
  c.setFont(nullptr);
  c.setTextWrap(false);
  // Reset every call, so nothing a previous render did can reach this one —
  // which is what "faces.cpp is pure" has to mean for the prover.
  chip_clamp_reset();
  face_scratch_reset();
  const bool hollow = (s != S_FILLED);
  char b[16];

  // ---- the ported themes -------------------------------------------------
  // A themed style is a different LAYOUT, not a different glyph treatment, so
  // it branches here rather than inside draw_numeric(). It handles the
  // RTC-unreadable case itself: "--" set in the theme's own face is a better
  // answer than falling out to the 5x7 fallback, which would show a face the
  // user did not choose at the moment the clock is least trustworthy.
  if (const FaceTheme *t = theme_of(s)) {
    face_themed(c, *t, w, ov, d, inv, hollow_tier);
    return;
  }

  if (!d.valid && w != W_LOGO && w != W_BLANK) {
    ELEM("no-time");
    fx_center(inv_target(c, inv), "--", inv ? inv_size(7) : 7, 0,
              inv ? !hollow : hollow);
    emit_negative(c, inv);
    return;
  }

  switch (w) {
    case W_HOUR: {
      uint8_t h = d.hour;
      if (!d.hour24) { h = (uint8_t)(d.hour % 12); if (!h) h = 12; }
      // No leading zero on a 12-hour clock — it reads "4", not "04".
      if (d.hour24) snprintf(b, sizeof(b), "%02u", (unsigned)h);
      else          snprintf(b, sizeof(b), "%u", (unsigned)h);
      draw_numeric(c, s, h, d.hour24 ? 23 : 12, h >= 10, hollow, b,
                   ov != OV_NONE, inv);
      break;
    }
    case W_MINUTE:
      snprintf(b, sizeof(b), "%02u", (unsigned)d.minute);
      draw_numeric(c, s, d.minute, 59, true, hollow, b, ov != OV_NONE, inv);
      break;
    case W_SECOND:
      snprintf(b, sizeof(b), "%02u", (unsigned)d.second);
      draw_numeric(c, s, d.second, 59, true, hollow, b, ov != OV_NONE, inv);
      break;
    case W_DAY:
      snprintf(b, sizeof(b), "%u", (unsigned)d.day);
      draw_numeric(c, s, d.day, 31, d.day >= 10, hollow, b, ov != OV_NONE, inv);
      break;
    case W_MONTH:
      if (s == S_WORDS) {
        draw_numeric(c, s, d.month, 12, d.month >= 10, hollow, "",
                     ov != OV_NONE, inv);
      } else {
        draw_text_styled(c, FX_MON[(d.month ? d.month : 1) - 1], 5, s, hollow,
                         "month", ov != OV_NONE, inv);
      }
      break;
    case W_YEAR:
      snprintf(b, sizeof(b), "%u", (unsigned)d.year);
      draw_text_styled(c, b, 4, s, hollow, "year", ov != OV_NONE, inv);
      break;
    case W_WEEKDAY:
      // "WED" at size 5 is 3*30-5 = 85 px wide and 35 tall — comfortably
      // inside 116 x 52, and a size larger than the old layout managed.
      draw_text_styled(c, FX_WDAY[d.weekday % 7], 5, s, hollow, "weekday",
                       ov != OV_NONE, inv);
      break;
    case W_DATE:
      // Two elements on one panel, stacked. The month sits in the upper half
      // and the day in the lower, with a gap the prover measures.
      snprintf(b, sizeof(b), "%u", (unsigned)d.day);
      stack2_impl(c, FX_MON[(d.month ? d.month : 1) - 1], 3, b, 4, 2, hollow,
                  "date-month", "date-day", inv);
      break;
    case W_AMPM:
      draw_text_styled(c, d.hour < 12 ? "AM" : "PM", 7, s, hollow, "ampm",
                       ov != OV_NONE, inv);
      break;
    case W_TEMP: {
      int16_t t = d.temp_f ? (int16_t)(d.temp_c10 * 9 / 5 + 320) : d.temp_c10;
      // The sign has to be carried separately. For -9 <= t <= -1, t/10
      // truncates toward zero to 0, so "%d.%u" printed -0.4 C as "0.4".
      {
        int16_t at = (int16_t)(t < 0 ? -t : t);
        snprintf(b, sizeof(b), "%s%u.%u", t < 0 ? "-" : "",
                 (unsigned)((uint16_t)at / 10u), (unsigned)((uint16_t)at % 10u));
      }
      // "-123.4" is six characters; a fixed size 4 is 140 px and overruns the
      // 116 px safe width, so the size follows the string.
      stack2_impl(c, b, fx_fit(b, 4, SAFE_W), d.temp_f ? "F" : "C", 2, 4,
                  hollow, "temp", "temp-unit", inv);
      break;
    }
    case W_HUMIDITY:
      snprintf(b, sizeof(b), "%u", (unsigned)d.humidity);
      stack2_impl(c, b, fx_fit(b, 5, SAFE_W), "%RH", 2, 3, hollow,
                  "humidity", "humidity-unit", inv);
      break;
    case W_SECBAR: {
      // NOT INVERTED, deliberately. The bar is already a filled block that
      // moves; wrapping it in a lit chip would light most of the panel for the
      // one widget whose whole virtue is that its pixels are on for a minute
      // an hour. It renders the same on both halves of the MODE cycle.
      // A minute as a progress bar. The most burn-in friendly thing here:
      // every pixel it lights is lit for at most a minute an hour.
      ELEM("secbar-frame");
      c.drawRect(SAFE_X0, SAFE_Y0 + 18, SAFE_W, 16, 1);
      ELEM("secbar-fill");
      int16_t fw = (int16_t)((int32_t)(SAFE_W - 4) * d.second / 59);
      // HONOUR `hollow`. This used to ignore the style entirely, so three of
      // the four styles widget_allows() offered rendered identically and the
      // MODE gesture visibly changed every panel except this one.
      if (fw > 0) {
        if (hollow) {
          // Vertical ticks rather than a solid block: same reading, a fifth
          // of the lit area.
          for (int16_t x = SAFE_X0 + 2; x < SAFE_X0 + 2 + fw; x += 3)
            c.drawFastVLine(x, SAFE_Y0 + 20, 12, 1);
        } else {
          c.fillRect(SAFE_X0 + 2, SAFE_Y0 + 20, fw, 12, 1);
        }
      }
      break;
    }
    case W_LOGO:
      ELEM("logo");
      // At size 6 the wordmark is 102 px wide and reaches x=114, straight
      // through the overlay corner. One size smaller clears it.
      {
        uint8_t ls = ov != OV_NONE ? 5 : 6;
        fx_center(inv_target(c, inv), "4SQ", inv ? inv_size(ls) : ls, 0,
                  inv ? !hollow : hollow);
        emit_negative(c, inv);
      }
      break;
    case W_BLANK:
    default:
      break;                     // deliberately nothing: the darkest option
  }

  draw_overlay(c, ov, d);
}
// ============================================================ name tables ==
static const char *const WIDGET_NAME[W_COUNT] = {
  "HOUR", "MINUTE", "SECOND", "WEEKDAY", "DAY", "MONTH", "YEAR",
  "DATE", "AM/PM", "TEMP", "HUMIDITY", "SEC BAR", "LOGO", "BLANK"
};
static const char *const STYLE_NAME[S_COUNT] = {
  "OUTLINE", "FILLED", "SEGMENT", "WORDS", "SHADOW", "STENCIL",
  "DIAL", "DATASHEET", "QUIET", "ROUNDEL", "CAPSULE"
};
static const char *const OVERLAY_NAME[OV_COUNT] = {
  "NONE", "SECONDS", "AM/PM", "TEMP"
};

const char *widget_name(uint8_t w)  { return w < W_COUNT  ? WIDGET_NAME[w]  : "?"; }
const char *style_name(uint8_t s) {
  const uint8_t b = fx_base_style(s);
  if (b >= S_COUNT) return "?";
  // The OUTLINE variant of a themed face has to NAME ITSELF, or the prover's
  // findings and the serial report both say "DIAL" for two different screens
  // and there is no way to tell which stop of the cycle produced one.
  if (fx_is_hollow(s) && !fx_is_inverted(s)) {
    static char out_buf[24];
    snprintf(out_buf, sizeof out_buf, "%s OUT", STYLE_NAME[b]);
    return out_buf;
  }
  if (!fx_is_inverted(s)) return STYLE_NAME[b];
  // A shared buffer, which is safe for the same reason `scratch` is: rendering
  // is single-threaded and the caller formats the string before the next call.
  static char inv_buf[24];
  snprintf(inv_buf, sizeof inv_buf, "%s INV", STYLE_NAME[b]);
  return inv_buf;
}
const char *overlay_name(uint8_t o) { return o < OV_COUNT ? OVERLAY_NAME[o] : "?"; }

// Which style/widget pairs are real. A seven-segment WEEKDAY or a binary-dots
// MONTH would have to be faked by falling back to text, and a menu that offers
// a choice which silently does nothing is worse than one that does not offer
// it. The host prover walks exactly this predicate, so what it checks and what
// the menu offers cannot drift apart.
bool widget_allows(uint8_t w, uint8_t s) {
  // The INVERT bit is a COMPOSITING mode, not a glyph style, so it is decided
  // separately from what the widget can draw. Every widget already has a
  // canonical rendering for a style it cannot honour — a DATE is always two
  // stacked lines whatever the style says — and that rendering inverts fine.
  // Without this, render_clock()'s fallback would strip the bit and half the
  // MODE stops would come up inverted on two panels and normal on the other
  // two, which is the bug and not the feature.
  if (fx_is_inverted(s)) return widget_allows(w, S_OUTLINE);
  // AND THE HOLLOW BIT IS A TIER SELECTION, not a glyph style — `bigo` instead
  // of `big`, the same glyph at the same pixel height. It has to be stripped
  // here for exactly the reason INVERT is, and the failure when it is not was
  // measured rather than argued: S_DIAL | S_HOLLOW is 0x26, which trips the
  // `s >= S_COUNT` guard below, so widget_allows() said NO for every hollow
  // themed stop and render_clock()'s fallback quietly substituted S_OUTLINE.
  // FIVE OF THE TWELVE MODE STOPS RENDERED AS THE DEFAULT FACE and the prover
  // passed the whole way, because a screen that draws the wrong thing
  // correctly is not an overlap. What caught it was the per-stop fill table
  // reading 8.1% — OUTLINE's own number — for all five of them.
  if (fx_is_hollow(s)) return widget_allows(w, fx_base_style(s));
  if (w >= W_COUNT || s >= S_COUNT) return false;
  // A THEME IS A CLOCK FACE, AND ONLY A CLOCK FACE. The five ported families
  // carry four layouts between them — hour, minute, weekday, date — which is
  // exactly the set CLOCK_W offers, and they are the four D authored. Nothing
  // in the port says what a themed SEC BAR or a themed LOGO should look like,
  // and a menu that offers a choice which silently falls back to something
  // else is worse than one that does not offer it. The prover walks this same
  // predicate, so what it checks and what the device offers cannot drift.
  if (theme_of(s))
    return w == W_HOUR || w == W_MINUTE || w == W_WEEKDAY || w == W_DATE;
  switch (w) {
    case W_HOUR: case W_MINUTE: case W_SECOND: case W_DAY:
      return true;                                    // 0-59 numerics: all six
    case W_TEMP: case W_HUMIDITY:
      // Composed of a value and a unit stacked as two elements, which the
      // single-string glyph styles cannot express — so only the two that go
      // through stack2.
      return s == S_OUTLINE || s == S_FILLED;
    case W_YEAR:
      // Four digits, so no seg; but it is one string, so the glyph styles work.
      return s == S_OUTLINE || s == S_FILLED || s == S_SHADOW || s == S_STENCIL;
    case W_MONTH:
      return s == S_OUTLINE || s == S_FILLED || s == S_WORDS ||
             s == S_SHADOW  || s == S_STENCIL;        // month has a number too
    case W_WEEKDAY: case W_AMPM:
      return s == S_OUTLINE || s == S_FILLED || s == S_SHADOW || s == S_STENCIL;
    case W_LOGO:
      return s == S_OUTLINE || s == S_FILLED;         // text only
    case W_DATE:
      return s == S_OUTLINE || s == S_FILLED;         // month + day, composed
    case W_SECBAR:
      // Only the two the renderer actually distinguishes.
      return s == S_OUTLINE || s == S_FILLED;
    case W_BLANK:
      return s == S_OUTLINE;                          // one canonical choice
  }
  return false;
}
