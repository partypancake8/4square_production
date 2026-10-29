// faces.h, what a screen shows, and how it is drawn.
//
// EVERYTHING IN HERE IS PURE. A face is handed a GFXcanvas1 (plain RAM) and a
// snapshot of the data; it cannot reach I2C, cannot read a global, cannot know
// what time it is except through what it was passed. That is what lets the
// host-side prover in tools/layoutcheck compile THIS EXACT SOURCE, against the
// real Adafruit_GFX and the real font, and prove properties about the pixels
// that will actually appear on the panels.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>
#include "../settings/store.h"

// A snapshot, not a live reference, a face must render the same pixels every
// time it is called with the same input, or the prover proves nothing.
struct FaceData {
  uint8_t  hour;        // 0-23, always. 12-hour is a presentation choice.
  uint8_t  minute;
  uint8_t  second;
  uint8_t  day;         // 1-31
  uint8_t  month;       // 1-12
  uint16_t year;
  uint8_t  weekday;     // 0 = Sunday, computed by Sakamoto from the date
  int16_t  temp_c10;    // tenths of a degree C
  uint8_t  humidity;    // %RH
  bool     hour24;
  bool     temp_f;
  bool     valid;       // false when the RTC could not be read
};

// ---- element attribution --------------------------------------------------
// Faces call ELEM() before each independent thing they draw. On the device the
// hook is null and this costs one null check per element. On the host the
// prover installs a hook that lifts each element into its own bitmap, so it
// can prove no two of them share a pixel.
//
// THE RULE: anything that could conceivably collide with something else on the
// same panel must be its own element. Two elements that overlap by one pixel
// is a bug the prover will fail the build over.
typedef void (*ElemHook)(const char *name);
extern ElemHook face_elem_hook;
inline void ELEM(const char *name) { if (face_elem_hook) face_elem_hook(name); }

// ---- inverted looks -------------------------------------------------------
// MODE cycles the clock's look, and half those looks are NEGATIVES: a lit chip
// with the glyph knocked out of it. That is expressed as a bit OR'd onto a
// Style rather than as six more enum values, because the stored setting in the
// EEPROM is a Style and must keep meaning exactly what it meant before  - 
// inversion is a compositing mode the MODE button applies, not a new glyph.
//
// 0x40 is chosen to sit clear of S_COUNT (6 when this was written, 11 since
// the theme port) with room for the enum to keep growing.
static const uint8_t S_INVERT = 0x40;

// ---- the OUTLINE variant of a themed face ---------------------------------
// The five ported families each ship TWO display tiers: `big`, a solid
// numeral, and `bigo`, a TRUE closed outline of the same glyph (the
// thresholded form minus its own erosion, not a stroke font, which is why its
// counters come out as loops rather than as a dashed fragment).
//
// So "outline vs filled" for a themed face is not a second design, it is the
// tier the theme already carries, and it is expressed the same way inversion
// is: A BIT OR'd ONTO A STYLE, never a second enum value. The stored setting
// in the EEPROM is a Style and has to keep meaning exactly what it meant
// before; this is a rendering mode MODE applies on top of it.
//
// 0x20 sits below S_INVERT's 0x40 and clear of S_COUNT (11), with room for the
// enum to keep growing underneath both.
static const uint8_t S_HOLLOW = 0x20;

// BOTH modifier bits are masked off, or a themed style with a bit set indexes
// past the end of the style table. This is fed from an EEPROM byte.
inline uint8_t fx_base_style(uint8_t s)  { return (uint8_t)(s & 0x1F); }
inline bool    fx_is_inverted(uint8_t s) { return (s & S_INVERT) != 0; }
inline bool    fx_is_hollow(uint8_t s)   { return (s & S_HOLLOW) != 0; }

// ---- rendering ------------------------------------------------------------
// The one entry point. Draws widget `w` in style `s` with corner overlay `ov`.
// The canvas is cleared first, so a face cannot inherit pixels from the last
// frame, another thing the prover would otherwise have to assume.
void face_render(GFXcanvas1 &c, uint8_t w, uint8_t s, uint8_t ov,
                 const FaceData &d);

// Shared text helpers, exposed because the menu system draws with them too and
// must obey the same safe area.
void fx_center(GFXcanvas1 &c, const char *s, uint8_t size, int16_t dy,
               bool hollow);
void fx_left(GFXcanvas1 &c, const char *s, uint8_t size, int16_t x, int16_t y,
             bool hollow);
void fx_outline_text(GFXcanvas1 &c, const char *s, uint8_t size,
                     int16_t x, int16_t y);
// Two lines centred AS A BLOCK, each its own element. Exported because the
// pages draw with it too: centring each line separately and nudging with a
// hand-tuned dy is exactly what put the humidity readout outside the safe area
// the first time, and one shared implementation cannot drift back into that.
void stack2(GFXcanvas1 &c, const char *a, uint8_t sa, const char *b,
            uint8_t sb, int16_t gap, bool hollow,
            const char *tag_a, const char *tag_b);
int16_t fx_text_w(const char *s, uint8_t size);
int16_t fx_text_h(uint8_t size);
// Largest size at which `s` still fits `avail_w`.
uint8_t fx_fit(const char *s, uint8_t want, int16_t avail_w);

uint8_t fx_weekday(uint16_t y, uint8_t m, uint8_t d);
extern const char *const FX_WDAY[7];
extern const char *const FX_MON[12];
