// A host-side stand-in for Arduino.h, just wide enough to compile the REAL
// store.cpp and defaults.cpp on a PC.
//
// WHY THIS IS A SHIM AND NOT A REIMPLEMENTATION. Same rule the layout prover
// works under: the value of the store prover is that it exercises the exact
// source that ships, so anything reimplemented here is something the prover
// could be wrong about. This file contains no store logic, no CRC, no ring  - 
// only Print (so a Serial exists), the PROGMEM macros, and a millis() the
// deferred-write timer can read. The EEPROM itself is modelled in Wire.h,
// which is a model of the CHIP, not of the code under test.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "Print.h"
#include "pgmspace.h"

typedef bool boolean;
typedef uint8_t byte;

// Adafruit_GFX declares getTextBounds() overloads for these two even though
// nothing the firmware calls uses them. Declaring the types is enough to
// compile the header; the firmware uses plain const char* everywhere, on the
// device as well as here.
class __FlashStringHelper;
#define F(s) (s)

class String {
  const char *p;
public:
  String(const char *s = "") : p(s ? s : "") {}
  unsigned length() const { return (unsigned)strlen(p); }
  char operator[](unsigned i) const { return p[i]; }
  const char *c_str() const { return p; }
};

// Templates, NOT the macros the real Arduino.h uses. As macros these collide
// head-on with the standard headers this harness needs.
template <typename T> static inline T min(T a, T b) { return a < b ? a : b; }
template <typename T> static inline T max(T a, T b) { return a > b ? a : b; }
#ifndef _BV
#define _BV(b) (1UL << (b))
#endif

// Adafruit_GFX::rotatePoint() uses these.
#define radians(d) ((d) * (float)M_PI / 180.0f)
#define degrees(r) ((r) * 180.0f / (float)M_PI)

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return 1; }
inline int analogRead(uint8_t) { return 0; }
inline uint32_t esp_random() { return 0; }

// A CONTROLLABLE CLOCK, unlike the layout prover's constant 0. settings_tick()
// is a deferred write on a 3 s timer, and a test that cannot advance time
// cannot tell "the write was deferred" from "the write never happened", which
// is exactly the distinction worth proving.
extern unsigned long host_millis;
inline unsigned long millis() { return host_millis; }
inline unsigned long micros() { return host_millis * 1000UL; }
// The EEPROM model acknowledges immediately, so ee_wait_ready()'s delay(1) is
// never reached on the happy path. It still has to exist and still has to move
// the clock, or a write-retry loop would spin against a frozen millis().
inline void delay(unsigned long ms) { host_millis += ms; }
inline void delayMicroseconds(unsigned int) {}

// Silent by default. The store is deliberately chatty on the serial console  - 
// that narration is a feature on the device and noise in a test harness, so it
// is swallowed unless a test asks to see it.
extern bool host_serial_quiet;
class HostSerial : public Print {
public:
  size_t write(uint8_t c) override {
    if (!host_serial_quiet) fputc(c, stdout);
    return 1;
  }
  void begin(unsigned long) {}
  void setTxTimeoutMs(unsigned long) {}
  int available() { return 0; }
  int read() { return -1; }
};
extern HostSerial Serial;
