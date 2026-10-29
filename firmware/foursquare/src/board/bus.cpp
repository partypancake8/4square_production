#include "bus.h"
#include <Wire.h>

//     J1 top-left  HOURS  ch2  |  J2 top-right    MINUTES ch6
//     J3 bottom-left DAY   ch1  |  J4 bottom-right DATE    ch7
const uint8_t OLED_CH[N_SCREENS] = {2, 6, 1, 7};
const char *const OLED_NAME[N_SCREENS] = {"top-left", "top-right",
                                          "bottom-left", "bottom-right"};

static uint32_t errs[DEV_COUNT] = {0, 0, 0, 0, 0};
static const char *const DEV_NAME[DEV_COUNT] = {"mux", "rtc", "sht", "eeprom",
                                                "oled"};
static uint8_t  cur_ch = 0xFF;
// Four consecutive failures on one device. One is a NAK; four is a fault.
static const uint32_t FAULT_AT = 4;

void bus_begin() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);          // safe: R1/R2 bus pull-ups fitted 2026-08-03
  // THE FREEZE FIX. Without a timeout an I2C transaction waits forever for a
  // device that never releases the bus and the whole sketch stops dead  - 
  // which is exactly what "it froze" looks like from the outside.
  //
  // 25 ms, not 50. This is per-transaction, and disp_rescan() issues ~165 of
  // them, at 50 ms a fully wedged bus takes 8 s, which is a guaranteed panic
  // against the 5 s task watchdog. 25 ms is still 40x any legitimate
  // transaction at 400 kHz, including the SHT31's clock stretching.
  //
  // (The core already defaults _timeOutMillis to 50, so this line changed
  // nothing before. It is kept as an explicit pin against a core upgrade.)
  Wire.setTimeOut(25);
  cur_ch = 0xFF;
}

void mux_select(uint8_t ch) {
  if (ch == cur_ch) return;       // the mux is the busiest device on the bus
  Wire.beginTransmission(ADDR_MUX);
  Wire.write((uint8_t)(1 << ch));
  if (Wire.endTransmission() != 0) {
    // L0 of the ladder: retry once. Most errors really are a single NAK, and
    // bus.h has documented this retry for a while without it existing.
    Wire.beginTransmission(ADDR_MUX);
    Wire.write((uint8_t)(1 << ch));
    if (Wire.endTransmission() != 0) {
      bus_note_error(DEV_MUX);
      cur_ch = 0xFF;
      return;
    }
  }
  bus_note_ok(DEV_MUX);
  cur_ch = ch;
}

void mux_off() {
  Wire.beginTransmission(ADDR_MUX);
  Wire.write((uint8_t)0x00);
  Wire.endTransmission();
  cur_ch = 0xFF;
}

bool mux_verify(uint8_t ch) {
  mux_select(ch);
  if (Wire.requestFrom((uint8_t)ADDR_MUX, (uint8_t)1) != 1) return false;
  return Wire.read() == (uint8_t)(1 << ch);
}

// Nine SCL pulses with SDA released. A slave that was interrupted mid-byte
// finishes clocking that byte out and then releases SDA; the STOP that follows
// puts every device back into its idle state.
static void unstick() {
  Wire.end();
  // OPEN DRAIN, NOT PUSH-PULL. A plain OUTPUT on the C3 is push-pull, so
  // driving SCL HIGH would fight the open-drain FET of the very slave that is
  // holding the line down, which is the only condition this function exists
  // to handle. The fitted R1/R2 pull-ups supply the highs; contention becomes
  // impossible.
  pinMode(PIN_SDA, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_SDA, HIGH);                  // released, pull-up holds it
  digitalWrite(PIN_SCL, HIGH);
  delayMicroseconds(5);

  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
    if (digitalRead(PIN_SDA) == HIGH) break;    // it let go
  }

  // A REAL STOP: SDA must go low while SCL is LOW, then SCL high, then SDA
  // high. Pulling SDA low while SCL was already high, which is what the
  // previous version did after the loop broke, is a START condition, not a
  // STOP.
  digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
  digitalWrite(PIN_SDA, LOW);  delayMicroseconds(5);
  digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
  digitalWrite(PIN_SDA, HIGH); delayMicroseconds(5);

  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
}

bool bus_recover() {
  Serial.print("# I2C recovery, sda=");
  Serial.print(digitalRead(PIN_SDA));
  Serial.print(" scl="); Serial.println(digitalRead(PIN_SCL));
  unstick();
  bus_begin();
  mux_off();
  // Did it work? The mux is always present, so it is the probe.
  Wire.beginTransmission(ADDR_MUX);
  bool ok = (Wire.endTransmission() == 0);
  Serial.println(ok ? "# I2C recovered" : "# I2C STILL STUCK");
  if (ok) for (uint8_t i = 0; i < DEV_COUNT; i++) errs[i] = 0;
  // NOTE: the four SSD1306 controllers may be sitting mid-command with a
  // half-consumed GDDRAM pointer after being clocked past. The caller must
  // re-initialise them, see the disp_health_check() call in loop(), which
  // notices they stopped answering and does exactly that.
  return ok;
}

void bus_note_error(uint8_t dev) { if (dev < DEV_COUNT) errs[dev]++; }
void bus_note_ok(uint8_t dev)    { if (dev < DEV_COUNT) errs[dev] = 0; }
uint32_t bus_error_count(uint8_t dev) { return dev < DEV_COUNT ? errs[dev] : 0; }

uint32_t bus_error_count() {
  uint32_t m = 0;
  for (uint8_t i = 0; i < DEV_COUNT; i++) if (errs[i] > m) m = errs[i];
  return m;
}

bool bus_faulting() { return bus_error_count() >= FAULT_AT; }

const char *bus_worst_device() {
  uint8_t w = 0;
  for (uint8_t i = 1; i < DEV_COUNT; i++) if (errs[i] > errs[w]) w = i;
  return DEV_NAME[w];
}
