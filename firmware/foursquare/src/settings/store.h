// store.h, the 24LC256 EEPROM (U3) and the settings that live in it.
//
// WHY AN EEPROM AND NOT NVS. U3 is a real DIP-8 24LC256 in a socket on the
// main I2C bus; it is on the board precisely so settings survive. Using it
// instead of the ESP32's NVS also means a firmware flash, including a
// partition-table change, which erases NVS, never loses the user's setup.
#pragma once
#include <Arduino.h>
#include "../config.h"

// ---- raw device -----------------------------------------------------------
// 24LC256: 32 KB, 64-byte pages, ~5 ms write cycle. A page write must not
// cross a page boundary; the store below only ever writes whole aligned pages,
// so that constraint is satisfied by construction rather than by care.
bool ee_present();
bool ee_read(uint16_t addr, uint8_t *buf, uint16_t len);
bool ee_write_page(uint16_t addr, const uint8_t *buf, uint8_t len);

// ---- settings -------------------------------------------------------------
// What a screen shows. Numeric widgets render in any style; text widgets only
// make sense in a few, which widget_allows() below encodes so the menu can
// never offer a combination the renderer would have to fake.
enum Widget : uint8_t {
  W_HOUR = 0, W_MINUTE, W_SECOND, W_WEEKDAY, W_DAY, W_MONTH, W_YEAR,
  W_DATE, W_AMPM, W_TEMP, W_HUMIDITY, W_SECBAR, W_LOGO, W_BLANK, W_COUNT
};

// How it is drawn. OUTLINE is the default everywhere: hollowing a glyph cuts
// its lit area by ~75%, which is the single biggest burn-in win available on
// a device that shows the same digit in the same place for years.
// Version A: BARS and DOTS are gone. They were built, looked at on the real
// glass and rejected, a 12-bar gauge and a binary dot row are both puzzles
// rather than clocks. SHADOW and STENCIL replace them: still hollow, still
// inside the burn-in budget, but legible at a glance from across a room.
//
// ⛔ THIS ENUM IS STORED IN EEPROM (Settings::slot_style). ⛔
// The numeric values are FROZEN. S_SEG, S_WORDS, S_SHADOW and S_STENCIL were
// dropped from the MODE cycle (pages.cpp, CLOCK_STYLES[]) on the user's ruling
//, "none of these shadow bullshit segments", but they are NOT dropped from
// here and their renderers still compile. Renumbering to close the gap would
// silently relabel every saved record, which is the exact class of bug the
// single-source rule exists to prevent. New styles are APPENDED, never
// inserted.
//
// The five appended below are the type-system themes ported from firmware D.
// A theme is not a glyph trick: it is a FAMILY (five generated tiers of a real
// outline face) plus the CHROME that family wears. See the FTHEME table at the
// top of screens/faces.cpp.
enum Style : uint8_t {
  S_OUTLINE = 0, S_FILLED, S_SEG, S_WORDS, S_SHADOW, S_STENCIL,
  // ---- appended for the firmware-D theme port, 2026-08-20 -----------------
  S_DIAL,        // Outfit numerals over a tick scale
  S_DATASHEET,   // Iosevka mono with registration corner ticks
  S_QUIET,       // Quicksand geometric, one hairline, widest tracking
  S_ROUNDEL,     // Baloo 2 ExtraBold on bare glass
  S_CAPSULE,     // Fredoka inside a 1 px rounded card
  S_COUNT
};
// The contiguous themed block, so faces.cpp can index its table with one
// subtraction and every other file can ask the question without knowing how.
static const uint8_t S_THEME_FIRST = S_DIAL;
static const uint8_t S_THEME_N     = (uint8_t)(S_CAPSULE - S_DIAL + 1);

// A small second value tucked into a corner of the same screen. This is the
// only place two things share a panel, so it is the only real overlap risk  - 
// and it is exactly what the host-side prover in tools/layoutcheck exists to
// rule out, for every widget/style/overlay combination.
enum Overlay : uint8_t {
  OV_NONE = 0, OV_SECONDS, OV_AMPM, OV_TEMP, OV_COUNT
};

static const uint8_t CFG_VERSION = 2;

// How hard the room's light pulls on the panel brightness. This scales the
// WIDTH of the light window the dimming curve spans, around its midpoint:
// a narrow window means a small change in the room swings the panel across its
// whole range, a wide one means it barely moves. Sensitivity is therefore a
// property of the CURVE, not of the caps, bright_day and bright_night still
// bound the result at both ends whatever this is set to.
//
// OFF is not a value of this field; it is autodim == 0. Keeping them separate
// means the serial 'D' pin and the existing autodim flag keep working exactly
// as they did, and the menu just presents the two as one knob.
enum DimSens : uint8_t {
  DIM_SENS_LOW = 1, DIM_SENS_MED, DIM_SENS_HIGH, DIM_SENS_MAX, DIM_SENS_COUNT
};

struct Settings {
  uint8_t version;
  uint8_t hour24;            // 0 = 12-hour (default). "don't do military time"
  uint8_t slot_widget[N_SCREENS];
  uint8_t slot_style[N_SCREENS];
  uint8_t slot_overlay[N_SCREENS];
  uint8_t bright_day;        // contrast cap in a lit room
  uint8_t bright_night;      // contrast floor in the dark
  uint8_t autodim;
  uint8_t night_mode;        // 0 normal, 1 dim, 2 blank
  // Hour AND minute. Hours alone cannot express the 22:30 default the
  // anti-burn-in design asks for, which is how this was caught.
  uint8_t night_start_h, night_start_m;
  uint8_t night_end_h,   night_end_m;
  uint8_t shift_secs;        // anti-burn-in pixel shift period
  uint8_t shift_amp;         // and its amplitude in pixels
  uint8_t led_mode;          // 0 = off, 1 = status. Idle is dark either way.
  uint8_t led_bright;
  uint8_t temp_unit;         // 0 = C, 1 = F
  uint8_t wifi_on;
  // ---- added at CFG_VERSION 2, INSIDE the old reserved[8] ------------------
  // Deliberately carved out of the existing reserved bytes rather than
  // appended: sizeof(Settings) is unchanged, so a record written by version 1
  // is exactly the same length and slot_read()'s overlay puts the ZEROS that
  // version 1 wrote into these fields. Every one of them therefore has to
  // treat 0 as "never set", which settings_sanitize() does. Appending instead
  // would have been equally safe on length but would have spent two more
  // bytes of the 56-byte payload budget for nothing.
  uint8_t dim_sens;          // DimSens; 0 = never set -> MED
  uint8_t off_enable;        // daily "screens off" schedule. 0 = off, and 0 is
                             // also what a version 1 record supplies, which is
                             // the right answer for an upgrade.
  uint8_t off_start_h, off_start_m;   // screens go dark at
  uint8_t off_end_h,   off_end_m;     // and come back at
  uint8_t reserved[2];
};

extern Settings cfg;

void     settings_defaults(Settings &s);
// Clamp every field into its legal range. MUST be called on anything that came
// out of the EEPROM: a CRC proves the bytes arrived intact, not that they were
// ever meaningful, and several of these are used as raw array indices.
void     settings_sanitize(Settings &s);
bool     settings_load();          // true if a stored record was found
bool     settings_save();          // no-op if nothing changed since last save
void     settings_mark_dirty();
void     settings_tick();          // deferred write, so held buttons don't
                                   // burn a page per press
uint16_t settings_writes();        // records written this power-on, for the
                                   // system info page

bool widget_allows(uint8_t w, uint8_t s);
const char *widget_name(uint8_t w);
const char *style_name(uint8_t s);
const char *overlay_name(uint8_t o);
