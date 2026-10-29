// A host-side model of the 24LC256 on the wire, not of store.cpp.
//
// THIS IS THE ONE PLACE THE PROVER IS ALLOWED TO REIMPLEMENT ANYTHING, and
// what it reimplements is the CHIP, the part we do not have on the PC, never
// the firmware. store.cpp is compiled from the tree and run against this
// exactly as it runs against U3. If a test passes here and fails on hardware,
// the bug is in this file's understanding of the datasheet, and that is a much
// smaller thing to be wrong about than a reimplemented ring would be.
//
// Modelled deliberately, because each of these has bitten this project or is
// one line away from doing so:
//   - an erased part reads 0xFF everywhere, which is what makes 0xFFFFFFFF a
//     usable "never written" sentinel;
//   - a page write that runs past a 64-byte page boundary WRAPS to the start
//     of that same page and silently overwrites it (datasheet, and the reason
//     ee_write_page() refuses one);
//   - a read is an address write with a repeated start, so a read that is not
//     preceded by a latched address reads from wherever the pointer was left;
//   - the part NAKs while its ~5 ms write cycle is in progress, which is what
//     ee_wait_ready()'s ACK polling is for.
// It does NOT model bus arbitration, clock stretching or the mux, none of
// which store.cpp can see.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>

static const uint16_t EESIM_SIZE  = 32768;
static const uint16_t EESIM_PAGE  = 64;
static const uint16_t EESIM_PAGES = EESIM_SIZE / EESIM_PAGE;

// ---- the simulated part, and the knobs a test turns ------------------------
inline uint8_t  eesim[EESIM_SIZE];
// Write cycles spent per page. The wear-levelling claim is "the ring spreads
// writes across 128 pages"; that claim is only checkable if somebody counts.
inline uint32_t eesim_page_writes[EESIM_PAGES];
// Is U3 in its socket at all? A false here makes every transaction NAK, which
// is the "EEPROM not found" path settings_load() has to survive.
inline bool     eesim_present = true;
// Fail the next N read transactions, then behave. Reproduces a marginal bus
// rather than an absent chip, a different code path in settings_load(), and
// the one whose failure mode is "my settings keep reverting".
inline uint16_t eesim_read_faults = 0;
// Refuse writes, as a write-protected or worn-out part would.
inline bool     eesim_write_enabled = true;

inline void eesim_reset() {
  memset(eesim, 0xFF, sizeof(eesim));      // an erased EEPROM, not a zeroed one
  memset(eesim_page_writes, 0, sizeof(eesim_page_writes));
  eesim_present       = true;
  eesim_read_faults   = 0;
  eesim_write_enabled = true;
}

inline uint32_t eesim_total_writes() {
  uint32_t n = 0;
  for (uint16_t i = 0; i < EESIM_PAGES; i++) n += eesim_page_writes[i];
  return n;
}

// ---- the wire ---------------------------------------------------------------
class TwoWire {
  uint8_t  txbuf[80];
  uint16_t txn = 0;
  uint8_t  rxbuf[80];
  uint16_t rxn = 0, rxi = 0;
  uint16_t ptr = 0;        // the part's internal address pointer

public:
  void begin() {}
  void begin(int, int) {}
  void setClock(uint32_t) {}
  void end() {}

  void beginTransmission(uint8_t) { txn = 0; }
  size_t write(uint8_t b) {
    if (txn < sizeof(txbuf)) txbuf[txn++] = b;
    return 1;
  }
  size_t write(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) write(b[i]);
    return n;
  }

  // 0 = the part acknowledged. 2 = address NAK, which is both "no chip" and
  // "chip busy finishing a write", the caller cannot tell them apart on real
  // hardware either, which is exactly why ee_wait_ready() polls rather than
  // waits a fixed time.
  uint8_t endTransmission(bool = true) {
    if (!eesim_present) { txn = 0; return 2; }

    if (txn >= 2) {
      ptr = (uint16_t)((txbuf[0] << 8) | txbuf[1]);
      if (txn > 2) {
        if (!eesim_write_enabled) { txn = 0; return 3; }
        // PAGE WRAP, modelled. Data past the end of the page the write started
        // in comes back around to that page's first byte. store.cpp refuses to
        // issue such a write; this is here so that refusal is load-bearing and
        // not decorative.
        uint16_t page_base = (uint16_t)(ptr & ~(EESIM_PAGE - 1));
        uint16_t off       = (uint16_t)(ptr - page_base);
        for (uint16_t i = 0; i < txn - 2; i++) {
          eesim[page_base + ((off + i) % EESIM_PAGE)] = txbuf[2 + i];
        }
        eesim_page_writes[page_base / EESIM_PAGE]++;
        ptr = (uint16_t)(page_base + ((off + (txn - 2)) % EESIM_PAGE));
      }
    } else if (txn == 1) {
      ptr = txbuf[0];
    }
    txn = 0;
    return 0;
  }

  uint8_t requestFrom(uint8_t, uint8_t n) {
    rxn = rxi = 0;
    if (!eesim_present) return 0;
    if (eesim_read_faults) { eesim_read_faults--; return 0; }
    for (uint8_t i = 0; i < n && i < sizeof(rxbuf); i++) {
      rxbuf[rxn++] = eesim[ptr];
      // The pointer rolls over the WHOLE part on a sequential read, not the
      // page, reads have no page boundary, only writes do.
      ptr = (uint16_t)((ptr + 1) % EESIM_SIZE);
    }
    return (uint8_t)rxn;
  }

  int available() { return (int)(rxn - rxi); }
  int read() { return rxi < rxn ? rxbuf[rxi++] : -1; }
};

inline TwoWire Wire;
