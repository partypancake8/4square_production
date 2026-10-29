// A host-side stand-in for Arduino.h, just wide enough to compile the REAL
// Adafruit_GFX and the REAL firmware layout code on a PC.
//
// WHY THIS IS A SHIM AND NOT A REIMPLEMENTATION. The whole value of the layout
// prover is that it exercises the exact source that ships. Anything reimplemented
// here is something the prover could be wrong about, so this file contains no
// drawing code, no font, and no geometry. It provides Print (so GFX can inherit
// from it), the PROGMEM macros (so the font tables read correctly) and nothing
// else. Adafruit_GFX.cpp and glcdfont.c are compiled from the library the device
// builds against.
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
// head-on with <algorithm> and <vector>, which the prover needs, and the
// three-argument std::min is a compile error rather than a subtle one, so this
// is a straight substitution with no behavioural difference for GFX's use.
template <typename T> static inline T min(T a, T b) { return a < b ? a : b; }
template <typename T> static inline T max(T a, T b) { return a > b ? a : b; }
#ifndef _BV
#define _BV(b) (1UL << (b))
#endif

// The firmware calls these from setup paths the prover never runs, but a few
// headers reference them. Cheap to satisfy, and keeps the shim honest about
// doing nothing.
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}
inline uint32_t esp_random() { return 0; }

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

// A Serial that goes to stdout, so a firmware Serial.print() left in a shared
// path does not break the host build.
class HostSerial : public Print {
public:
  size_t write(uint8_t c) override { fputc(c, stdout); return 1; }
  void begin(unsigned long) {}
  void setTxTimeoutMs(unsigned long) {}
  int available() { return 0; }
  int read() { return -1; }
};
extern HostSerial Serial;
