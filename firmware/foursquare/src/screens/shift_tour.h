#pragma once
#include <stdint.h>

// THE ANTI-BURN-IN SHIFT PATH, IN A HEADER SO IT CAN BE PROVEN.
//
// This is here rather than inside display.cpp for one reason: display.cpp
// cannot be compiled on the host (it pulls in Wire, the SSD1306 driver and the
// mux), so anything defined in it can only ever be reasoned about. firmware B's
// shift was reasoned about for months and was a placebo the whole time. The
// path math lives here instead, the host prover compiles THIS header, and the
// properties below are asserted against the code that actually ships.
//
// The path: a serpentine (boustrophedon) walk of the (2Ax+1) x (2Ay+1) grid of
// pixel offsets, traversed forward and then backward. Three properties matter
// and all are checked in tools/layoutcheck/layoutcheck.cpp:
//
//   UNIFORM , every offset is visited exactly twice per cycle, so residency is
//              exactly uniform rather than uniform in expectation. This is what
//              lets the prover model a pixel's duty as a plain uniform box sum.
//   ADJACENT, consecutive offsets differ by at most 1 px on one axis, so the
//              image never visibly jumps. The forward/backward traversal exists
//              precisely to avoid the jump home: an odd x odd grid admits a
//              Hamiltonian PATH but no Hamiltonian CYCLE, by a parity argument.
//   CENTRED , the mean offset over a cycle is exactly (0,0), so the wander
//              moves the image without moving where its centre sits. Without
//              that, "this layout is centred" would not mean anything.
//
// THE ENVELOPE IS RECTANGULAR, NOT SQUARE, and that is a measurement talking.
// Only 60 of the 64 rows are visible through the case (see slot_bias.h), and
// the budget has to cover the safe area plus twice the vertical wander plus
// nothing else: 52 + 2*4 = 60 exactly. Horizontally all 128 columns are
// visible, so x keeps the full +/-6. Forcing y to 6 as well would have cost
// the safe area 4 rows and every existing layout with it.

// Number of ticks in one full cycle.
static inline uint16_t shift_tour_len(int16_t ax, int16_t ay) {
  const uint16_t sw = (uint16_t)(2 * ax + 1), sh = (uint16_t)(2 * ay + 1);
  return (uint16_t)(2u * sw * sh);
}

// The offset at tick i. i may be any value; it is reduced into the cycle.
static inline void shift_tour_at(uint16_t i, int16_t ax, int16_t ay,
                                 int8_t *x, int8_t *y) {
  const uint16_t sw = (uint16_t)(2 * ax + 1), sh = (uint16_t)(2 * ay + 1);
  const uint16_t m   = (uint16_t)(sw * sh);
  const uint16_t len = (uint16_t)(2u * m);
  i = (uint16_t)(i % len);

  // Second half of the cycle retraces the first, reversed.
  uint16_t k   = (i < m) ? i : (uint16_t)(len - 1 - i);
  uint16_t row = (uint16_t)(k / sw);
  uint16_t col = (uint16_t)(k % sw);
  if (row & 1) col = (uint16_t)(sw - 1 - col);     // odd rows run back

  *x = (int8_t)((int16_t)col - ax);
  *y = (int8_t)((int16_t)row - ay);
}
