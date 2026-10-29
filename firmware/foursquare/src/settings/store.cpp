#include "store.h"
#include <Wire.h>
#include <string.h>

// ============================================================ raw 24LC256 ==
// Word-addressed: two address bytes, MSB first, then data. Reads are a write
// of the address followed by a repeated start.

static const uint16_t EE_SIZE = 32768;
static const uint8_t  EE_PAGE = 64;

bool ee_present() {
  Wire.beginTransmission(ADDR_EEPROM);
  return Wire.endTransmission() == 0;
}

bool ee_read(uint16_t addr, uint8_t *buf, uint16_t len) {
  if ((uint32_t)addr + len > EE_SIZE) return false;
  while (len) {
    // Wire's RX buffer is finite; 32 bytes is the portable chunk and costs
    // nothing here because the whole store is read once at boot.
    uint8_t n = (uint8_t)(len > 32 ? 32 : len);
    Wire.beginTransmission(ADDR_EEPROM);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;   // repeated start
    if (Wire.requestFrom((uint8_t)ADDR_EEPROM, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    buf  += n;
    addr = (uint16_t)(addr + n);
    len  = (uint16_t)(len - n);
  }
  return true;
}

// ACK POLLING, not a fixed delay. After a write the part stops acknowledging
// until its internal cycle finishes, typically 3-5 ms but specified up to 5
// and longer when warm. Polling returns as soon as it is genuinely ready and
// cannot silently corrupt the next write by being optimistic.
static bool ee_wait_ready() {
  for (uint8_t i = 0; i < 100; i++) {
    Wire.beginTransmission(ADDR_EEPROM);
    if (Wire.endTransmission() == 0) return true;
    delay(1);
  }
  return false;
}

bool ee_write_page(uint16_t addr, const uint8_t *buf, uint8_t len) {
  if (len == 0 || len > EE_PAGE) return false;
  // A page write that crosses a page boundary WRAPS to the start of the same
  // page and quietly overwrites what is already there. Refuse instead.
  if ((addr % EE_PAGE) + len > EE_PAGE) return false;
  if ((uint32_t)addr + len > EE_SIZE) return false;
  Wire.beginTransmission(ADDR_EEPROM);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.write(buf, len);
  if (Wire.endTransmission() != 0) return false;
  return ee_wait_ready();
}

// ========================================================== settings store ==
// A WEAR-LEVELLED RING, not a fixed address. The part is rated 1M writes per
// page; a settings block rewritten in place would spend that budget on one
// page. Rotating across 128 pages multiplies it by 128, which for a device
// whose settings change by hand is effectively unlimited.
//
// Slot layout, exactly one 64-byte page:
//     u32 seq    monotonic; 0xFFFFFFFF means never written
//     u16 len    payload bytes
//     u16 crc    CRC16-CCITT over seq, len and payload
//     u8  payload[56]
// A torn write leaves a bad CRC on the newest slot, and load falls back to the
// previous one, so a power cut mid-save costs the last change, never the
// whole configuration.

static const uint16_t SLOT_SZ  = 64;
static const uint16_t N_SLOTS  = 128;      // first 8 KB; the rest is reserved
static const uint8_t  HDR_SZ   = 8;
static const uint8_t  MAX_PAY  = SLOT_SZ - HDR_SZ;
// Growing Settings past one EEPROM page would silently truncate every save.
static_assert(sizeof(Settings) <= MAX_PAY,
              "Settings no longer fits one 64-byte EEPROM page");

Settings cfg;
static uint32_t cur_seq  = 0;
static int16_t  cur_slot = -1;
static bool     dirty = false;
static uint32_t dirty_at = 0;
static uint16_t n_writes = 0;
static bool     ee_ok = false;

// Deferred by this much. Holding UP on a numeric setting fires many changes a
// second; writing each one would spend the ring in an afternoon and stall the
// UI for 5 ms a step. One write, three seconds after you stop fiddling.
static const uint32_t SAVE_DELAY_MS = 3000;

static uint16_t crc16(const uint8_t *d, uint16_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*d++) << 8;
    for (uint8_t i = 0; i < 8; i++)
      c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}


static bool slot_read(uint16_t i, Settings &out, uint32_t &seq) {
  uint8_t raw[SLOT_SZ];
  if (!ee_read((uint16_t)(i * SLOT_SZ), raw, SLOT_SZ)) return false;
  seq = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
        ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
  if (seq == 0xFFFFFFFFUL) return false;
  uint16_t len = (uint16_t)raw[4] | ((uint16_t)raw[5] << 8);
  uint16_t crc = (uint16_t)raw[6] | ((uint16_t)raw[7] << 8);
  if (len == 0 || len > MAX_PAY) return false;
  uint8_t tmp[SLOT_SZ];
  memcpy(tmp, raw, 6);                    // seq + len participate in the CRC
  memcpy(tmp + 6, raw + HDR_SZ, len);
  if (crc16(tmp, (uint16_t)(6 + len)) != crc) return false;
  // A record written by an older, smaller version still loads: defaults first,
  // then overlay however many bytes it actually stored. Growing the struct
  // therefore does not orphan a user's settings.
  settings_defaults(out);
  memcpy(&out, raw + HDR_SZ, len > sizeof(Settings) ? sizeof(Settings) : len);
  out.version = CFG_VERSION;
  settings_sanitize(out);
  return true;
}

bool settings_load() {
  settings_defaults(cfg);
  ee_ok = ee_present();
  if (!ee_ok) {
    Serial.println("# EEPROM U3 not found, settings are defaults, not saved");
    return false;
  }
  // ONE PASS. Read every header once into RAM, then work from that.
  // The previous version re-walked all 128 slots for every corrupt record it
  // found, 16,384 header reads worst case, in setup(), before the watchdog is
  // even running. A marginal bus turned that into minutes of apparently dead
  // board with no serial output.
  uint32_t seqs[N_SLOTS];
  bool     read_failed = false;
  for (uint16_t i = 0; i < N_SLOTS; i++) {
    uint8_t h[4];
    bool got = false;
    // RETRY, and distinguish a bus error from an erased slot. Treating a
    // transient NAK as "blank" can make load pick an OLDER record; save then
    // writes to the slot after THAT one, which may already hold a higher
    // sequence number that wins on every future boot. The ring's monotonicity
    // is broken permanently, and the user's symptom is "my settings keep
    // reverting".
    for (uint8_t attempt = 0; attempt < 3 && !got; attempt++)
      got = ee_read((uint16_t)(i * SLOT_SZ), h, 4);
    if (!got) { read_failed = true; seqs[i] = 0xFFFFFFFFUL; continue; }
    seqs[i] = (uint32_t)h[0] | ((uint32_t)h[1] << 8) |
              ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
  }
  if (read_failed) {
    // Refuse to save rather than risk corrupting the ring from a bad read.
    ee_ok = false;
    Serial.println("# EEPROM reads failing, running on defaults, NOT saving");
    return false;
  }

  // Walk the sequence numbers downward until one passes its CRC. Normally that
  // is the first try; after a torn write it is the second.
  for (;;) {
    uint32_t best = 0;
    int16_t  best_i = -1;
    for (uint16_t i = 0; i < N_SLOTS; i++) {
      if (seqs[i] == 0xFFFFFFFFUL) continue;
      if (best_i < 0 || seqs[i] > best) { best = seqs[i]; best_i = (int16_t)i; }
    }
    if (best_i < 0) break;

    Settings s;
    uint32_t seq;
    if (slot_read((uint16_t)best_i, s, seq) && seq == best) {
      cfg = s;
      cur_seq = seq;
      cur_slot = best_i;
      Serial.print("# settings loaded from slot "); Serial.print(best_i);
      Serial.print(" seq "); Serial.println(seq);
      return true;
    }
    Serial.print("# settings slot "); Serial.print(best_i);
    Serial.println(" is corrupt, falling back");
    seqs[best_i] = 0xFFFFFFFFUL;          // drop it and try the next-highest
  }
  Serial.println("# EEPROM has no valid settings yet, using defaults");
  return false;
}

bool settings_save() {
  if (!ee_ok) return false;
  uint8_t raw[SLOT_SZ];
  memset(raw, 0xFF, sizeof(raw));
  uint32_t seq = cur_seq + 1;
  // 0xFFFFFFFF is the "never written" sentinel. Unreachable in practice
  // (endurance caps lifetime writes far below it) but a record that wrote it
  // would make itself invisible.
  if (seq == 0xFFFFFFFFUL) seq = 1;
  uint16_t len = (uint16_t)sizeof(Settings);
  if (len > MAX_PAY) len = MAX_PAY;      // compile-time truth, belt and braces
  raw[0] = (uint8_t)seq;        raw[1] = (uint8_t)(seq >> 8);
  raw[2] = (uint8_t)(seq >> 16); raw[3] = (uint8_t)(seq >> 24);
  raw[4] = (uint8_t)len;        raw[5] = (uint8_t)(len >> 8);
  memcpy(raw + HDR_SZ, &cfg, len);
  uint8_t tmp[SLOT_SZ];
  memcpy(tmp, raw, 6);
  memcpy(tmp + 6, raw + HDR_SZ, len);
  uint16_t crc = crc16(tmp, (uint16_t)(6 + len));
  raw[6] = (uint8_t)crc; raw[7] = (uint8_t)(crc >> 8);

  uint16_t slot = (uint16_t)((cur_slot + 1) % N_SLOTS);
  if (!ee_write_page((uint16_t)(slot * SLOT_SZ), raw, SLOT_SZ)) {
    Serial.println("# EEPROM write FAILED");
    return false;
  }
  cur_seq  = seq;
  cur_slot = (int16_t)slot;
  n_writes++;
  Serial.print("# settings saved slot "); Serial.print(slot);
  Serial.print(" seq "); Serial.println(seq);
  return true;
}

void settings_mark_dirty() { dirty = true; dirty_at = millis(); }

void settings_tick() {
  if (!dirty) return;
  if (millis() - dirty_at < SAVE_DELAY_MS) return;
  dirty = false;
  settings_save();
}

uint16_t settings_writes() { return n_writes; }

// The widget/style name tables and widget_allows() live in faces.cpp, they
// describe what a widget IS, which is a rendering fact, and keeping them there
// means the host-side prover links them without dragging in Wire.
