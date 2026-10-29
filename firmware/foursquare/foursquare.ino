// 4square rev O — the clock.
//
// WHAT THIS IS
//   Four 128x64 OLEDs on a PCA9548A mux, a DS3231, an SHT31, a 24LC256, a
//   phototransistor and three RGB LEDs, on an ESP32-C3 SuperMini.
//
// HOW IT IS PUT TOGETHER, and why the shape matters
//   config.h      pins, addresses, tunables — one place, all from the NETLIST
//   bus.cpp       I2C, the mux, and a recovery ladder that can actually
//                 unstick a slave holding SDA low
//   store.cpp     the 24LC256, as a wear-levelled ring of settings records
//   display.cpp   THE ONLY CODE THAT CAN WRITE TO A PANEL. Anti-burn-in is
//                 applied here, so no drawing code can forget it or opt out
//   faces.cpp     what a clock face shows — pure, no hardware
//   pages.cpp     the four button pages — pure, no hardware
//   anim.cpp      48x48 animation frames — pure, no hardware
//   ui.cpp        buttons and page selection
//   leds.cpp      the status language
//   hw.cpp        RTC, SHT31, light
//
//   faces.cpp, pages.cpp and anim.cpp being PURE is what lets tools/layoutcheck
//   compile them on a PC against the real Adafruit_GFX and prove, pixel by
//   pixel, that no screen this firmware can draw puts two things in the same
//   place or draws where the burn-in shift would clip it.
//       firmware/tools/layoutcheck/build.sh      <- run this before flashing
//
// HOST CONTRACT — do not change the `T` line without changing 4square-mon.py.
//   T <sht_c> <rh_pct> <rtc_c> <ldr_raw> <ldr_mv> <btn_now> <btn_seen> <millis>
#include <Arduino.h>
#include <Wire.h>
// THE RADIO HEADERS ARE NOT INCLUDED IN A DEMO BUILD.
//
// This is the difference between "the radio is switched off" and "there is no
// radio in the image". cfg.wifi_on = 0 is a RAM VALUE -- one byte in the
// EEPROM ring that a corrupt record, a factory reset or a menu press can flip,
// after which a demo unit in front of a stranger starts hunting for a network.
// Not linking WiFi.h and ArduinoOTA.h at all makes that unreachable, and it is
// what lets `strings` on the .bin be the proof rather than a promise.
//
// esp_ota_ops.h STAYS in both builds: it is the ROLLBACK path
// (verifyRollbackLater / esp_ota_mark_app_valid_cancel_rollback), which is
// about surviving a bad flash over USB and has nothing to do with the radio.
#ifndef DEMO_BUILD
#include <WiFi.h>
#include <ArduinoOTA.h>
#endif
#include <esp_ota_ops.h>

#include "src/config.h"
#include "src/demo.h"       // DEMO_BUILD: the demo's policy, in one place
#include "src/board/bus.h"
#include "src/settings/store.h"
#include "src/screens/display.h"
#include "src/screens/faces.h"
#include "src/screens/pages.h"
#include "src/screens/anim.h"
#include "src/app/ui.h"
#include "src/app/leds.h"
#include "src/board/sensors.h"
// WiFi + OTA credentials. Gitignored, never committed, never printed.
// A DEMO build has no radio and does not include this file at all. A full
// build needs it: copy src/secrets.h.example to src/secrets.h and fill it in.
#ifndef DEMO_BUILD
#if __has_include("src/secrets.h")
#include "src/secrets.h"
#else
#error "src/secrets.h is missing. Copy firmware/foursquare/src/secrets.h.example to src/secrets.h and fill in your WiFi credentials, or build the demo image with -DDEMO_BUILD, which needs no secrets."
#endif
#endif

// ===========================================================================
// DEFECT FIX 1 of 2 — OTA ROLLBACK WAS BEING THROWN AWAY.
// ===========================================================================
// With app rollback enabled the bootloader starts a freshly flashed image in
// PENDING_VERIFY and reverts to the previous one unless the app marks itself
// valid. The Arduino core does that inside initArduino(), BEFORE setup() runs —
// so the safety net was spent before there was anything to be safe about, and
// a firmware that crashed on its first line would need a cable to recover.
//
// This weak symbol hands the decision back to us; loop() marks the image good
// only after it has genuinely run for a minute without faulting.
extern "C" bool verifyRollbackLater() { return true; }
static bool     rollback_settled = false;
static uint32_t healthy_since = 0;
static const uint32_t ROLLBACK_CONFIRM_MS = 60000;

// ---- WiFi / OTA -----------------------------------------------------------
// Every setting here was paid for on the older C3 clock; read the justclock-c3
// memory before changing any of them.
//   * TX POWER STAYS CAPPED at 11 dBm. At full TX the SuperMini's LDO browns
//     out on wall-charger power: it joins happily on PC USB and never on a
//     charger, and because the screens stay lit it looks alive, not broken.
//   * THE RETRY MUST BE 20 s. A 5 s retry aborts a join still in progress, so
//     it livelocks and never connects at all.
//   * MODEM SLEEP OFF, or the board misses the UDP invitation espota sends.
#ifndef DEMO_BUILD
static uint32_t next_wifi_try = 0;
static uint8_t  wifi_which = 0;
static bool     ota_up = false, wifi_was_up = false;
static volatile bool ota_active = false;
// onError fires for OTA_AUTH_ERROR and OTA_BEGIN_ERROR, both of which are
// raised BEFORE onStart. Without this flag a port scanner hitting 3232 would
// turn all four panels on, fire the failure LED for 12 s, and leave a
// night-blanked bedroom lit.
static bool ota_started = false;
// ---- THE OTA DEAD-MAN SWITCH ----------------------------------------------
// `ota_active` short-circuits loop() and onStart disables the loop watchdog,
// so between them an OTA that ends WITHOUT firing onEnd or onError is the one
// state this firmware cannot recover from on its own: no loop, no watchdog, no
// display updates, and the LEDs frozen on whatever progress bar they last
// drew. Every known path does fire one of the two — setTimeout(4000) sees to
// that — which is precisely why this is worth having: the failure it catches
// is the unknown one, and its cost is a comparison per pass.
static uint32_t ota_activity_ms = 0;
static const uint32_t OTA_STALL_MS = 30000;
#endif  // !DEMO_BUILD

// ---- boot counter ---------------------------------------------------------
// Its own EEPROM page, outside the settings ring. One write per power-on
// against a 1,000,000 cycle rating is a boot a day for 2,700 years.
static const uint16_t EE_BOOT_ADDR = 8192;

static uint32_t boot_count_bump() {
  uint8_t b[8];
  uint32_t n = 0;
  if (ee_read(EE_BOOT_ADDR, b, 8)) {
    uint32_t v   = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                   ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    uint32_t chk = (uint32_t)b[4] | ((uint32_t)b[5] << 8) |
                   ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    if ((v ^ 0xA5A5A5A5UL) == chk) n = v;      // a blank page fails this
  }
  n++;
  uint32_t chk = n ^ 0xA5A5A5A5UL;
  b[0] = (uint8_t)n;         b[1] = (uint8_t)(n >> 8);
  b[2] = (uint8_t)(n >> 16); b[3] = (uint8_t)(n >> 24);
  b[4] = (uint8_t)chk;       b[5] = (uint8_t)(chk >> 8);
  b[6] = (uint8_t)(chk >> 16); b[7] = (uint8_t)(chk >> 24);
  ee_write_page(EE_BOOT_ADDR, b, 8);
  return n;
}

// ---- serial ---------------------------------------------------------------
static void report_rtc() {
  RtcTime t = rtc_read();
  Serial.print("C ");
  if (!t.ok) { Serial.println("READFAIL"); return; }
  char buf[32];
  snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u:%02u",
           t.year, t.mon, t.day, t.hour, t.min, t.sec);
  Serial.print(buf);
  Serial.print(" osf="); Serial.print(rtc_osf() ? 1 : 0);
  Serial.print(" up=");  Serial.println(millis());
  // The commit ledger. "Dirty commits made the animation segments faster" is
  // a claim about the bus, and this line is where it stops being a belief:
  // last commit's cost and pushed/asked panels, the worst case seen, and the
  // lifetime pushed/skipped split.
  uint32_t lus, mus, pt, st; uint8_t lp, la;
  disp_commit_stats(lus, mus, lp, la, pt, st);
  Serial.print("B commit=");  Serial.print(lus);
  Serial.print("us pushed="); Serial.print(lp);
  Serial.print("/");          Serial.print(la);
  Serial.print(" max=");      Serial.print(mus);
  Serial.print("us total=");  Serial.print(pt);
  Serial.print(" skipped=");  Serial.println(st);
}

static void print_settings() {
  // Printed from the struct directly. Version A has no settings-table module
  // to walk, and re-inventing one only so the report could iterate it would be
  // a second source of truth for the same fields.
  Serial.println("# settings:");
  Serial.print("#   page        = "); Serial.print(page_name(ui_page()));
  Serial.print(" variant ");          Serial.println(ui_variant());
  Serial.print("#   hour mode   = "); Serial.println(cfg.hour24 ? "24 HOUR" : "12 HOUR");
  if (ui_page() == PG_SETTINGS) {
    Serial.print("#   set cursor  = "); Serial.println(ui_set_cursor());
  }
  Serial.print("#   temp unit   = "); Serial.println(cfg.temp_unit ? "F" : "C");
  Serial.print("#   bright day  = "); Serial.println(cfg.bright_day);
  Serial.print("#   bright nite = "); Serial.println(cfg.bright_night);
  Serial.print("#   auto dim    = "); Serial.println(cfg.autodim ? "ON" : "OFF");
  Serial.print("#   dim sens    = ");
  if (!cfg.autodim) Serial.println("(auto dim off)");
  else {
    uint16_t wlo, whi;
    disp_light_window(wlo, whi);
    // The WINDOW, not just the level. "HIGH" alone is not diagnosable; the two
    // raw ADC counts the curve actually spans can be compared directly against
    // the ldr_raw field on the T line.
    Serial.print(cfg.dim_sens);
    Serial.print("  window raw "); Serial.print(wlo);
    Serial.print("..");            Serial.println(whi);
  }
  Serial.print("#   auto off    = ");
  if (!cfg.off_enable) Serial.println("OFF");
  else {
    char buf[24];
    snprintf(buf, sizeof buf, "%02u:%02u -> %02u:%02u",
             cfg.off_start_h, cfg.off_start_m, cfg.off_end_h, cfg.off_end_m);
    Serial.print(buf);
    Serial.print(ui_is_screens_off(rtc_now(millis())) ? "  (NOW)" : "");
    Serial.println();
  }
  Serial.print("#   glass       = ");
  Serial.println(disp_is_blanked() ? "BLANKED" : "on");
  Serial.print("#   night mode  = ");
  Serial.println(cfg.night_mode == 0 ? "OFF" : (cfg.night_mode == 1 ? "DIM" : "BLANK"));
  Serial.print("#   night start = "); Serial.print(cfg.night_start_h);
  Serial.print(":");                  Serial.println(cfg.night_start_m);
  Serial.print("#   night end   = "); Serial.print(cfg.night_end_h);
  Serial.print(":");                  Serial.println(cfg.night_end_m);
  Serial.print("#   shift       = "); Serial.print(cfg.shift_secs);
  Serial.print("s / ");               Serial.print(cfg.shift_amp);
  Serial.println("px");
  Serial.print("#   wifi        = "); Serial.println(cfg.wifi_on ? "ON" : "OFF");
  Serial.print("#   led bright  = "); Serial.println(cfg.led_bright);
  // Held states and their remaining budget. A dark fault LED now means either
  // "no fault" or "a fault whose budget lapsed", and only this can tell them
  // apart — see the hold watchdog in leds.cpp.
  led_report();
}

// Anything unrecognised is ignored silently, so a stray keystroke in a
// terminal cannot corrupt the clock.
static void poll_serial() {
  static char line[64];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof(line) - 1) line[n++] = c; continue; }
    line[n] = 0;
    n = 0;
    switch (line[0]) {
      case '?': report_rtc(); break;
      case 'P': print_settings(); break;
      // `# SW state=<zero|run|pause> ms=<elapsed> page=<PG_ index>`, one line.
      // tools and the bench acceptance script parse it; keep the format.
      case 'W': ui_stopwatch_report(); break;
      // B0..B3 = MODE/SET/UP/DOWN, and a trailing L makes it a LONG press.
      // SW1-SW4 are through-hole parts in the LCSC kit and JLC never fitted
      // them, so on an unpopulated board this is the only way to reach the
      // pages — and since holding SET is the only way into the settings page,
      // `B1L` is the only way to reach that at all.
      case 'B': {
        int n = atoi(line + 1);
        bool lng = strchr(line + 1, 'L') || strchr(line + 1, 'l');
        ui_button((uint8_t)(n < 0 ? 0 : n), lng);
        break;
      }
      case 'Z': rtc_clear_osf(); Serial.println("# OSF cleared"); break;
      case 'I': led_fire(LED_IDENTIFY); Serial.println("# identify"); break;
      // Re-scan the screens. The boot report scrolls past before a host can
      // attach — this board is native-USB CDC, so it does NOT reset when
      // DTR/RTS toggle and you cannot reopen the port to catch it.
      case 'S': disp_rescan(); ui_force_repaint(); break;
      case 'R': {
        int which = -1;
        if (sscanf(line + 1, "%d", &which) != 1) which = -1;
        disp_flip(which);
        ui_force_repaint();
        Serial.print("# rot now");
        for (uint8_t i = 0; i < N_SCREENS; i++) {
          Serial.print(' '); Serial.print(OLED_ROT[i]);
        }
        Serial.println();
        break;
      }
      // 'D n' pins a contrast; 'D' alone hands auto-dim back. This exists to
      // separate two very different faults: the contrast register not doing
      // anything, versus the light-to-contrast curve being wrong for the room.
      // Without it you cannot tell them apart by looking.
      case 'D': {
        int v = -1;
        if (sscanf(line + 1, "%d", &v) == 1 && v >= 0 && v <= 255) {
          disp_pin_contrast((int16_t)v);
          Serial.print("# contrast pinned at "); Serial.println(v);
        } else {
          disp_pin_contrast(-1);
          Serial.println("# auto-dim back on");
        }
        break;
      }
      case 'F':
        settings_defaults(cfg);
        settings_save();
        ui_force_repaint();
        Serial.println("# factory defaults restored");
        break;
      case 'T': {
        unsigned y, mo, d, h, mi, s;
        // VALIDATE. dec2bcd() silently truncates anything above 99 (250 turns
        // into 0x90), so an unchecked value lands the clock in a state only
        // the plausibility gate reports, with no clue where it came from.
        if (sscanf(line + 1, "%u %u %u %u %u %u", &y, &mo, &d, &h, &mi, &s) == 6 &&
            y >= 2000 && y <= 2099 && mo >= 1 && mo <= 12 &&
            d >= 1 && d <= 31 && h < 24 && mi < 60 && s < 60) {
          rtc_write_time((uint16_t)y, (uint8_t)mo, (uint8_t)d,
                         (uint8_t)h, (uint8_t)mi, (uint8_t)s);
          rtc_clear_osf();
          Serial.println("# time set");
          report_rtc();
          ui_force_repaint();
        } else {
          Serial.println("# bad T command");
        }
        break;
      }
      default: break;
    }
  }
}

// ---- OTA ------------------------------------------------------------------
// EVERYTHING FROM HERE TO THE END OF wifi_tick() IS ABSENT FROM A DEMO IMAGE:
// the OTA server, the join, the retry ladder and every WiFi.* call the
// firmware makes. See src/demo.h.
#ifndef DEMO_BUILD
static void ota_setup() {
  ArduinoOTA.setHostname(OTA_HOST);
  // THE DIGEST, NOT THE PASSWORD. setPasswordHash() authenticates identically
  // to setPassword() against the same `espota.py -a <your OTA password>`, while the
  // image carries something an attacker cannot upload with. See secrets.h.
  ArduinoOTA.setPasswordHash(OTA_PASS_SHA256);
  ArduinoOTA.onStart([]() {
    Serial.println("# OTA start");
    ota_active = true;
    ota_started = true;
    ota_activity_ms = millis();
    // ArduinoOTA.handle() is FULLY BLOCKING — _runUpdate() does not return
    // until the transfer ends, so loop() is never entered and the loop
    // watchdog would fire partway through and reboot mid-flash.
    disableLoopWDT();
    led_hold(LED_OTA_ACTIVE, true);
    // Screens OFF for the whole transfer, and NOT redrawn for progress.
    // Driving 4 KB of I2C while the radio is at peak current is what used to
    // kill these transfers on charger power.
    disp_all_off(true);
    // The 11 dBm cap guards the LDO with the screens and LEDs lit. Both are
    // off right now, so the transfer gets the full radio — which is the half
    // of the link this board is worst at. See design/ota-diagnosis.md.
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int total) {
    led_ota_progress((uint8_t)(total ? (uint32_t)p * 100 / total : 0));
  });
  ArduinoOTA.onEnd([]() {
    ota_active = false;
    Serial.println("# OTA done, rebooting");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.print("# OTA error "); Serial.println((int)e);
    if (!ota_started) return;          // an auth failure, not a failed update
    ota_started = false;
    ota_active = false;
    enableLoopWDT();
    WiFi.setTxPower(WIFI_POWER_11dBm);
    led_hold(LED_OTA_ACTIVE, false);
    led_fire(LED_OTA_FAIL);
    // The panels are left exactly as onStart put them — dark — and ui_tick's
    // blank policy decides within one 50 ms pass whether they should come
    // back. This used to re-derive the answer here from night_mode, which was
    // a second copy of a rule that now has two inputs (the night window AND
    // the screens-off schedule); a failed update at 4 am would have lit a
    // bedroom the schedule had deliberately darkened.
    ui_force_repaint();
  });
  // The stock receive deadline is 1000 ms, and on the FIRST chunk the retry
  // branch is skipped (written == 0), so one late chunk is an instant
  // OTA_RECEIVE_ERROR — the "error 3 at 1.0 s" this board kept producing.
  // Stays under espota's own 10 s per-chunk budget.
  ArduinoOTA.setTimeout(4000);
  ArduinoOTA.begin();
  ota_up = true;
  Serial.print("# OTA ready on "); Serial.print(WiFi.localIP());
  Serial.print(":3232 host "); Serial.println(OTA_HOST);
}

// THE JOIN SEQUENCE IS COPIED FROM THE CLOCK THAT IS ACTUALLY ON THIS NETWORK
// (justclockc3's tryWifi), not invented here. A version that set mode + sleep
// + power + hostname in setup() and called begin() once never associated at
// all, with the credentials verified correct against NetworkManager's own PSK.
// Rather than keep guessing which difference mattered, do what demonstrably
// works.
static void wifi_attempt() {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.setAutoReconnect(true);
  const bool two = strcmp(WIFI_SSID, WIFI_SSID2) != 0;
  if (two) wifi_which ^= 1; else wifi_which = 0;
  WiFi.begin(wifi_which ? WIFI_SSID2 : WIFI_SSID,
             wifi_which ? WIFI_PASS2 : WIFI_PASS);
  Serial.print("# wifi: attempting join, net ");
  Serial.println(wifi_which ? 2 : 1);
}

static void wifi_tick() {
  if (!cfg.wifi_on) {
    if (wifi_was_up || WiFi.getMode() != WIFI_OFF) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      wifi_was_up = false;
      ota_up = false;
      ui_env.wifi_up = false;
      ui_env.ota_ready = false;
    }
    led_hold(LED_WIFI_JOINING, false);
    led_hold(LED_WIFI_FAIL, false);
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifi_was_up) {
      wifi_was_up = true;
      led_hold(LED_WIFI_JOINING, false);
      led_hold(LED_WIFI_FAIL, false);
      led_fire(LED_WIFI_UP);
      ui_env.wifi_up = true;
      snprintf(ui_env.ip, sizeof ui_env.ip, "%s",
               WiFi.localIP().toString().c_str());
      snprintf(ui_env.ssid, sizeof ui_env.ssid, "%s", WiFi.SSID().c_str());
      Serial.print("# wifi up  ip="); Serial.print(WiFi.localIP());
      Serial.print("  rssi=");        Serial.println(WiFi.RSSI());
      if (!ota_up) ota_setup();
    }
    ui_env.rssi = WiFi.RSSI();
    ui_env.ota_ready = ota_up;
    ArduinoOTA.handle();
    return;
  }
  if (wifi_was_up) {
    wifi_was_up = false;
    ui_env.wifi_up = false;
    ui_env.ota_ready = false;
  }
  uint32_t now = millis();
  led_hold(LED_WIFI_JOINING, true);
  if ((int32_t)(now - next_wifi_try) < 0) return;
  // RETRY ON A CLOCK. Gating on the status code failed both ways: re-begin()ing
  // every 20 s aborted joins still in flight, and treating WL_DISCONNECTED as
  // "still trying" meant never retrying, because that is also where a FAILED
  // attempt sits. A fixed window is right in both cases.
  next_wifi_try = now + WIFI_RETRY_MS;
  wifi_attempt();
}
#endif  // !DEMO_BUILD

// ===========================================================================
void setup() {
  Serial.begin(115200);
  // Without this a USB CDC write blocks forever when nothing is reading the
  // port, which freezes everything on a board running from a wall plug.
  Serial.setTxTimeoutMs(0);
  delay(300);

  led_begin();
  led_fire(LED_BOOT);

  bus_begin();
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
  rtc_begin();

  settings_load();
#ifdef DEMO_BUILD
  // The ring survives a flash, so a previously-used board would come up on its
  // own record. demo_force() re-asserts only the demo's promises, and saves
  // nothing. See src/demo.h.
  demo_force(cfg);
#endif
  ui_env.boots = boot_count_bump();

  disp_begin();
  ui_begin();

  Serial.println("# 4square rev O");
  Serial.println("# T <sht_c> <rh_pct> <rtc_c> <ldr_raw> <ldr_mv>"
                 " <btn_now> <btn_seen> <millis>");
  Serial.println("# buttons: MODE=clock  SET=sensors  UP=stopwatch  DOWN=anim");
  Serial.println("#          on stopwatch: UP start, DOWN pause then clear;"
                 "  hold MODE = LEDs on/off,  hold SET = settings");
  Serial.println("# serial:  ? report | P settings | T set | Z clr OSF |"
                 " S rescan | R rotate | D dim | I identify | F factory");
  Serial.println("#          B0-B3 press, B0L-B3L hold  (B1L opens settings) | W stopwatch");
  Serial.print  ("# boot #"); Serial.println(ui_env.boots);

#ifndef DEMO_BUILD
  if (cfg.wifi_on) {
    WiFi.setHostname(OTA_HOST);
    wifi_attempt();
    next_wifi_try = millis() + WIFI_RETRY_MS;
  }
#endif

  // =========================================================================
  // DEFECT FIX 2 of 2 — loop() WAS NOT WATCHDOGGED.
  // =========================================================================
  // The Arduino core leaves loopTaskWDTEnabled false, so a loop() that wedged
  // — on a stuck I2C bus, say — hung forever with no recovery and no evidence
  // of why. Subscribed, a pass that never returns reboots the device and
  // leaves a reset reason behind. It is explicitly disabled for the duration
  // of an OTA, which blocks by design.
  enableLoopWDT();

  healthy_since = millis();
}

// A pass through loop() that takes longer than this says so on serial with the
// elapsed time. A freeze that leaves no trace is unfixable; one that announces
// itself is not.
static const uint32_t STALL_MS = 250;
static uint32_t last_loop_ms = 0;
static uint32_t next_light = 0, next_sht = 0, next_rtc = 0, next_rtc_poll = 0;
static uint32_t next_led = 0, next_ui = 0, next_report = 0, next_health = 0;

void loop() {
  uint32_t now = millis();
  if (last_loop_ms && (now - last_loop_ms) > STALL_MS) {
    Serial.print("# STALL "); Serial.print(now - last_loop_ms);
    Serial.println("ms");
  }
  last_loop_ms = now;

#ifndef DEMO_BUILD
  wifi_tick();
#else
  // ========================================================================
  // DEMO BUILD: THE RADIO IS NEVER BROUGHT UP. See src/demo.h.
  // ========================================================================
  // wifi_tick() owns every path to the radio in firmware C -- WiFi.mode(),
  // WiFi.begin(), the retry ladder and the arming of the OTA server all live
  // inside it or behind it. Not calling it at all means none of them exists,
  // the boot waits on nothing, and there is no JOINING or retry state the
  // device can be caught sitting in while somebody is looking at it.
  //
  // ui_env.wifi_up stays false, which the sensor page and the LED column
  // already render as "no network", so nothing downstream needed a special
  // case. That is why this is one guarded call rather than a fork.
#endif
#ifndef DEMO_BUILD
  if (ota_active) {
    uint32_t seen = led_ota_progress_ms();
    if (seen && (int32_t)(seen - ota_activity_ms) > 0) ota_activity_ms = seen;
    if ((uint32_t)(now - ota_activity_ms) > OTA_STALL_MS) {
      // See the dead-man switch note at the top. Put back everything onStart
      // took away, in the same order onError would have.
      Serial.println("# OTA went quiet — recovering");
      ota_active = false;
      ota_started = false;
      enableLoopWDT();
      WiFi.setTxPower(WIFI_POWER_11dBm);
      led_hold(LED_OTA_ACTIVE, false);
      led_all_off();
      ui_force_repaint();
    }
    return;
  }
#endif  // !DEMO_BUILD

  // Mark the running image good only once it has proved it can run. Before
  // this point a crash rolls back to the last firmware that worked.
  if (!rollback_settled && now - healthy_since > ROLLBACK_CONFIRM_MS) {
    rollback_settled = true;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
      Serial.println("# image marked valid, rollback cancelled");
  }

  poll_serial();

  RtcTime t = rtc_now(now);

  ui_input(now);
  ui_tick(now, t);

  if ((int32_t)(now - next_ui) >= 0) {
    next_ui = now + UI_TICK_MS;
    ui_paint(now, t, false);
  }

  if ((int32_t)(now - next_rtc_poll) >= 0) { next_rtc_poll = now + 10; rtc_temp_poll(); }
  if ((int32_t)(now - next_rtc) >= 0)      { next_rtc = now + RTC_PERIOD_MS; rtc_temp_trigger(); }

  if ((int32_t)(now - next_light) >= 0) {
    next_light = now + LIGHT_PERIOD_MS;
    light_sample();                    // blanks the LEDs for 25 ms
    disp_set_ambient(light_raw());
  }

  if ((int32_t)(now - next_led) >= 0) {
    next_led = now + LED_FRAME_MS;
    led_tick(now, light_raw(), ui_is_night(t));
  }

  if ((int32_t)(now - next_sht) >= 0) {
    next_sht = now + SHT_PERIOD_MS;
    float tc, rh;
    if (sht_read(tc, rh)) {
      ui_env.sht_c  = tc;
      ui_env.rh     = (uint8_t)(rh + 0.5f);
      ui_env.sht_ok = true;
      Serial.print("T ");
      Serial.print(tc, 3);           Serial.print(' ');
      Serial.print(rh, 2);           Serial.print(' ');
      Serial.print(rtc_temp_c(), 2); Serial.print(' ');
      Serial.print(light_raw());     Serial.print(' ');
      Serial.print(light_mv());      Serial.print(' ');
      Serial.print(btn_stable);      Serial.print(' ');
      Serial.print(btn_latched);     Serial.print(' ');
      Serial.println(now);
    } else {
      // Always emit a line. Printing nothing on failure makes the host panel
      // look frozen when the real story is a bus fault.
      ui_env.sht_ok = false;
      Serial.print("E "); Serial.print(bus_worst_device());
      Serial.print(' ');  Serial.print(bus_error_count());
      Serial.print(' ');  Serial.print(btn_latched);
      Serial.print(' ');  Serial.println(now);
    }
    // OUTSIDE the if. Clearing the latch only on success meant that with a
    // failing SHT31 — the exact fault the E line exists to report — every
    // button press ever made stayed latched, and the host panel showed all
    // four buttons permanently held.
    btn_latched = 0;
    if (bus_faulting()) bus_recover();
  }

  if ((int32_t)(now - next_report) >= 0) { next_report = now + 1000; report_rtc(); }

  // LIVE display health, once a second. Without this, panel_ok[] was a
  // boot-time snapshot: four panels could die and nothing would notice,
  // because Adafruit's display() discards every I2C result. It also
  // revalidates the mux, whose channel cache is otherwise trusted forever.
  if ((int32_t)(now - next_health) >= 0) {
    next_health = now + 1000;
    if (disp_health_check()) ui_force_repaint();
  }
  // (The separate un-blank repaint flag is gone. It existed because
  // display.cpp used to un-blank the panels on its own at the end of the night
  // window; now ui.cpp's blank policy owns that transition and forces the
  // repaint at the point it makes the decision. A panel re-initialised behind
  // the UI's back is still covered — that is what disp_health_check() returns.)

  // Faults, in the order the LED layer will show them.
  led_hold(LED_FAULT_I2C, bus_faulting() || disp_present_mask() != 0x0F);
  led_hold(LED_RTC_UNSET, !t.ok || rtc_osf_cached());

  // Deferred settings write: one EEPROM page a few seconds after you stop
  // pressing buttons, rather than one per press.
  settings_tick();
}
