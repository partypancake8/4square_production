// pages.cpp, the four pages, all pure. See pages.h for the grammar.
#include "pages.h"
#include "anim.h"
#include "display.h"
#include <string.h>
#include <stdio.h>

// The order MODE cycles. OUTLINE first because it is both the look that was
// picked and the lowest-fill one, so the default costs the panel the least.
// All the normal looks first, then the same looks again as NEGATIVES, a lit
// chip with the glyph knocked out of it (faces.cpp, "inverted looks"). The
// invert is a bit on the style rather than a second set of enum values, so the
// stored setting in the EEPROM keeps meaning exactly what it always meant, and
// the grouping is normal-then-inverted rather than interleaved so pressing
// MODE walks every look once before it walks any of them again.
//
// ---- WHAT IS AND IS NOT IN THIS TABLE, 2026-08-20 -------------------------
// SEGMENT, SHADOW, STENCIL and WORDS were REMOVED FROM THE CYCLE on the user's
// ruling, "none of these shadow bullshit segments". They are removed from
// HERE and from nowhere else: S_SEG, S_SHADOW, S_STENCIL and S_WORDS keep
// their enum values in settings/store.h and their renderers in faces.cpp still
// compile and are still proven. The enum is stored in EEPROM, so renumbering
// it to close the gap would silently relabel every saved record.
//
// In their place are the five type-system themes ported from firmware D. Those
// are a FAMILY plus its CHROME rather than a glyph treatment, see the FTHEME
// table at the top of faces.cpp for what each one is and what the port had to
// change to live without firmware D's sheet.
const uint8_t CLOCK_STYLES[] = {
  // THE CLASSICS FIRST. OUTLINE is the OG look and the lowest-fill one, so the
  // default costs the panel the least.
  S_OUTLINE, S_FILLED,
  // THEN THE FIVE PORTED FACES, EACH OUTLINE THEN FILLED. `bigo` and `big` are
  // the same glyph at the same pixel height, so the pair is one design seen two
  // ways rather than two designs. See S_HOLLOW in faces.h.
  (uint8_t)(S_DIAL      | S_HOLLOW), S_DIAL,
  (uint8_t)(S_DATASHEET | S_HOLLOW), S_DATASHEET,
  (uint8_t)(S_QUIET     | S_HOLLOW), S_QUIET,
  (uint8_t)(S_ROUNDEL   | S_HOLLOW), S_ROUNDEL,
  (uint8_t)(S_CAPSULE   | S_HOLLOW), S_CAPSULE
};
// NOT IN THE CYCLE, AND STILL IN THE ENUM: S_SEG, S_WORDS, S_SHADOW and
// S_STENCIL. The user cut them ("none of these shadow bullshit segments"), but
// `slot_style` is an EEPROM byte, so renumbering the enum to delete them would
// silently relabel every stored record. Dropping them from this table is the
// whole of the change; their renderers still compile and the prover still
// proves them.
//
// The INVERTED variants are likewise gone from the cycle by ruling -- outline
// and filled only. S_INVERT and its compositing path stay in faces.cpp,
// unreferenced by this table.
const uint8_t CLOCK_STYLE_N = (uint8_t)(sizeof(CLOCK_STYLES) / sizeof(CLOCK_STYLES[0]));

// What the clock page puts on each panel. Fixed, not a setting: version A's
// whole point is that a button press changes the page, not that a menu
// configures it.
static const uint8_t CLOCK_W[4]  = { W_HOUR, W_MINUTE, W_WEEKDAY, W_DATE };
static const uint8_t CLOCK_OV[4] = { OV_NONE, OV_SECONDS, OV_NONE, OV_NONE };

// ============================================================== helpers ====

// Three lines centred as a block, same contract as stack2, each line its own
// element so the prover can see them separately, and the BLOCK centred rather
// than each line nudged into place.
static void stack3(GFXcanvas1 &c,
                   const char *a, uint8_t sa, const char *b, uint8_t sb,
                   const char *d, uint8_t sd, int16_t gap, bool hollow,
                   const char *ta, const char *tb, const char *td) {
  int16_t ha = fx_text_h(sa), hb = fx_text_h(sb), hd = fx_text_h(sd);
  int16_t y0 = SAFE_Y0 + (SAFE_H - (ha + gap + hb + gap + hd)) / 2;
  if (*a) {
    ELEM(ta);
    fx_left(c, a, sa, SAFE_X0 + (SAFE_W - fx_text_w(a, sa)) / 2, y0, hollow);
  }
  if (*b) {
    ELEM(tb);
    fx_left(c, b, sb, SAFE_X0 + (SAFE_W - fx_text_w(b, sb)) / 2,
            (int16_t)(y0 + ha + gap), hollow);
  }
  if (*d) {
    ELEM(td);
    fx_left(c, d, sd, SAFE_X0 + (SAFE_W - fx_text_w(d, sd)) / 2,
            (int16_t)(y0 + ha + gap + hb + gap), hollow);
  }
}

// Copy `src` into `dst`, truncated to whatever fits `SAFE_W` at `size`.
// An SSID is user data of unbounded length; fx_fit cannot shrink below size 1,
// so past 19 characters the only honest options are truncate or overflow the
// safe area, and overflowing is not an option the prover permits.
static void fit_copy(char *dst, size_t dstsz, const char *src, uint8_t size) {
  size_t maxc = (size_t)((SAFE_W + size) / (6 * size));
  if (maxc >= dstsz) maxc = dstsz - 1;
  size_t n = strlen(src);
  if (n > maxc) n = maxc;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void fmt_uptime(char *b, size_t n, uint32_t s) {
  uint32_t d = s / 86400u, h = (s % 86400u) / 3600u,
           m = (s % 3600u) / 60u, sec = s % 60u;
  if (d)      snprintf(b, n, "%lud%02luh", (unsigned long)d, (unsigned long)h);
  else if (h) snprintf(b, n, "%luh%02lum", (unsigned long)h, (unsigned long)m);
  else        snprintf(b, n, "%lum%02lus", (unsigned long)m, (unsigned long)sec);
}

// ============================================================ the clock ====
static void render_clock(GFXcanvas1 &c, uint8_t slot, uint8_t variant,
                         const PageData &d) {
  uint8_t style = CLOCK_STYLES[variant % CLOCK_STYLE_N];
  uint8_t w = CLOCK_W[slot & 3];
  // Not every widget can honour every style, DATE is two stacked elements, so
  // a single-string glyph style has nothing to apply to. Falling back keeps
  // the panel legible instead of blank, and widget_allows() is the same
  // predicate the prover walks, so this can never render something unproven.
  if (!widget_allows(w, style)) style = S_OUTLINE;
  face_render(c, w, style, CLOCK_OV[slot & 3], d.clock);
}

// =========================================================== the sensors ===
// Variant 0 is what was asked for: temperature, WiFi, uptime, dimming.
// Variant 1 is the rest of what the board already knows, rather than inventing
// a second screen's worth of readings it would have to fake.
static void render_sensor(GFXcanvas1 &c, uint8_t slot, uint8_t variant,
                          const PageData &d) {
  const SensorData &s = d.sens;
  char a[24], b[24];

  if (variant == 0) {
    switch (slot & 3) {
      case 0:
        if (!s.sht_ok) { ELEM("temp-bad"); fx_center(c, "--", 6, 0, true); break; }
        {
          int16_t t = s.temp_f ? (int16_t)(s.temp_c10 * 9 / 5 + 320) : s.temp_c10;
          int16_t at = (int16_t)(t < 0 ? -t : t);
          // The sign is carried separately: for -9..-1 tenths, t/10 truncates
          // toward zero and "%d.%u" printed -0.4 as "0.4".
          snprintf(a, sizeof a, "%s%d.%u", t < 0 ? "-" : "",
                   at / 10, (unsigned)(at % 10));
          stack2(c, a, fx_fit(a, 4, SAFE_W), s.temp_f ? "F" : "C", 2, 4, true,
                 "temp", "temp-unit");
        }
        break;
      case 1:
        if (!s.wifi_up) {
          stack2(c, "WIFI", 3, "DOWN", 2, 4, true, "wifi-t", "wifi-v");
        } else {
          snprintf(a, sizeof a, "%d", (int)s.rssi);
          fit_copy(b, sizeof b, s.ssid, 1);
          stack3(c, "dBm", 1, a, 4, b, 1, 3, true, "wifi-u", "wifi-v", "wifi-ssid");
        }
        break;
      case 2:
        fmt_uptime(a, sizeof a, s.uptime_s);
        stack2(c, a, fx_fit(a, 3, SAFE_W), "UPTIME", 1, 5, true, "up-v", "up-t");
        break;
      default:
        snprintf(a, sizeof a, "%u", (unsigned)s.contrast);
        snprintf(b, sizeof b, "DIM %s", s.night ? "NIGHT" : "DAY");
        stack2(c, a, 4, b, 1, 5, true, "dim-v", "dim-t");
        break;
    }
    return;
  }

  switch (slot & 3) {                      // variant 1
    case 0:
      if (!s.sht_ok) { ELEM("rh-bad"); fx_center(c, "--", 6, 0, true); break; }
      snprintf(a, sizeof a, "%u", (unsigned)s.humidity);
      stack2(c, a, fx_fit(a, 5, SAFE_W), "%RH", 2, 3, true, "rh-v", "rh-t");
      break;
    case 1:
      fit_copy(a, sizeof a, s.wifi_up ? s.ip : "NO IP", 1);
      stack2(c, "IP", 2, a, 1, 6, true, "ip-t", "ip-v");
      break;
    case 2:
      snprintf(a, sizeof a, "%u", (unsigned)s.light_pct);
      stack2(c, a, 4, "LIGHT %", 1, 5, true, "lux-v", "lux-t");
      break;
    default:
      stack2(c, s.night ? "NIGHT" : "DAY", 3, "MODE", 1, 5, true,
             "nm-v", "nm-t");
      break;
  }
}

// =========================================================== the markets ===
// No longer a page: UP is the stopwatch since 2026-10-15. This renderer stays
// because the showreel's STOCKS segment draws with it (see showreel_draw).
// SYMBOL · PRICE · ARROW+CHANGE, laid out as a TABLE ROW and not as centred
// text. NO SPARKLINE, NO GRAPH, and deliberately so, a 116x52 window cannot
// hold a price history at a size anyone can read, and the version that tried
// was rejected on sight.
//
// THE ALIGNMENT IS THE DESIGN. Each panel is one row of a four-row table that
// happens to be split across four pieces of glass, so every field sits on a
// column that does not move with its own content:
//
//     x6                                                            x121
//   y6  NVDA                                                              <- left
//   y21                                                        1234.56    <- right
//   y42 [/__\ 21x16]                                            +1.23%    <- right
//
// The two numeric fields are RIGHT-ALIGNED to the same edge, so the price and
// the change stack into one column and four panels read down as a table. The
// two glyphs are LEFT-ALIGNED to the same edge for the same reason: whatever
// the numbers are doing, the arrows are always in the identical place on all
// four panels, which is what makes the direction readable from across the room
// before any digit is.
//
// Centring, which is what every other page here does, is exactly wrong for
// this one: a centred "9.99" and a centred "65043" put their decimal points in
// different places and the four panels stop being a table.
//
// Vertical budget, spelled out so it can be checked without running the tool:
//   safe y 6..57
//   sym    size 2  y  6..19   (14)
//   price  size<=3 y 21..41   (21)
//   change size 2  y 44..57   (14)   arrow 16 px tall at y 42..57
// which lands exactly on SAFE_Y1 with nothing hanging over it.
static const int16_t MKT_SYM_Y   = 6;
static const int16_t MKT_SYM_X   = SAFE_X0;
static const int16_t MKT_SYM_MAXW = 5 * 12 - 2;                 // 5 chars @ sz2
static const int16_t MKT_PRICE_Y = 21;
static const int16_t MKT_CHG_Y   = 44;
static const int16_t MKT_ARR_Y   = 42;
static const int16_t MKT_ARR_W   = 21;
static const int16_t MKT_ARR_H   = 16;

// ---- the per-ticker motifs, and why there are none --------------------------
// A 12 px pack icon per ticker WAS BUILT AND THEN MEASURED OUT. The converter
// in the sibling tree emits them ready to paste --
//
//     ../firmware-d/tools/iconpack/emit_market.py
//
// -- and all seven were real pack art (pixelarticons chart-bar-big / gpu /
// car / window-frame / package, HackerNoon apple / bitcoin / dollar-solid),
// nothing hand-drawn. They are not here because of the burn-in fill budget,
// which is 15% of the panel = 1228 lit pixels, and this page's worst
// enumerated screen is NVDA at a six-character price:
//
//     symbol   size 2 hollow, 4 chars     246 px
//     price    size 3 hollow, 6 chars     554 px
//     change   size 2 hollow, 6 chars     259 px
//     arrow    21x16, 3 px outline         88 px
//                                       ------
//   worst enumerated screen (AMZN)       1206 px   14.7%   <- ships
//     + a 12 px motif                      +58 px
//                                          1270 px  15.5%   <- over budget
//
// So the motif is not free-standing decoration competing with white space: it
// costs 58 px on a screen with 97 px of headroom, and the only field big
// enough to pay for it is the change, which would have to drop from size 2 to
// size 1 (a 209 px saving) to make room. A 5x7 percentage under a 16 px arrow
// is exactly the trade this project's taste forbids -- readability over
// decoration -- so the icons went and the numbers stayed. If the fill budget
// is ever renegotiated, re-running the emitter above is a paste job.

// IT IS A THICK OUTLINE AND NOT A SOLID, AND THAT IS NOT A STYLE CHOICE. A
// solid 21x16 triangle has interior pixels that stay lit at every position of
// the +/-6 wander, so the prover measured its relief at 1.07x against a 1.10x
// floor: the anti-burn-in shift bought nothing on the one element that would
// be on screen every time this page is up. Hollowed to a 3 px stroke, no pixel
// survives the whole envelope, and the silhouette is unchanged at any distance
// where the arrow is doing its job. Same argument as the OUTLINE clock face.
static void mkt_arrow(GFXcanvas1 &c, int16_t bp) {
  const int16_t x0 = SAFE_X0, x1 = (int16_t)(SAFE_X0 + MKT_ARR_W - 1);
  const int16_t y0 = MKT_ARR_Y, y1 = (int16_t)(MKT_ARR_Y + MKT_ARR_H - 1);
  const int16_t cx = (int16_t)(x0 + MKT_ARR_W / 2);
  const int16_t k  = 3;                 // how far the knocked-out core is inset
  if (bp > 0) {
    c.fillTriangle(cx, y0, x0, y1, x1, y1, 1);
    c.fillTriangle(cx, (int16_t)(y0 + k + 1), (int16_t)(x0 + k), (int16_t)(y1 - 2),
                   (int16_t)(x1 - k), (int16_t)(y1 - 2), 0);
  } else if (bp < 0) {
    c.fillTriangle(x0, y0, x1, y0, cx, y1, 1);
    c.fillTriangle((int16_t)(x0 + k), (int16_t)(y0 + 2), (int16_t)(x1 - k),
                   (int16_t)(y0 + 2), cx, (int16_t)(y1 - k - 1), 0);
  } else {
    // Zero gets a bar, not a triangle. Drawing an up arrow on an unchanged
    // price to avoid a special case would be the renderer telling a small lie
    // every time the market is shut.
    c.fillRect(x0, (int16_t)(y0 + MKT_ARR_H / 2 - 2), MKT_ARR_W, 3, 1);
  }
}

// Right-aligned to SAFE_X1, the shared numeric column.
static void mkt_right(GFXcanvas1 &c, const char *s, uint8_t size, int16_t y,
                      bool hollow, const char *tag) {
  ELEM(tag);
  fx_left(c, s, size, (int16_t)(SAFE_X1 + 1 - fx_text_w(s, size)), y, hollow);
}

static void render_market(GFXcanvas1 &c, uint8_t slot, uint8_t variant,
                          const PageData &d) {
  (void)variant;
  const Quote &q = d.q[slot & 3];
  const char *sym = q.sym[0] ? q.sym : "----";
  char chg[16];

  // The symbol keeps its column whether or not a quote has landed, so a panel
  // that is still waiting is the same row with its numbers missing rather than
  // a differently shaped screen.
  // THE WIDTH IT IS FITTED AGAINST IS FIVE CHARACTERS AND NOT THE REST OF THE
  // ROW, which is deliberate: every symbol this thing quotes is three or four
  // letters, and the sym field is allowed to be size 2 for as long as that
  // holds. Anything longer, the prover sweeps ABCDEFG through here, steps
  // down to size 1 rather than eating 420 lit pixels of the fill budget on the
  // one input nobody will ever type.
  ELEM("sym");
  fx_left(c, sym, fx_fit(sym, 2, MKT_SYM_MAXW), MKT_SYM_X, MKT_SYM_Y, true);

  // WAITING IS SHOWN AS MISSING, not as a stale or invented number. There is
  // no arrow either: an arrow with no change behind it is a direction claim.
  if (!q.valid) {
    mkt_right(c, "----", 3, MKT_PRICE_Y, true, "price-none");
    mkt_right(c, "--%",  2, MKT_CHG_Y,   true,  "chg-none");
    return;
  }

  // Basis points, so a percent prints exactly without float formatting, the
  // same reason the price arrives as a string. See pages.h.
  //
  // THE PRECISION FALLS AWAY AS THE NUMBER GROWS, and that is a width
  // decision, not a taste one: a fixed "%d.%02u%%" runs to eight characters at
  // the int16 limit ("+320.00%"), and eight characters at size 2 is 425 lit
  // pixels of a 1228 px screen budget, the change field alone would cost more
  // than the price. Two decimals under 10%, one under 100%, none above, caps
  // the field at six characters, and nobody has ever needed to know that a
  // stock is up 320.00% rather than 320%.
  int16_t bp = q.chg_bp;
  const char sign = bp < 0 ? '-' : '+';
  const int  a    = (bp < 0 ? -bp : bp);
  if      (a < 1000)  snprintf(chg, sizeof chg, "%c%d.%02u%%", sign, a / 100,
                               (unsigned)(a % 100));
  else if (a < 10000) snprintf(chg, sizeof chg, "%c%d.%u%%", sign, a / 100,
                               (unsigned)((a % 100) / 10));
  else                snprintf(chg, sizeof chg, "%c%d%%", sign, a / 100);

  // ASK FOR 3 AND LET fx_fit DECIDE. Size 3 holds six characters in the 116 px
  // safe width, which is every price this thing quotes; a seven-character one
  // steps itself down to 2 instead of running out through the side of the
  // panel on the day some index gains a digit.
  mkt_right(c, q.price, fx_fit(q.price, 3, SAFE_W), MKT_PRICE_Y, true, "price");

  ELEM("arrow");
  mkt_arrow(c, bp);
  // Six characters is the widest the change can now be ("-99.9%", "+9.99%",
  // 70 px), which starts at x52 and clears the arrow's 21 px column by 26 px.
  mkt_right(c, chg, fx_fit(chg, 2, (int16_t)(SAFE_X1 - SAFE_X0 - MKT_ARR_W - 3)),
            MKT_CHG_Y, true, "chg");
}

// ========================================================= the stopwatch ===
// THE CLOCK PAGE'S OWN DIGITS, in the clock page's own arrangement. The clock
// puts hours on TL and minutes on TR; the stopwatch puts MINUTES on TL and
// SECONDS on TR, drawn by face_render with W_MINUTE / W_SECOND in S_OUTLINE --
// stop 1 of the MODE cycle, the lowest-fill look and the one the prover holds
// to the budget. Same glyphs, same safe area, same burn-in shift at commit.
//
//     TL  MM (00..59)        TR  SS (00..59)
//     BL  READY|RUN|PAUSED   BR  hours elapsed
//         "STOPWATCH"            "HOURS"
//
// Minutes WRAP at 60 and the hours carry on the BR panel, so the two big
// fields are always exactly two digits and never change width. uint32 ms tops
// out at 1193 h, four digits, which fx_fit keeps inside the panel.
//
// The STATE WORD is the paused/running indicator: a word, not a blinking
// colon, because the four panels have no shared colon to blink and a blink
// would double the bus traffic of a page that otherwise only pushes the
// seconds panel once a second.
static void render_stopwatch(GFXcanvas1 &c, uint8_t slot, const PageData &d) {
  const uint32_t total_s = d.sw.ms / 1000u;
  FaceData f = d.clock;
  f.valid  = true;              // elapsed time does not depend on the RTC
  f.hour24 = true;
  f.minute = (uint8_t)((total_s / 60u) % 60u);
  f.second = (uint8_t)(total_s % 60u);
  switch (slot & 3) {
    case SLOT_TL: face_render(c, W_MINUTE, S_OUTLINE, OV_NONE, f); return;
    case SLOT_TR: face_render(c, W_SECOND, S_OUTLINE, OV_NONE, f); return;
    case SLOT_BL: {
      const char *w = d.sw.state == STW_RUN   ? "RUN"
                    : d.sw.state == STW_PAUSE ? "PAUSED" : "READY";
      stack2(c, w, fx_fit(w, 3, SAFE_W), "STOPWATCH", 1, 5, true,
             "sw-state", "sw-name");
      return;
    }
    default: {
      char hb[12];
      snprintf(hb, sizeof hb, "%lu", (unsigned long)(total_s / 3600u));
      stack2(c, hb, fx_fit(hb, 4, SAFE_W), "HOURS", 1, 5, true,
             "sw-hours", "sw-hlabel");
      return;
    }
  }
}

// ======================================================== the animations ===
// Variant 0 is the old 2x2 arrangement: a different animation on each panel.
// Variants 1..AN_COUNT put one animation on all four.
// THE PAGE IS THE REEL. There is no variant to cycle any more: the panel shows
// whichever drawing the schedule has put in anim_ids[slot], at that panel's own
// phase. This function stays exactly as pure and as enumerable as it was --
// what changed is that its input is now four independent streams rather than
// one number derived from the variant.
static void render_anim(GFXcanvas1 &c, uint8_t slot, uint8_t variant,
                        const PageData &d) {
  (void)variant;
  const uint8_t k = (uint8_t)(slot & 3);
  // THE SCENE DECIDES. The showreel is the page's first stop; the clock
  // interlude is what ui.cpp cuts the shuffled roster to every ~10 s. Both
  // are pure per-slot renderers below, fed from this same snapshot.
  if (d.anim_scene == SCENE_SHOWREEL) {
    showreel_draw(c, k, d.anim_frame, d);
    return;
  }
  if (d.anim_scene == SCENE_CLOCK) {
    showclock_draw(c, k, d.anim_frame, d.clock.hour, d.clock.minute);
    return;
  }
  const uint8_t id = (uint8_t)(d.anim_ids[k] % AN_COUNT);
  anim_draw(c, id, (uint16_t)(d.anim_frame + d.anim_phase[k]));
}

// ===========================================================================
// THE SHOWREEL, the segmented attract-mode loop. See pages.h for the map.
// ===========================================================================
#include "wxdemo.h"

// ---- the clock interlude ---------------------------------------------------
// Which digit this slot carries, 12-hour with a leading zero (a dark TL for
// hours 1-9 would read as a dead panel on camera, which is the one reading a
// marketing loop must not allow).
static char sr_digit(uint8_t slot, uint8_t hour, uint8_t minute) {
  uint8_t h12 = (uint8_t)(hour % 12);
  if (h12 == 0) h12 = 12;
  uint8_t v = 0;
  switch (slot & 3) {
    case SLOT_TL: v = (uint8_t)(h12 / 10);    break;
    case SLOT_TR: v = (uint8_t)(h12 % 10);    break;
    case SLOT_BL: v = (uint8_t)(minute / 10); break;
    default:      v = (uint8_t)(minute % 10); break;
  }
  return (char)('0' + v);
}

void showclock_draw(GFXcanvas1 &c, uint8_t slot, uint16_t tick,
                    uint8_t hour, uint8_t minute) {
  slot = (uint8_t)(slot & 3);
  const uint16_t t = (uint16_t)(tick % SHOWCLOCK_TICKS);
  const uint16_t slam = slot;                       // staggered one tick/slot
  if (t < slam) return;
  const uint16_t held = (uint16_t)(t - slam);
  // Two growth frames, then full size: 3 -> 5 -> 7. Size 7 is the clock's own
  // numeral size, already proven against the safe area; hollow is the
  // product's signature glyph treatment and reads crisp on camera.
  const uint8_t size = (uint8_t)(held == 0 ? 3 : (held == 1 ? 5 : 7));
  const char ds[2] = { sr_digit(slot, hour, minute), '\0' };
  ELEM("sc-digit");
  fx_center(c, ds, size, 0, true);
}

bool showclock_blank_ok(uint8_t slot, uint16_t tick) {
  return (uint16_t)(tick % SHOWCLOCK_TICKS) < (uint16_t)(slot & 3);
}

// ---- the sensors segment ---------------------------------------------------
// One metric per panel, big: TL temperature, TR humidity, BL ambient light,
// BR uptime. The values are LIVE, this is the segment that proves the board
// is a real instrument and not a slideshow, and the compositions are the
// readings page's own (stack2, hollow, fx_fit), so the showcase and the page
// cannot drift apart in style.
static void sr_sensors(GFXcanvas1 &c, uint8_t slot, uint16_t ts,
                       const SensorData &s) {
  if (ts < (uint16_t)(slot * 2)) return;            // staggered arrival
  char a[24], b[24];
  switch (slot & 3) {
    case SLOT_TL:
      if (!s.sht_ok) { ELEM("sr-temp-bad"); fx_center(c, "--", 6, 0, true); break; }
      {
        int16_t t = s.temp_f ? (int16_t)(s.temp_c10 * 9 / 5 + 320) : s.temp_c10;
        int16_t at = (int16_t)(t < 0 ? -t : t);
        snprintf(a, sizeof a, "%s%d.%u", t < 0 ? "-" : "",
                 at / 10, (unsigned)(at % 10));
        stack2(c, a, fx_fit(a, 4, SAFE_W), s.temp_f ? "F" : "C", 2, 4, true,
               "sr-temp", "sr-temp-u");
      }
      break;
    case SLOT_TR:
      if (!s.sht_ok) { ELEM("sr-rh-bad"); fx_center(c, "--", 6, 0, true); break; }
      snprintf(a, sizeof a, "%u%%", (unsigned)s.humidity);
      stack2(c, a, fx_fit(a, 4, SAFE_W), "HUMIDITY", 1, 5, true,
             "sr-rh", "sr-rh-t");
      break;
    case SLOT_BL:
      snprintf(a, sizeof a, "%u%%", (unsigned)s.light_pct);
      stack2(c, a, fx_fit(a, 4, SAFE_W), "LIGHT", 1, 5, true,
             "sr-light", "sr-light-t");
      break;
    default:
      fmt_uptime(a, sizeof a, s.uptime_s);
      snprintf(b, sizeof b, "%s", "UPTIME");
      stack2(c, a, fx_fit(a, 3, SAFE_W), b, 1, 5, true, "sr-up", "sr-up-t");
      break;
  }
}

// ---- the weather segment ---------------------------------------------------
// One day per panel from the baked block in wxdemo.h: label on top, the
// animated sky in the WX_BOX band, the temperature underneath. The three
// bands are disjoint by construction, label rows end at 20, the box is
// 22..41, the temperature starts at 43, which is what lets the sky animate
// under the overlap law without ever negotiating with the text.
static void sr_weather(GFXcanvas1 &c, uint8_t slot, uint16_t ts) {
  if (ts < slot) return;                            // staggered arrival
  const WxDay &w = WX_DEMO[slot & 3];
  char b[8];
  ELEM("wx-day");
  fx_left(c, w.label, 2,
          (int16_t)(SAFE_X0 + (SAFE_W - fx_text_w(w.label, 2)) / 2), 7, true);
  wx_anim_draw(c, w.kind, ts);                      // its own ELEM inside
  snprintf(b, sizeof b, "%dF", (int)w.temp_f);
  ELEM("wx-temp");
  fx_left(c, b, 2,
          (int16_t)(SAFE_X0 + (SAFE_W - fx_text_w(b, 2)) / 2), 43, true);
}

// The segment map, SR_AT_*, lives in pages.h, exported for the prover's
// targeted data-domain sweeps.

void showreel_draw(GFXcanvas1 &c, uint8_t slot, uint16_t tick,
                   const PageData &d) {
  slot = (uint8_t)(slot & 3);
  const uint16_t t = (uint16_t)(tick % SHOWREEL_TICKS);
  if (t < SR_AT_WALK)
    showclock_draw(c, slot, t, d.clock.hour, d.clock.minute);
  else if (t < SR_AT_SENS)
    sr_pass_draw(c, slot, (uint16_t)(t - SR_AT_WALK), (uint8_t)(d.sr_walk & 3));
  else if (t < SR_AT_CLK2)
    sr_sensors(c, slot, (uint16_t)(t - SR_AT_SENS), d.sens);
  else if (t < SR_AT_WX)
    showclock_draw(c, slot, (uint16_t)(t - SR_AT_CLK2),
                   d.clock.hour, d.clock.minute);
  else if (t < SR_AT_STK)
    sr_weather(c, slot, (uint16_t)(t - SR_AT_WX));
  else if (t < SR_AT_HEART) {
    // The markets page's own renderer over whatever quotes the snapshot
    // carries, on the demo build, the baked walk. Ticker, price, hollow
    // arrow; deliberately NO graphs.
    if ((uint16_t)(t - SR_AT_STK) >= (uint16_t)(slot * 2))
      render_market(c, slot, 0, d);
  } else if (t < SR_AT_BLANK)
    sr_heart_draw(c, (uint16_t)(t - SR_AT_HEART));
  // else: the blank beat
}

// The wall clock of the reel: which segments run on the fast dirty-commit
// tick. Stated here, next to the map, so retiming a segment is one edit.
uint16_t showreel_tick_ms(uint16_t tick) {
  const uint16_t t = (uint16_t)(tick % SHOWREEL_TICKS);
  const bool fast = (t >= SR_AT_WALK && t < SR_AT_SENS) ||   // the walker
                    (t >= SR_AT_WX   && t < SR_AT_STK);      // the skies
  return fast ? SR_FAST_MS : (uint16_t)ANIM_FRAME_MS;
}

bool showreel_blank_ok(uint8_t slot, uint16_t tick, const PageData &d) {
  slot = (uint8_t)(slot & 3);
  const uint16_t t = (uint16_t)(tick % SHOWREEL_TICKS);
  if (t < SR_AT_WALK)  return showclock_blank_ok(slot, t);
  if (t < SR_AT_SENS)  return sr_pass_blank_ok(slot, (uint16_t)(t - SR_AT_WALK),
                                               (uint8_t)(d.sr_walk & 3));
  if (t < SR_AT_CLK2)  return (uint16_t)(t - SR_AT_SENS) < (uint16_t)(slot * 2);
  if (t < SR_AT_WX)    return showclock_blank_ok(slot, (uint16_t)(t - SR_AT_CLK2));
  if (t < SR_AT_STK)   return (uint16_t)(t - SR_AT_WX) < slot;
  if (t < SR_AT_HEART) return (uint16_t)(t - SR_AT_STK) < (uint16_t)(slot * 2);
  if (t < SR_AT_BLANK) return false;                // every heart frame lights
  return true;                                      // the blank beat
}

// =========================================================== the settings ===
// ONE ITEM PER PANEL, and the selected one carries a bar along its bottom
// edge. The alternative, a scrolling list on one panel with the other three
// idle, throws away three quarters of the display to imitate a device that
// only has one screen. Here every panel is a row, and which row is live is a
// property you can read from the other side of the room.

static const char *sens_name(uint8_t level) {
  switch (level) {
    case 0:  return "OFF";
    case 1:  return "LOW";
    case 2:  return "MED";
    case 3:  return "HIGH";
    default: return "MAX";
  }
}

uint8_t set_window_start(uint8_t cursor) {
  if (SI_COUNT <= 4) return 0;
  if (cursor >= SI_COUNT) cursor = (uint8_t)(SI_COUNT - 1);
  // Keep one row of context above the cursor where there is room for it, so
  // moving down the list scrolls by one instead of jumping a whole screen.
  int16_t s = (int16_t)cursor - 1;
  if (s < 0) s = 0;
  if (s > (int16_t)SI_COUNT - 4) s = (int16_t)SI_COUNT - 4;
  return (uint8_t)s;
}

void set_row_text(const SettingsData &s, uint8_t item,
                  char *label, uint8_t label_sz, char *value, uint8_t value_sz) {
  label[0] = 0;
  value[0] = 0;
  switch (item) {
    case SI_SENS:
      snprintf(label, label_sz, "%s", "LIGHT SENS");
      snprintf(value, value_sz, "%s", sens_name(s.sens_level));
      break;
    case SI_TEMP:
      snprintf(label, label_sz, "%s", "TEMP UNIT");
      snprintf(value, value_sz, "%s", s.temp_f ? "F" : "C");
      break;
    case SI_OFF_EN:
      snprintf(label, label_sz, "%s", "AUTO OFF");
      snprintf(value, value_sz, "%s", s.off_enable ? "ON" : "OFF");
      break;
    case SI_OFF_START:
      snprintf(label, label_sz, "%s", "SLEEP AT");
      snprintf(value, value_sz, "%02u:%02u",
               (unsigned)s.off_start_h, (unsigned)s.off_start_m);
      break;
    default:
      snprintf(label, label_sz, "%s", "WAKE AT");
      snprintf(value, value_sz, "%02u:%02u",
               (unsigned)s.off_end_h, (unsigned)s.off_end_m);
      break;
  }
}

// The row geometry, spelled out because the prover enforces it and a reader
// should be able to check the arithmetic without running the tool:
//
//   safe area   y 6..57
//   label  sz1  y 8..14           (7 px)
//   value band  y 15..51          (37 px), the value CENTRED inside it
//   cursor bar  y 52..54          (3 px), on the selected row only
//
// The value is centred in its band rather than pinned to a fixed y. Pinning it
// was the first version and it looked wrong the moment two rows next to each
// other resolved to different sizes: a one-character "C" and a five-character
// "22:30" hung from the same line with quite different amounts of air beneath
// them. Centring also means the size is free to change without a second
// constant having to be edited to match, which is the kind of pair that drifts.
static const int16_t SET_LABEL_Y  = 8;
static const int16_t SET_BAND_Y0  = 15;
static const int16_t SET_BAND_H   = 37;
static const int16_t SET_BAR_Y    = 52;
static const int16_t SET_BAR_H    = 3;

static void render_settings(GFXcanvas1 &c, uint8_t slot, const PageData &d) {
  const SettingsData &s = d.set;
  uint8_t item = (uint8_t)(set_window_start(s.cursor) + (slot & 3));
  if (item >= SI_COUNT) return;            // unreachable; a blank row, not junk

  char label[16], value[16];
  set_row_text(s, item, label, sizeof label, value, sizeof value);

  ELEM("set-label");
  fx_left(c, label, 1, SAFE_X0 + (SAFE_W - fx_text_w(label, 1)) / 2,
          SET_LABEL_Y, false);

  // ASK FOR 4 AND LET fx_fit DECIDE, rather than hardcoding a size that
  // happens to work. Size 4 is the largest the band can hold (28 px of 37) and
  // the longest value, "22:30", comes to exactly the 116 px of safe width  - 
  // legal, and the prover is what says so rather than this arithmetic. Short
  // values get the full 4; anything longer that arrives later steps itself
  // down instead of running out through the side of the safe area silently,
  // on the one item nobody thought to re-check.
  uint8_t vs = fx_fit(value, 4, SAFE_W);
  int16_t vy = (int16_t)(SET_BAND_Y0 + (SET_BAND_H - fx_text_h(vs)) / 2);
  ELEM("set-value");
  fx_left(c, value, vs, SAFE_X0 + (SAFE_W - fx_text_w(value, vs)) / 2, vy, true);

  if (item == s.cursor) {
    ELEM("set-cursor");
    c.fillRect(SAFE_X0, SET_BAR_Y, SAFE_W, SET_BAR_H, 1);
  }
}

// ================================================================ entry ====
uint8_t page_variants(uint8_t page) {
  switch (page) {
    case PG_CLOCK:  return CLOCK_STYLE_N;
    case PG_SENSOR: return 2;
    // ONE. UP on the stopwatch starts it; there is no look to cycle.
    case PG_STOPWATCH: return 1;
    // ONE. The animation page plays itself, so there is nothing for a repeat
    // press to step through -- a repeat press reshuffles instead, which ui.cpp
    // does directly. This used to be 1 + AN_COUNT, back when the page was a
    // manual picker.
    case PG_ANIM:   return 1;
    // ONE. The settings page has no variants: its state is the cursor, and
    // pressing SET moves that rather than cycling the page. Returning anything
    // else here would let the modulo in page_render silently reinterpret a
    // cursor as a variant.
    case PG_SETTINGS: return 1;
  }
  return 1;
}

const char *page_name(uint8_t page) {
  switch (page) {
    case PG_CLOCK:    return "CLOCK";
    case PG_SENSOR:   return "SENSORS";
    case PG_STOPWATCH: return "STOPWATCH";
    case PG_ANIM:     return "ANIM";
    case PG_SETTINGS: return "SETTINGS";
  }
  return "?";
}

void page_render(GFXcanvas1 &c, uint8_t page, uint8_t slot, uint8_t variant,
                 const PageData &d) {
  if (page >= PG_COUNT) page = PG_CLOCK;
  variant = (uint8_t)(variant % page_variants(page));

  // The clock delegates to face_render, which clears for itself. Everything
  // else clears here, so no page can inherit pixels from the one before it  - 
  // the same assumption the prover makes when it snapshots elements.
  if (page == PG_CLOCK) { render_clock(c, slot, variant, d); return; }
  // So does the stopwatch on its two digit panels; the other two are cleared
  // below like every other page, and clearing first is harmless for all four.

  c.fillScreen(0);
  c.setTextWrap(false);
  switch (page) {
    case PG_SENSOR:   render_sensor(c, slot, variant, d); break;
    case PG_STOPWATCH: render_stopwatch(c, slot, d);       break;
    case PG_SETTINGS: render_settings(c, slot, d);        break;
    default:          render_anim(c, slot, variant, d);   break;
  }
}
