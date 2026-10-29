// bus.h, the shared I2C bus, the mux in front of the screens, and recovery.
#pragma once
#include <Arduino.h>
#include "../config.h"

// Mux channels come from the NETLIST (board.py's OLED_CHANNELS), never from
// silk and never from memory.
extern const uint8_t OLED_CH[N_SCREENS];
extern const char *const OLED_NAME[N_SCREENS];

// Light-sensor calibration, in raw ADC counts. MEASURED with the LEDs blanked
// on 2026-08-07, a normally lit room reads ~86. The 200-ish numbers an
// earlier version used came from readings contaminated by our own LED and put
// a lit room at a third of full brightness.
static const uint16_t LIGHT_RAW_LO = 5;      // at or below: fully dimmed
static const uint16_t LIGHT_RAW_HI = 110;    // at or above: full brightness

void bus_begin();

// The PCA9548A is transparent to the main bus: the RTC, SHT and EEPROM stay
// reachable whatever channel is open. A channel is still closed after every
// screen write so the bus sits in one known state when something misbehaves.
void mux_select(uint8_t ch);
void mux_off();

// Returns true if the mux acknowledged and read back the channel we asked
// for. A mux that answers but has not switched is a silent, extremely
// confusing failure, all four screens show whatever the last channel had.
bool mux_verify(uint8_t ch);

// I2C RECOVERY LADDER.
//
// The previous firmware recovered from a bus fault with Wire.end() then
// Wire.begin(). THAT CANNOT WORK for the fault that actually happens: a slave
// interrupted mid-byte holds SDA low and keeps holding it, and re-initialising
// the master does not make it let go. The only thing that does is clocking SCL
// until the slave finishes the byte it thinks it is transmitting.
//
// The SHT31 is read in clock-stretch mode, so it holds SCL by design and is
// the part most able to wedge the bus if a joint is marginal.
//
//   L0  retry once, most errors are a single NAK
//   L1  nine SCL pulses + a STOP, bit-banged: frees a stuck slave
//   L2  full Wire re-init
//   L3  reset the mux by deselecting all channels
//   L4  give up for this cycle and report a fault to the LEDs
bool bus_recover();

// PER DEVICE, not one global counter. With a single counter shared by
// everything, a DS3231 with a marginal joint could fail every second forever
// and never trip recovery, because the SHT31 succeeding in between reset the
// count to zero. The fault the counter existed to catch was the one it could
// not see.
enum BusDev : uint8_t { DEV_MUX = 0, DEV_RTC, DEV_SHT, DEV_EE, DEV_OLED,
                        DEV_COUNT };
void     bus_note_error(uint8_t dev);
void     bus_note_ok(uint8_t dev);
uint32_t bus_error_count(uint8_t dev);
// The worst device's count, for the serial line and the info page.
uint32_t bus_error_count();
// True when ANY single device has failed repeatedly.
bool     bus_faulting();
const char *bus_worst_device();
