// hw.h, the RTC, the room sensors and the light sensor.
#pragma once
#include <Arduino.h>
#include "../config.h"

struct RtcTime { uint16_t year; uint8_t mon, day, hour, min, sec; bool ok; };

void    rtc_begin();
RtcTime rtc_read();
// The CACHED time. rtc_read() is a 7-byte I2C transaction; calling it once per
// loop() pass, which is what the main loop naturally wants to do, runs about
// a thousand transactions a second and starves every other device on the bus.
// A clock changes once a second, so the cache refreshes at 10 Hz, which is
// still twenty times finer than anything on screen.
// BY VALUE, not by reference. The main loop holds this across ui_input(),
// which can call rtc_now() again and refresh the cache in place, the holder's
// `t` would change underneath it mid-pass.
RtcTime rtc_now(uint32_t now_ms);
void    rtc_invalidate();
void    rtc_write_time(uint16_t y, uint8_t mo, uint8_t d,
                       uint8_t h, uint8_t mi, uint8_t s);
// OSF (bit 7 of status register 0x0F) is THE battery test. The DS3231 sets it
// whenever its oscillator has stopped, which happens the moment it loses both
// VCC and VBAT. Clear it, set the time, pull power, plug back in: if the coin
// cell is working the flag is still clear and the clock kept counting through
// the dark. There is no register that reports battery voltage, this behaviour
// IS the measurement.
bool    rtc_osf();
// The CACHED flag. rtc_osf() is two I2C transactions and the main loop wants
// it every pass to drive the fault LED, for a bit that changes about once a
// year. Refreshed alongside the temperature conversion.
bool    rtc_osf_cached();
void    rtc_clear_osf();
// The die temperature conversion takes up to ~125 ms, so it is a state machine
// rather than a busy-wait: trigger, return, collect on a later pass.
void    rtc_temp_trigger();
void    rtc_temp_poll();
float   rtc_temp_c();

bool    sht_read(float &t_c, float &rh);

// Sampled with the LEDs blanked, so it reads the ROOM and not our own light.
void     light_sample();
uint16_t light_raw();
uint32_t light_mv();
