#include "sensors.h"
#include "bus.h"
#include <Wire.h>
#include "../app/leds.h"            // the blank is bracketed there; the strip
                                    // itself is private to leds.cpp now

// ==================================================================== RTC ==
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint8_t bcd2dec(uint8_t v) { return (uint8_t)(((v >> 4) * 10) + (v & 0x0F)); }

static uint8_t reg(uint8_t r) {
  Wire.beginTransmission(ADDR_RTC);
  Wire.write(r);
  if (Wire.endTransmission() != 0) { bus_note_error(DEV_RTC); return 0xFF; }
  if (Wire.requestFrom((uint8_t)ADDR_RTC, (uint8_t)1) != 1) { bus_note_error(DEV_RTC); return 0xFF; }
  return Wire.read();
}

static void reg_write(uint8_t r, uint8_t v) {
  Wire.beginTransmission(ADDR_RTC);
  Wire.write(r); Wire.write(v);
  if (Wire.endTransmission() != 0) bus_note_error(DEV_RTC);
}

void rtc_begin() { /* nothing to configure; the part free-runs on VBAT */ }

RtcTime rtc_read() {
  RtcTime t = {0, 0, 0, 0, 0, 0, false};
  Wire.beginTransmission(ADDR_RTC);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission() != 0) { bus_note_error(DEV_RTC); return t; }
  if (Wire.requestFrom((uint8_t)ADDR_RTC, (uint8_t)7) != 7) { bus_note_error(DEV_RTC); return t; }
  t.sec  = bcd2dec(Wire.read() & 0x7F);
  t.min  = bcd2dec(Wire.read() & 0x7F);
  t.hour = bcd2dec(Wire.read() & 0x3F);     // forced 24-hour mode on write
  Wire.read();                              // day-of-week register, never used
  t.day  = bcd2dec(Wire.read() & 0x3F);
  uint8_t mo = Wire.read();
  t.mon  = bcd2dec(mo & 0x1F);
  t.year = (uint16_t)(2000 + bcd2dec(Wire.read()) + ((mo & 0x80) ? 100 : 0));
  // A plausibility gate. A bus glitch that returns 0xFF everywhere would
  // otherwise be rendered as a confident, wrong time, and a clock that is
  // obviously broken is better than one that is quietly wrong.
  t.ok = t.mon >= 1 && t.mon <= 12 && t.day >= 1 && t.day <= 31 &&
         t.hour < 24 && t.min < 60 && t.sec < 60 &&
         // The year register was the only BCD field neither masked nor
         // range-checked: bcd2dec(0xFF) is 165, giving a confident "2165".
         t.year >= 2000 && t.year <= 2099;
  if (t.ok) bus_note_ok(DEV_RTC);
  return t;
}

static RtcTime  cached = {0, 0, 0, 0, 0, 0, false};
static uint32_t cached_at = 0;
static bool     cache_valid = false;
static const uint32_t RTC_CACHE_MS = 100;

RtcTime rtc_now(uint32_t now_ms) {
  if (!cache_valid || (uint32_t)(now_ms - cached_at) >= RTC_CACHE_MS) {
    cached = rtc_read();
    cached_at = now_ms;
    cache_valid = true;
  }
  return cached;
}

// After setting the clock, the cache is a lie until it is thrown away.
void rtc_invalidate() { cache_valid = false; }

void rtc_write_time(uint16_t y, uint8_t mo, uint8_t d,
                    uint8_t h, uint8_t mi, uint8_t s) {
  Wire.beginTransmission(ADDR_RTC);
  Wire.write((uint8_t)0x00);
  Wire.write(dec2bcd(s));
  Wire.write(dec2bcd(mi));
  Wire.write(dec2bcd(h));            // bit 6 clear = 24-hour mode
  Wire.write((uint8_t)1);            // day-of-week: written but never trusted
  Wire.write(dec2bcd(d));
  Wire.write(dec2bcd(mo));           // century bit clear; we stay in 20xx
  Wire.write(dec2bcd((uint8_t)(y % 100)));
  Wire.endTransmission();
  rtc_invalidate();
}

bool rtc_osf() { return (reg(0x0F) & 0x80) != 0; }
void rtc_clear_osf() { reg_write(0x0F, (uint8_t)(reg(0x0F) & 0x7F)); }

static enum { T_IDLE, T_BUSY } tstate = T_IDLE;
static uint32_t t_started = 0;
static float    t_c = NAN;

static bool osf_cached = false;
bool rtc_osf_cached() { return osf_cached; }

void rtc_temp_trigger() {
  osf_cached = rtc_osf();          // once a second, with the conversion
  if (tstate == T_BUSY) return;
  if (reg(0x0F) & 0x04) return;                  // already converting
  reg_write(0x0E, (uint8_t)(reg(0x0E) | 0x20));  // CONV
  tstate = T_BUSY;
  t_started = millis();
}

void rtc_temp_poll() {
  if (tstate != T_BUSY) return;
  bool done = !(reg(0x0F) & 0x04);
  if (!done && millis() - t_started < 300) return;
  tstate = T_IDLE;
  Wire.beginTransmission(ADDR_RTC);
  Wire.write((uint8_t)0x11);
  if (Wire.endTransmission() != 0) { bus_note_error(DEV_RTC); return; }
  if (Wire.requestFrom((uint8_t)ADDR_RTC, (uint8_t)2) != 2) { bus_note_error(DEV_RTC); return; }
  int8_t  ti = (int8_t)Wire.read();
  uint8_t tf = (uint8_t)(Wire.read() >> 6);
  t_c = ti + tf * 0.25f;
}

float rtc_temp_c() { return t_c; }

// ================================================================== SHT31 ==
static uint8_t sht_crc(uint8_t msb, uint8_t lsb) {
  uint8_t crc = 0xFF, d[2] = {msb, lsb};
  for (uint8_t i = 0; i < 2; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool sht_read(float &t_out, float &rh) {
  Wire.beginTransmission(ADDR_SHT);
  Wire.write(0x2C); Wire.write(0x0D);        // clock-stretch, medium repeatability
  if (Wire.endTransmission() != 0) { bus_note_error(DEV_SHT); return false; }
  delay(8);
  if (Wire.requestFrom((uint8_t)ADDR_SHT, (uint8_t)6) != 6) { bus_note_error(DEV_SHT); return false; }
  uint8_t tm = Wire.read(), tl = Wire.read(), tc = Wire.read();
  uint8_t hm = Wire.read(), hl = Wire.read(), hc = Wire.read();
  if (sht_crc(tm, tl) != tc || sht_crc(hm, hl) != hc) { bus_note_error(DEV_SHT); return false; }
  t_out = -45.0f + 175.0f * (((tm << 8) | tl) / 65535.0f);
  rh    = 100.0f * (((hm << 8) | hl) / 65535.0f);
  bus_note_ok(DEV_SHT);
  return true;
}

// ================================================================== light ==
static uint16_t l_raw = 0;
static uint32_t l_mv  = 0;

// R7 and the LEDs sit on the same board face 9.5 mm apart, so a lit LED shines
// straight into the sensor. MEASURED 2026-08-07: the same room reads 78-92 with
// the LEDs dark and 172-192 with a red LED lit. Blanking for 25 ms first is
// what makes this a measurement of the ROOM. 4 ms was tried and is not enough  - 
// a phototransistor needs longer than that to decay, and every calibration made
// against 4 ms readings was wrong.
// WHAT CHANGED, AND WHY. This used to be `strip.clear(); strip.show();
// delay(25)` and then nothing, the strip was left BLACK until whenever
// led_tick next happened to run. That is a 25 ms hard-edged hole punched into
// every LED animation, once a second, forever. It is the black notch the user
// reported as flicker, and it was the dominant visible defect in the LED layer.
//
// The blanking itself is not negotiable: 4 ms was tried and a phototransistor
// does not decay that fast, so every calibration made against 4 ms readings was
// wrong. What IS negotiable is paying for it when there is nothing to blank,
// paying for it every single second, and cutting rather than fading. So:
//
//   - Strip already dark? No blank, no settle, no cost, and the reading is
//     clean as it stands. IDLE IS DARK, so this is the usual case.
//   - Strip lit? Blank at most every LIGHT_BLANK_LIT_MIN_MS instead of every
//     LIGHT_PERIOD_MS, and ease out and back in rather than cutting, restoring
//     the exact frame that was showing. The LED image stays continuous.
//
// The measurement is unchanged in either branch: the ADC is still read with the
// LEDs genuinely off and still after the full LIGHT_BLANK_MS settle.
void light_sample() {
  uint32_t now = millis();
  static uint32_t last_lit_sample = 0;
  if (!led_is_dark()) {
    if (last_lit_sample &&
        (uint32_t)(now - last_lit_sample) < LIGHT_BLANK_LIT_MIN_MS)
      return;                            // keep the last reading; it is 4 s old
    last_lit_sample = now ? now : 1;
  }
  bool blanked = led_light_blank_begin();
  if (blanked) delay(LIGHT_BLANK_MS);
  uint32_t a = 0;
  for (int i = 0; i < 4; i++) a += analogRead(PIN_LDR);
  l_raw = (uint16_t)(a / 4);
  l_mv  = analogReadMilliVolts(PIN_LDR);
  if (blanked) led_light_blank_end();
}

uint16_t light_raw() { return l_raw; }
uint32_t light_mv()  { return l_mv; }
