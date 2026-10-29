// A host model of the WS2812 strip, the part the PC does not have.
//
// Same rule the store prover works under (its shim/Wire.h models the 24LC256
// and nothing else): the value of this gate is that it runs the exact leds.cpp
// that ships, so everything reimplemented here is something the gate could be
// wrong about. This file contains no animation, no easing, no colour and no
// timing. It is a pin, a latch and three counters.
//
// The counters exist because of a real, expensive rule: strip.begin() must be
// called EXACTLY ONCE, in led_begin(). On the C3 the RMT binding never comes
// back if anything re-inits or borrows GPIO7, the LEDs go permanently dead
// while every call still reports success. That cost a full day. It is now
// a mechanical assertion instead of a comment.
#pragma once
#include <stdint.h>
#include <string.h>

#define NEO_GRB     0x52
#define NEO_KHZ800  0x0000

// The last frame LATCHED by show(), and the counters. Defined in ledcheck.cpp.
#define NEO_MAX_PIXELS 64
extern uint8_t  neo_latched[NEO_MAX_PIXELS][3];
extern int      neo_begin_calls;
extern int      neo_shows;
extern uint8_t  neo_brightness;
extern int      neo_pin;

class Adafruit_NeoPixel {
public:
  Adafruit_NeoPixel(uint16_t n, int16_t pin, uint8_t type)
      : n_(n > NEO_MAX_PIXELS ? NEO_MAX_PIXELS : n) {
    (void)type;
    neo_pin = pin;
    memset(buf_, 0, sizeof(buf_));
  }
  void begin() { neo_begin_calls++; }
  void setBrightness(uint8_t b) { neo_brightness = b; }
  void clear() { memset(buf_, 0, sizeof(buf_)); }
  void setPixelColor(uint16_t i, uint32_t c) {
    if (i >= n_) return;                       // the real library ignores these
    buf_[i][0] = (uint8_t)(c >> 16);
    buf_[i][1] = (uint8_t)(c >> 8);
    buf_[i][2] = (uint8_t)c;
  }
  void show() {
    memcpy(neo_latched, buf_, sizeof(buf_));
    neo_shows++;
  }
  static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }
  uint16_t numPixels() const { return n_; }

private:
  uint16_t n_;
  uint8_t  buf_[NEO_MAX_PIXELS][3];
};
