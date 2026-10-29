// demo.h — THE ONE PLACE THAT SAYS WHAT A DEMO BUILD IS.
//
// A demo build is the shelf firmware with the radio taken out and a few
// defaults leaned on. It is a BUILD FLAG, not a fork: every line below is
// inside `#ifdef DEMO_BUILD`, the shipping build compiles to exactly what it
// compiled to before, and there is no second copy of the firmware to keep in
// step. Turn it on with `-DDEMO_BUILD`:  firmware/build.sh demo
//
// THIS IS FIRMWARE C's COPY. The demo is built on C, not on D, because C is
// the firmware the device actually shipped with and the one whose grammar the
// user knows: EACH BUTTON OWNS A PAGE — MODE the clock, SET the readings, UP
// the stopwatch, DOWN the animations — and pressing a button again cycles that
// page's variants. D replaced that with a named picker, which is a better
// argument and the wrong product for a demo.
//
// WHY IT EXISTS. The device gets shown to people who are not going to wait
// through a join, and who will read a JOINING screen or a retry state as the
// product being broken. A demo build has no radio to fail: no association, no
// captive portal, no SNTP, no OTA server, no market fetch. Boot is the bus,
// the panels and the clock chip, and then it is running.
//
// WHAT IT IS NOT. It is not a different UI, not a different render path and
// not a different settings schema. Every gate that guards the shipping build
// guards this one, and the demo's own screens go through the same provers.
#pragma once

#ifdef DEMO_BUILD

// ---- 1. NO RADIO, AT ALL ---------------------------------------------------
// net_begin() is never called, so the WiFi task is never created, the driver
// is never initialised and g_st stays zeroed — which is NET_OFF, the state the
// rest of the firmware already knows how to render as "no network". Nothing
// downstream needed a special case: the sensor page's WiFi row, the LED
// column's join/AP states and the market fetch are all already conditioned on
// a mode this build simply never leaves.
//
// wifi_tick() is never called and WiFi.mode() is never reached, so the radio
// is never brought up at all. The OTA server is armed from inside wifi_tick(),
// so it goes with it.
#define DEMO_NO_RADIO 1

// ---- 2. THE DEFAULTS THE DEMO LEANS ON -------------------------------------
// These are applied TWICE and the second time is the one that matters.
//
// settings_defaults() sets them, which covers a board with no stored record.
// But the ring in the 24LC256 SURVIVES A FLASH — that is its whole point, and
// it means a board that has been used before comes up on ITS record, not on
// these, and would show Celsius at whatever brightness it was left on. So
// demo_force() below is called right after settings_load() and re-asserts the
// handful of fields the demo is actually promising. Everything else stays the
// user's.
//
// FAHRENHEIT. temp_unit 1 = F, and it is global — every temperature on the
// device follows it.
#define DEMO_TEMP_UNIT 1

// BRIGHTNESS, ON THE HIGH SIDE. 200 against the shipping 140.
//
// Read the anti-burn-in note before moving this: contrast is a linear segment
// CURRENT control and OLED half-life goes as roughly J^-1.4, so 140 is a
// panel-lifetime number, not a comfort setting. 200 is a deliberate trade of
// lifetime for a device that reads well across a table under office lighting,
// and it is defensible precisely because a demo unit does not run 24/7 for a
// year. Do not copy this number back into the shipping defaults.
#define DEMO_BRIGHT_DAY   200
#define DEMO_BRIGHT_NIGHT  70
// The LEDs get the same treatment for the same reason.
#define DEMO_LED_BRIGHT   230

// The radio is off, so the stored wifi_on must not sit at 1 and make the
// sensor page claim a radio that is not there.
#define DEMO_WIFI_ON 0

// ---- 3. QUOTES ARE BAKED IN ------------------------------------------------
// The stock ticker page is gone (UP is the stopwatch since 2026-10-15), but the
// showreel's STOCKS segment still shows SPX, NVDA, AAPL, BTC, from a baked
// table in app/ui.cpp (reel_quotes) on a deterministic 3 s walk so the segment
// looks live rather than frozen. Nothing fetches anything.
#define DEMO_BAKED_QUOTES 1

struct Settings;
// Re-assert the demo's promises over whatever the EEPROM ring handed back.
// Called from setup() immediately after settings_load(). A no-op in a
// shipping build, which does not compile it at all.
void demo_force(Settings &s);

#endif  // DEMO_BUILD
