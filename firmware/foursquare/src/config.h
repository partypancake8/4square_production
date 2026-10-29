// config.h — every pin, address and tunable in one place.
//
// Nothing here is guessed. Pins and mux channels come from the NETLIST
// (board.py), never from silk and never from memory — the rev O silk lied
// twice and cost three parts, see hardware/HARDWARE.md.
#pragma once
#include <stdint.h>

// ---- pins -----------------------------------------------------------------
#define PIN_SDA 5
#define PIN_SCL 6
#define PIN_LDR 4          // ADC1_CH4, R7 phototransistor (needs R4 1k fitted)
#define PIN_LED 7          // WS2812 data, through the U6 74AHCT1G125 5V buffer

// SW1..SW4, each via a 1k series resistor (R8-R11) to a switch to GND.
// None are C3 strap pins (2/8/9), so a stuck button cannot break boot.
#define BTN_MODE  0
#define BTN_SET   1
#define BTN_UP    3
#define BTN_DOWN  10

// ---- I2C addresses --------------------------------------------------------
#define ADDR_MUX    0x70   // U1 PCA9548A, A0-A2 grounded
#define ADDR_OLED   0x3C   // all four modules, one per mux channel
#define ADDR_RTC    0x68   // U4 DS3231     — MAIN bus, not muxed
#define ADDR_SHT    0x44   // U5 SHT31      — MAIN bus, not muxed
#define ADDR_EEPROM 0x50   // U3 24LC256    — MAIN bus, not muxed. A0-A2 and
                           // WP all grounded, so writes are enabled.

// ---- the four screens -----------------------------------------------------
//     J1 top-left  HOURS  ch2  |  J2 top-right    MINUTES ch6
//     J3 bottom-left DAY   ch1  |  J4 bottom-right DATE    ch7
// from board.py's OLED_CHANNELS. Panels are ALL WHITE — no yellow top band, so
// all 64 rows are usable and nothing has to dodge a seam.
#define N_SCREENS 4
#define SCR_W 128
#define SCR_H 64

// Slot indices, by physical position. Slot 0 is top-left.
enum Slot { SLOT_TL = 0, SLOT_TR = 1, SLOT_BL = 2, SLOT_BR = 3 };

// ---- LEDs -----------------------------------------------------------------
// D2, D4, D5 are fitted and working. They were reverse-mounted on the rev O
// build (the GL5050's chamfer marks pin 3 / GND, not pin 1) and the user
// hot-air-gun rotated all three on 2026-08-07. Any design note that assumes
// "no LEDs fitted" is stale.
#define N_LEDS_FITTED 3
#define N_LEDS 8           // chain is driven longer than it is populated;
                           // surplus data simply falls out of the last DOUT

// ---- timing ---------------------------------------------------------------
static const uint32_t BTN_DEBOUNCE_MS  = 25;
static const uint32_t BTN_LONG_MS      = 600;    // held this long = long press
static const uint32_t BTN_REPEAT_MS    = 140;    // auto-repeat interval after
static const uint32_t BTN_REPEAT_FIRST = 450;    //   this initial delay

static const uint32_t SHT_PERIOD_MS    = 1000;
static const uint32_t RTC_PERIOD_MS    = 1000;
// 1000, not 200. The blank below grew from 4 ms to 25 ms when it turned out
// 4 ms was not long enough for the phototransistor to decay — but the period
// did not grow with it. 25 ms in every 200 is a 12.5% blackout at 5 Hz, which
// is squarely in the most perceptible flicker band, on indicators designed to
// be read across a dark bedroom. At 1 Hz it is 2.5% and invisible, and "the
// room got darker" was never a 200 ms event anyway.
static const uint32_t LIGHT_PERIOD_MS  = 1000;
static const uint32_t LED_FRAME_MS     = 16;     // ~60 fps, INTENDED not actual
static const uint32_t UI_TICK_MS       = 50;

// ---- LED cadence and the light blank ---------------------------------------
// LED_FRAME_MS above is what loop() ASKS for. What it delivers is another
// matter: led_tick() runs at most once per loop() pass and loop() serialises
// with ui_paint(), which on the animation page blocks ~92 ms pushing four 1 KB
// frames at 400 kHz. The real LED rate there is nearer 10 Hz. Every gait in
// leds.cpp is therefore a function of wall time and does not care — but the
// temporal dither is a 60 fps mechanism and at 10 Hz it turns a constant colour
// into a 10 Hz square wave on the bottom bit. Above this MEASURED interval it
// degrades to plain rounding instead. Two design frames.
static const uint32_t LED_DITHER_MAX_DT_MS = 33;
// How long the strip takes to ease out of, and back into, the light-sensor
// blank. The blank itself is a hard requirement (LIGHT_BLANK_MS below); its
// EDGES are not, and a hard edge once a second is a black notch punched into
// every animation. Six steps either side.
static const uint32_t LED_BLANK_RAMP_MS    = 24;

// 25 ms, not 4. MEASURED 2026-08-07: the same room reads raw 78-92 with the
// LEDs dark and 172-192 with a red LED lit — our own LED roughly DOUBLES the
// reading through a 4 ms blank. R7 sits 9.5 mm from D2 on the same board face
// and a phototransistor needs longer than 4 ms to decay. Every auto-dim
// calibration made against the contaminated numbers was wrong.
static const uint32_t LIGHT_BLANK_MS   = 25;

// HOW OFTEN THE BLANK IS ALLOWED TO COST ANYTHING. When the strip is dark there
// is nothing to blank and light_sample() samples freely at LIGHT_PERIOD_MS —
// which, since IDLE IS DARK, is nearly always. When it is LIT the sample costs
// a ramp down, the settle and a ramp up, and doing that once a second is a
// visible dip in an animation somebody is looking at. Room light is not a 1 Hz
// event; 4 s of staleness on the auto-dim curve is not perceptible and the dip
// is. The reading itself is unchanged either way: still taken with the LEDs
// genuinely off, still after the full LIGHT_BLANK_MS settle.
static const uint32_t LIGHT_BLANK_LIT_MIN_MS = 4000;

// ---- WiFi -----------------------------------------------------------------
// 20 s, not 5. A 5 s retry aborts a join that is still in progress, so it
// livelocks and never connects at all. Paid for on the older C3 clock.
static const uint32_t WIFI_RETRY_MS = 20000;
static const char OTA_HOST[] = "foursquare-revo";
