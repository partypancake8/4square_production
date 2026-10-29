#include "leds.h"
#include "../settings/store.h"
#include "../board/bus.h"
#include "../screens/display.h"        // disp_light_window, the LEDs and the panels must
                            // agree about how bright the room is
#include <Adafruit_NeoPixel.h>

// PRIVATE. This used to be a non-static global so sensors.cpp could blank it
// for the light reading; that is now done through led_light_blank_begin()/end()
// and exactly one translation unit touches GPIO7. strip.begin() is called ONCE,
// in led_begin(), and nothing else may ever re-init or borrow the pin: on this
// chip the RMT binding does not come back, and every call still reports success
// while the LEDs stay dead.
static Adafruit_NeoPixel strip(N_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);

// ---- the column -----------------------------------------------------------
// Chain order is also physical order, top to bottom.
static const uint8_t TOP = 0, MID = 1, BOT = 2;

struct Rgb { uint8_t r, g, b; };

// ---- THE PALETTE, AND THE ONE COLOUR THAT IS NOT IN IT ----------------------
// THERE IS NO GREEN. Not "green is used carefully", there is no green
// constant, so no state can pick one and no transition can land on one.
//
// WHY. LED_CONFIRM was green and it was reached, in normal use, straight out of
// a blue or cyan state: finish a join and you get cyan (LED_WIFI_UP) -> green;
// save a setting while a join is still retrying and you get the cyan comet
// (LED_WIFI_JOINING) -> green. Both of those were also HARD CUTS, so what the
// eye actually saw was a blue thing being replaced, in one frame, by a green
// thing. The user's words were "really choppy... I hate that. Don't use that."
//
// Retiring the hue rather than the transition is deliberate: a rule about which
// two colours may not be adjacent is a rule somebody has to remember, and this
// file is full of evidence that those do not survive. A colour that does not
// exist cannot come back by accident, and tools/ledcheck asserts it, no frame
// this file emits may be green-dominant, and no transition may put a blue or
// cyan frame next to a green one.
//
// Nothing is lost. The column's language was never the hue: "a state is not
// just a colour, it is a movement along the column" (leds.h). CONFIRM is now a
// warm double-thump, which reads as a nod, and REJECT is still red.
static const Rgb C_RED     = {255,   0,   0};
static const Rgb C_AMBER   = {255, 120,   0};
static const Rgb C_CYAN    = {  0, 190, 255};   // blue-dominant, deliberately
static const Rgb C_BLUE    = {  0,  60, 255};
static const Rgb C_MAGENTA = {255,   0, 160};
static const Rgb C_WARM    = {255, 170,  70};

// ---- state ----------------------------------------------------------------
static bool     held[LED_COUNT];
static uint32_t oneshot_until[LED_COUNT];
static uint32_t oneshot_since[LED_COUNT];
static uint32_t held_since[LED_COUNT];
static uint8_t  ota_pct = 0;
static uint16_t dither_err[N_LEDS][3];
// When led_ota_progress() last ran. The OTA hold is released by onEnd/onError,
// and this is how loop() notices a transfer that produced neither.
static uint32_t ota_progress_ms = 0;
// Whether the strip is already known-dark. Without it the idle path pushed a
// full clear+show down the wire 60 times a second forever, which is 24 bytes
// of bit-banged WS2812 traffic and an interrupts-off window, to change
// nothing. It also matters for the failsafe: "stop showing this" should stop
// driving the strip, not keep driving it with zeros. And it is what lets the
// light sample skip its blank entirely, see led_light_blank_begin().
static bool     dark_latched = false;
// The last frame actually pushed to the wire, per pixel. The light-sample blank
// restores this so the animation is continuous across the ADC read; without it
// the strip stayed black until whenever led_tick next happened to run.
static uint8_t  shown[N_LEDS][3];

// How long each one-shot lasts. Sticky states are not listed.
//
// These grew when the gaits stopped being square waves: an eased shape needs
// room for its own ramps or it is just a step with extra arithmetic. Each one
// is now the exact length of the animation it drives, and tools/ledcheck checks
// that the animation has finished (is at zero) by the time the budget ends, so
// a one-shot can never be cut off mid-swing.
uint32_t led_oneshot_ms(uint8_t s) {
  switch (s) {
    case LED_WIFI_UP:   return 900;    // g_rise, 800 ms of travel plus its tail
    case LED_ACK_MODE: case LED_ACK_SET:
    case LED_ACK_UP:   case LED_ACK_DOWN: return 300;   // g_spot envelope
    case LED_CONFIRM:   return 440;    // g_heartbeat, two thumps
    case LED_REJECT:    return 620;    // g_shiver, three decaying bumps
    case LED_BOOT:      return 960;    // g_wipe
    case LED_OTA_FAIL:  return 12000;
    case LED_IDENTIFY:  return 30000;
    default:            return 0;      // sticky
  }
}

// ---- THE HOLD WATCHDOG -----------------------------------------------------
// HOW LONG A STICKY STATE IS ALLOWED TO KEEP ANIMATING. Zero means forever.
//
// The one-shots already end by construction. The HELD states did not, and
// every one of them is asserted by a condition that can fail to clear:
// LED_FAULT_I2C is held for as long as a panel is missing, LED_WIFI_JOINING
// for as long as the join keeps failing, which on this board, with the RF
// problem in design/ota-diagnosis.md, is indefinitely. The result was three
// LEDs pulsing in a bedroom, all day, every day, reporting a fault the user
// already knows about and cannot do anything about from across the room.
//
// A fault indicator is a NOTIFICATION, not a status bar. It has done its job
// once it has been seen; after that it is just light. So each held state gets
// a budget, and when it lapses the state stops being rendered even though it
// is still asserted, `led_status_lapsed()` and the serial report still tell
// you it is there.
//
// It is deliberately NOT a reset of held[]: held_since is stamped only on the
// false->true edge, so a lapsed state stays lapsed until its condition
// actually clears and comes back. A fault that flickers cannot re-arm itself
// every few seconds and light the room forever anyway.
uint32_t led_hold_max_ms(uint8_t s) {
  switch (s) {
    // The join retry is on a 20 s clock. Nine attempts is long enough to cover
    // a router reboot, and past that the answer is not going to change.
    case LED_WIFI_JOINING: return 180000UL;      // 3 min
    case LED_WIFI_FAIL:    return 300000UL;      // 5 min
    // A bus fault or a dead RTC is worth a long, obvious complaint, but not a
    // permanent one. Ten minutes is far past "did I notice it".
    case LED_FAULT_I2C:    return 600000UL;      // 10 min
    case LED_RTC_UNSET:    return 600000UL;      // 10 min
    // An OTA that is genuinely running renews itself through led_ota_progress.
    // This bound only ever fires on one that stopped without saying so.
    case LED_OTA_ACTIVE:   return 300000UL;      // 5 min
    case LED_MENU:         return 300000UL;      // 5 min
    default:               return 0;             // no bound
  }
}

// Which states are suppressed overnight. A fault at 3am that lights a bedroom
// is a worse bug than the fault it is reporting; these all still show the
// moment the night window ends, and none of them is urgent at 3am.
static bool suppressed_at_night(uint8_t s) {
  switch (s) {
    case LED_FAULT_I2C: case LED_RTC_UNSET: case LED_WIFI_JOINING:
    case LED_WIFI_FAIL: case LED_WIFI_UP:   case LED_BOOT:
      return true;
    default:
      return false;
  }
}

// ---- the wire --------------------------------------------------------------
// Every write to the strip goes through here, so `shown` can never disagree
// with what is actually lit.
static void push_pixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b) {
  shown[i][0] = r; shown[i][1] = g; shown[i][2] = b;
  strip.setPixelColor(i, strip.Color(r, g, b));
}
static void push_show() { strip.show(); dark_latched = false; }

void led_begin() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  delay(20);
  strip.begin();
  // 255, NOT 200. NeoPixel's setBrightness() is a lossy 8-bit multiply applied
  // on top of whatever we write, so anything below 255 quietly throws away the
  // low bits the dithering below depends on. Brightness is applied upstream,
  // in 16 bits, where it belongs.
  strip.setBrightness(255);
  strip.clear();
  strip.show();
  dark_latched = true;
  for (uint8_t i = 0; i < LED_COUNT; i++) { held[i] = false; oneshot_until[i] = 0; }
  for (uint8_t i = 0; i < N_LEDS; i++) {
    shown[i][0] = shown[i][1] = shown[i][2] = 0;
    dither_err[i][0] = dither_err[i][1] = dither_err[i][2] = 0;
  }
}

void led_hold(LedStatus s, bool on) {
  if (s >= LED_COUNT) return;
  if (on && !held[s]) held_since[s] = millis();
  held[s] = on;
}

void led_fire(LedStatus s) {
  if (s >= LED_COUNT) return;
  uint32_t d = led_oneshot_ms(s);
  if (!d) return;
  oneshot_since[s] = millis();
  oneshot_until[s] = millis() + d;
}

void led_ack(uint8_t btn) {
  switch (btn) {
    case 0: led_fire(LED_ACK_MODE); break;
    case 1: led_fire(LED_ACK_SET);  break;
    case 2: led_fire(LED_ACK_UP);   break;
    case 3: led_fire(LED_ACK_DOWN); break;
  }
}

static void clear_shown() {
  for (uint8_t i = 0; i < N_LEDS; i++)
    shown[i][0] = shown[i][1] = shown[i][2] = 0;
}

void led_all_off() { strip.clear(); strip.show(); clear_shown(); dark_latched = true; }

// Go dark, but only actually touch the wire the first time. Any path that
// renders real pixels clears the latch, so this can never leave the strip lit.
static void go_dark() {
  if (dark_latched) return;
  strip.clear();
  strip.show();
  clear_shown();
  dark_latched = true;
}

uint32_t led_ota_progress_ms() { return ota_progress_ms; }

bool led_is_dark() { return dark_latched; }

// What the LED layer would show and what it is deliberately no longer showing.
// Exists because the hold watchdog makes "the fault LED is dark" ambiguous:
// it means either no fault, or a fault whose budget lapsed. Guessing which
// from across a room is exactly the thing the LEDs were supposed to prevent,
// so the answer belongs on the serial report.
void led_report() {
  uint32_t now = millis();
  Serial.print("#   leds        = ");
  Serial.println(cfg.led_mode ? "STATUS" : "OFF");
  for (uint8_t s = 1; s < LED_COUNT; s++) {
    if (!held[s]) continue;
    uint32_t age = now - held_since[s], cap = led_hold_max_ms(s);
    Serial.print("#     held #");   Serial.print(s);
    Serial.print(" for ");          Serial.print(age / 1000);
    Serial.print("s");
    if (cap && age >= cap) Serial.print("  LAPSED (no longer shown)");
    else if (cap)          { Serial.print("  budget "); Serial.print(cap / 1000);
                             Serial.print("s"); }
    Serial.println();
  }
}

// ---- easing ----------------------------------------------------------------
// SMOOTHSTEP, INTEGER, 0..255 -> 0..255. 3x^2 - 2x^3.
//
// The property that earns its keep is not that it is curved, it is that the
// slope is ZERO AT BOTH ENDS. Every shape below is built out of it, so every
// shape starts and stops without a corner, and a corner is precisely what the
// eye reads as a click. The old g_breathe was a triangle and its own comment
// admitted it: "a shape nobody can tell apart from this one at 60 fps". That
// was wrong twice over. There is a visible crease at the top and bottom of
// every breath, and the panels do not deliver 60 fps.
//
// Max intermediate is 255*255*765 = 49,744,875, comfortably inside uint32.
static uint8_t ease(uint16_t x) {
  if (x >= 255) return 255;
  uint32_t t = x;
  return (uint8_t)((t * t * (765 - 2 * t)) / 65025);
}

// An eased 0 -> 255 ramp across `dur` ms, clamped at both ends.
static uint8_t ramp(uint32_t t, uint32_t dur) {
  if (!dur || t >= dur) return 255;
  return ease((uint16_t)(t * 255 / dur));
}

// Rise, hold, fall, the shape almost everything here is made of. Zero at t=0
// and zero again at rise+hold+fall, which is what makes bumps safe to butt up
// against each other and safe to loop.
static uint8_t bump(uint32_t t, uint32_t rise, uint32_t hold, uint32_t fall) {
  if (t < rise) return ramp(t, rise);
  t -= rise;
  if (t < hold) return 255;
  t -= hold;
  if (t >= fall) return 0;
  return (uint8_t)(255 - ramp(t, fall));
}

// THE SLEW LAW. No ramp anywhere in this file is shorter than 90 ms, so no
// channel can move faster than 1.5 * 255/90 ~= 4.25 units per millisecond (the
// 1.5 is smoothstep's peak slope). tools/ledcheck asserts 4.5 + 3 for
// quantisation, at 1 ms, 16 ms and 100 ms sampling. 90 ms is about five frames
// at the design rate and one at the rate the animation page actually delivers:
// fast enough that a button acknowledgement still feels instant, slow enough
// that nothing in the column ever appears in a single frame.
static const uint16_t RAMP_MIN_MS = 90;

// ---- gaits -----------------------------------------------------------------
// Each writes a 0..255 intensity per LED; colour is applied afterwards, so a
// gait and a colour are genuinely independent and a new state is one table row
// rather than a new animation.
//
// EVERY GAIT IS A PURE FUNCTION OF ITS TIME ARGUMENT. Nothing here remembers a
// previous call, so a tick that never happened costs a sample and not a beat,
// and the animation runs at the same speed whether the panels are idle or
// pushing a 4 KB frame.
typedef uint8_t Level3[3];

static void g_solid(Level3 out, uint8_t v) { out[0] = out[1] = out[2] = v; }

// A slow swell and fade, all three together. FOR: waiting. Nothing is counting
// and nothing is travelling, because nothing is progressing, the setup AP sits
// like this for minutes while somebody types on a phone.
static void g_breathe(Level3 out, uint32_t t, uint16_t period,
                      uint8_t lo, uint8_t hi) {
  uint32_t p = t % period, half = period / 2;
  uint16_t tri = (p < half) ? (uint16_t)(p * 255 / half)
                            : (uint16_t)((period - p) * 255 / half);
  uint8_t e = ease(tri);
  g_solid(out, (uint8_t)(lo + (uint32_t)(hi - lo) * e / 255));
}

// n soft pulses, then a long dark gap. FOR: counting. You can read "two" across
// a room in a way you cannot read "amber rather than orange". Eased edges do
// not hurt the count, the gap between pulses is what carries it.
static void g_group(Level3 out, uint32_t t, uint8_t n) {
  const uint16_t RISE = 90, HOLD = 90, FALL = 130, GAP = 240, TAIL = 1200;
  const uint32_t SLOT = (uint32_t)RISE + HOLD + FALL + GAP;
  uint32_t cycle = (uint32_t)n * SLOT + TAIL;
  uint32_t p = t % cycle;
  uint8_t v = 0;
  if (p < (uint32_t)n * SLOT) v = bump(p % SLOT, RISE, HOLD, FALL);
  g_solid(out, v);
}

// A soft glow at a fractional position on the column, with a short leading edge
// and a longer trailing one. `pos` is in 256ths of an LED; index 2 is the
// BOTTOM, so pos falls as the glow rises.
//
// The track runs from 832 (below the bottom LED, fully dark) to -480 (above the
// top one, also fully dark), 1312 units. Both ends being dark is what makes a
// looped version continuous: the wrap happens in the dark, so there is nothing
// to jump. The old three-position comet stepped 0/60/255 and wrapped from a lit
// pixel straight to another lit pixel.
static const int32_t TRACK_LO = -480, TRACK_HI = 832;
static void g_travel(Level3 out, int32_t pos) {
  for (int i = 0; i < 3; i++) {
    int32_t d = (int32_t)(2 - i) * 256 - pos;
    // d < 0: the glow has not reached this LED yet (short, crisp leading edge).
    // d > 0: it has gone past (long, soft tail). That asymmetry is what makes
    // the direction of travel readable at all.
    int32_t w = (d < 0) ? ((-d) * 256 / 300) : (d * 256 / 460);
    out[i] = (w >= 256) ? 0 : (uint8_t)(255 - ease((uint16_t)w));
  }
}

// The travelling glow, looped. FOR: "working on it", a join in progress.
static void g_comet(Level3 out, uint32_t t, uint16_t period) {
  uint32_t p = t % period;
  g_travel(out, TRACK_HI - (int32_t)((uint32_t)p * (TRACK_HI - TRACK_LO) / period));
}

// The same glow, once, bottom to top and gone. FOR: a one-shot success. Upward
// movement reads as "we're up" from across a room without needing the hue.
static void g_rise(Level3 out, uint32_t t, uint16_t dur) {
  if (t >= dur) { g_solid(out, 0); return; }
  g_travel(out, TRACK_HI - (int32_t)((uint32_t)t * (TRACK_HI - TRACK_LO) / dur));
}

// Two soft thumps and then quiet, the shape of a nod. FOR: a setting was
// SAVED. This replaces a literal 0/255 square wave (two hard flashes of green),
// which is the single ugliest thing this file used to do and the one the user
// saw most often.
static void g_heartbeat(Level3 out, uint32_t t) {
  uint8_t v = 0;
  if (t < 240)      v = bump(t, 90, 40, 110);
  else if (t < 480) v = (uint8_t)((uint16_t)bump(t - 240, 90, 30, 110) * 205 / 255);
  g_solid(out, v);
}

// A shallow, very slow glow that never fully lights and never fully darkens.
// FOR: "you are in the menus", present without nagging. A sticky state is
// something you are meant to notice once, not something you should be able to
// watch.
static void g_ember(Level3 out, uint32_t t) { g_breathe(out, t, 4200, 26, 74); }

// Three decaying bumps, close together. FOR: refusal, "that input did
// nothing". A refusal should feel like a bump you walked into, not an alarm, so
// it is short, it fades out rather than stopping, and it never reaches full.
static void g_shiver(Level3 out, uint32_t t) {
  const uint16_t P = 200;                      // 90 rise + 20 hold + 90 fall
  uint32_t n = t / P;
  if (n >= 3) { g_solid(out, 0); return; }
  uint8_t a = (n == 0) ? 255 : (n == 1) ? 165 : 85;
  g_solid(out, (uint8_t)((uint16_t)bump(t % P, 90, 20, 90) * a / 255));
}

// A wave washing up the column, forever, never dark. FOR: "which one is this
// device", unmistakable at a glance and, because it never goes out, impossible
// to confuse with any of the fault gaits, which all count.
static void g_ripple(Level3 out, uint32_t t, uint16_t period) {
  uint32_t p = t % period;
  for (int i = 0; i < 3; i++) {
    Level3 b;
    g_breathe(b, p + (uint32_t)(2 - i) * period / 3, period, 18, 255);
    out[i] = b[0];
  }
}

// A fill level from the bottom up. FOR: OTA progress, the one place an actual
// quantity exists and a percentage is the honest thing to show.
static void g_bar(Level3 out, uint8_t pct, uint32_t t) {
  uint16_t filled = (uint16_t)pct * 3;              // 0..300, in hundredths
  for (int i = 0; i < 3; i++) {
    int pos = 2 - i;
    uint16_t lo = (uint16_t)pos * 100;
    if (filled >= lo + 100)      out[i] = 255;
    else if (filled <= lo)       out[i] = 0;
    else {
      uint8_t base = ease((uint16_t)((filled - lo) * 255 / 100));
      // The leading pixel BREATHES, so a stalled transfer looks different from
      // a slow one. It used to be a 180/255 square wave at 2.5 Hz, which is a
      // strobe sitting on top of the one thing you stare at during a flash.
      Level3 b;
      g_breathe(b, t, 1000, 150, 255);
      out[i] = (uint8_t)((uint16_t)base * b[0] / 255);
    }
  }
}

// Light up bottom to top with eased edges, hold, then fade together. FOR: a
// one-shot "something happened", used at boot.
static void g_wipe(Level3 out, uint32_t t, uint16_t per_px, uint16_t rise,
                   uint16_t hold, uint16_t fade) {
  uint32_t lit_end = (uint32_t)per_px * 2 + rise;   // the last pixel finishes rising
  for (int i = 0; i < 3; i++) {
    int pos = 2 - i;
    uint32_t on_at = (uint32_t)pos * per_px;
    if (t < on_at) { out[i] = 0; continue; }
    uint8_t up = ramp(t - on_at, rise);
    if (t < lit_end + hold) { out[i] = up; continue; }
    uint32_t ft = t - (lit_end + hold);
    out[i] = ft >= fade ? 0
           : (uint8_t)((uint16_t)up * (255 - ramp(ft, fade)) / 255);
  }
}

// A single LED, or all three, under a soft envelope. FOR: button feedback. The
// POSITION is the acknowledgement, the button you pressed lights where that
// button sits, so the envelope only has to keep it from being a step.
static void g_spot(Level3 out, int which, uint8_t v, uint32_t t) {
  uint8_t e = bump(t, RAMP_MIN_MS, 60, 150);        // 300 ms, = its one-shot
  uint8_t lvl = (uint8_t)((uint16_t)v * e / 255);
  out[0] = out[1] = out[2] = 0;
  if (which < 0) { g_solid(out, lvl); return; }
  out[which] = lvl;
}

void led_ota_progress(uint8_t pct) {
  ota_pct = pct;
  uint32_t now = millis();
  // Stamped BEFORE the rate limit, not after. This is the liveness signal
  // loop() uses to tell a slow transfer from a dead one, and rate-limiting it
  // to 30 Hz would have been harmless only by luck.
  ota_progress_ms = now ? now : 1;               // 0 means "no OTA has run"
  // Draw it here and now; nothing else is going to get the chance.
  static uint32_t last_draw = 0;
  if (now - last_draw < 33) return;              // ~30 Hz is plenty
  last_draw = now;

  Level3 lv;
  g_bar(lv, pct, now);
  for (uint8_t i = 0; i < N_LEDS; i++) {
    uint8_t l = (i < N_LEDS_FITTED) ? lv[i] : 0;
    // Full brightness, no ambient scaling and no night cap: if you are
    // flashing this thing you are looking at it.
    push_pixel(i, (uint8_t)((uint16_t)C_BLUE.r * l / 255),
                  (uint8_t)((uint16_t)C_BLUE.g * l / 255),
                  (uint8_t)((uint16_t)C_BLUE.b * l / 255));
  }
  push_show();
}


// ---- the render pass ------------------------------------------------------
// Pick the single highest-priority active state and render it. Deliberately
// NOT a blend: two things animating at once is unreadable, and the priority
// order exists precisely so there is always one right answer.
struct Chosen { uint8_t state; uint32_t age; };

static Chosen pick(uint32_t now, bool night) {
  for (int s = LED_COUNT - 1; s > 0; s--) {
    bool active = false;
    uint32_t age = 0;
    // CLEAR THE DEADLINE WHEN IT LAPSES. Leaving a stale `until` in place is
    // correct only until millis() has advanced 2^31 past it, at which point
    // the signed difference goes negative again and the state re-activates.
    // LED_BOOT would have come back to life at 24.9 days of uptime and, being
    // higher priority than every acknowledgement, silently killed button
    // feedback for the next 25 days.
    if (oneshot_until[s]) {
      if ((int32_t)(now - oneshot_until[s]) < 0) {
        active = true;
        age = now - oneshot_since[s];
      } else {
        oneshot_until[s] = 0;
      }
    }
    if (!active && held[s]) {
      uint32_t cap = led_hold_max_ms(s);
      // Lapsed: still asserted, no longer worth lighting a room over. See the
      // hold watchdog note above. Skipping rather than clearing held[] is what
      // stops a flickering fault from re-arming its own budget forever.
      if (cap && (uint32_t)(now - held_since[s]) >= cap) continue;
      active = true;
      age = now - held_since[s];
    }
    if (!active) continue;
    if (night && suppressed_at_night(s)) continue;
    return {(uint8_t)s, age};
  }
  return {LED_NONE, 0};
}

static void render(uint8_t s, uint32_t age, Level3 lv, Rgb &col) {
  col = C_WARM;
  g_solid(lv, 0);
  switch (s) {
    case LED_OTA_ACTIVE:   col = C_BLUE;    g_bar(lv, ota_pct, age); break;
    case LED_OTA_FAIL:     col = C_MAGENTA; g_group(lv, age, 4); break;
    case LED_IDENTIFY:     col = C_WARM;    g_ripple(lv, age, 1900); break;
    case LED_FAULT_I2C:    col = C_MAGENTA; g_group(lv, age, 3); break;
    case LED_RTC_UNSET:    col = C_AMBER;   g_group(lv, age, 2); break;
    case LED_WIFI_JOINING: col = C_CYAN;    g_comet(lv, age, 1700); break;
    case LED_WIFI_FAIL:    col = C_AMBER;   g_group(lv, age, 1); break;
    case LED_BOOT:         col = C_WARM;    g_wipe(lv, age, 130, 110, 160, 400); break;
    case LED_MENU:         col = C_AMBER;   g_ember(lv, age); break;
    // WARM, not green. See the palette note at the top of this file.
    case LED_CONFIRM:      col = C_WARM;    g_heartbeat(lv, age); break;
    case LED_REJECT:       col = C_RED;     g_shiver(lv, age); break;
    case LED_ACK_MODE:     g_spot(lv, -1,  150, age); break;
    case LED_ACK_SET:      g_spot(lv, MID, 150, age); break;
    case LED_ACK_UP:       g_spot(lv, TOP, 150, age); break;
    case LED_ACK_DOWN:     g_spot(lv, BOT, 150, age); break;
    case LED_WIFI_UP:      col = C_CYAN;    g_rise(lv, age, 800); break;
    default: break;
  }
}

// ---- THE TRANSITION, AND WHY IT DIPS THROUGH BLACK -------------------------
// pick() returns exactly one state and render() used to CUT to it. A cut is a
// step, and a step is the thing the eye reads as a flicker no matter how nice
// the two frames either side of it are.
//
// So every change of state fades the outgoing frame out and the incoming one
// in. It DIPS THROUGH BLACK rather than cross-blending the two colours, and
// that is not laziness: a linear blend invents a hue neither state owns, and
// cyan -> amber passes straight through green, which is the one thing this file
// is not allowed to produce. Dipping also gives each state a clean entrance,
// which is what makes the column's movement legible.
//
// The outgoing frame is FROZEN at the moment of the switch, its gait stops
// advancing, so only one animation is ever being evaluated. The incoming one
// keeps running under the fade, so a state does not appear to start late.
static const uint16_t XF_OUT_MS = 110;   // both >= RAMP_MIN_MS
static const uint16_t XF_IN_MS  = 150;

static uint8_t  cur_state = LED_NONE;
static uint8_t  xf_phase  = 0;           // 0 settled, 1 fading out, 2 fading in
static uint32_t xf_t0     = 0;
static Rgb      xf_col    = C_WARM;
static uint8_t  xf_lv[3]  = {0, 0, 0};
static Rgb      last_col  = C_WARM;      // the frame this tick produced, pre-master
static uint8_t  last_lv[3] = {0, 0, 0};

void led_tick(uint32_t now, uint16_t ambient_raw, bool night) {
  // ---- MEASURE THE REAL CADENCE, DO NOT ASSUME IT --------------------------
  // config.h asks for 60 fps and loop() honours that only when it has nothing
  // else to do. On the animation page ui_paint() blocks for ~92 ms pushing four
  // 1 KB frames at 400 kHz, so led_tick lands nearer 10 Hz, unevenly. Nothing
  // above depends on the rate, the gaits are functions of `now`, but the
  // dither below does, so it needs the truth and not the intention.
  static uint32_t last_tick = 0;
  static uint32_t dt_ema_q4 = (uint32_t)LED_FRAME_MS << 4;
  uint32_t dt = (last_tick == 0) ? LED_FRAME_MS : (uint32_t)(now - last_tick);
  if (dt > 500) dt = 500;                       // a pause is not a cadence
  last_tick = now ? now : 1;
  dt_ema_q4 += ((int32_t)(dt << 4) - (int32_t)dt_ema_q4) / 4;
  const uint32_t dt_ms = dt_ema_q4 >> 4;

  if (cfg.led_mode == 0) {
    go_dark();
    xf_phase = 0; cur_state = LED_NONE;
    last_lv[0] = last_lv[1] = last_lv[2] = 0;
    return;
  }

  Chosen ch = pick(now, night);

  if (ch.state != cur_state) {
    bool was_lit = !dark_latched &&
                   (last_lv[0] || last_lv[1] || last_lv[2]);
    cur_state = ch.state;
    xf_t0 = now;
    if (was_lit) {
      xf_phase = 1;
      xf_col = last_col;
      xf_lv[0] = last_lv[0]; xf_lv[1] = last_lv[1]; xf_lv[2] = last_lv[2];
    } else {
      xf_phase = 2;                              // coming up out of the dark
    }
  }

  Level3 lv;
  Rgb col;
  if (xf_phase == 1) {
    uint32_t e = now - xf_t0;
    uint8_t k = (e >= XF_OUT_MS) ? 0 : (uint8_t)(255 - ramp(e, XF_OUT_MS));
    col = xf_col;
    for (uint8_t i = 0; i < 3; i++)
      lv[i] = (uint8_t)(((uint16_t)xf_lv[i] * k + 127) / 255);
    if (e >= XF_OUT_MS) { xf_phase = 2; xf_t0 = now; }
  }
  if (xf_phase != 1) {
    if (cur_state == LED_NONE) {
      // The dip is finished and there is nothing to come back to. IDLE IS DARK.
      last_lv[0] = last_lv[1] = last_lv[2] = 0;
      xf_phase = 0;
      go_dark();
      return;
    }
    render(cur_state, ch.age, lv, col);
    if (xf_phase == 2) {
      uint32_t e = now - xf_t0;
      uint8_t k = ramp(e, XF_IN_MS);
      for (uint8_t i = 0; i < 3; i++)
        lv[i] = (uint8_t)(((uint16_t)lv[i] * k + 127) / 255);
      if (e >= XF_IN_MS) xf_phase = 0;
    }
  }

  last_col = col;
  last_lv[0] = lv[0]; last_lv[1] = lv[1]; last_lv[2] = lv[2];

  // ---- one master scalar, gamma-corrected exactly once ---------------------
  // Applying gamma per channel, or twice, is the classic way to get colours
  // that drift as they dim. The scalar is 16-bit so the dither below has bits
  // to work with.
  uint32_t user = cfg.led_bright ? cfg.led_bright : 1;
  // Ambient: a dim glow in the dark, full in a lit room. The SAME window the
  // screens dim against, sensitivity included, so the LEDs and the panels
  // agree about the room. This used to inline LIGHT_RAW_LO/HI, which was a
  // second copy of the curve's endpoints, correct only for as long as nothing
  // could move them, which the sensitivity setting now can.
  uint16_t wlo, whi;
  disp_light_window(wlo, whi);
  uint32_t amb = ambient_raw >= whi ? 255
               : ambient_raw <= wlo ? 20
               : 20 + (uint32_t)(ambient_raw - wlo) * 235 / (whi - wlo);
  uint32_t master = user * amb / 255;
  if (night && cur_state != LED_OTA_ACTIVE) {
    // A hard ceiling overnight. OTA is exempt because if you are flashing at
    // 3am you are awake and looking at it.
    const uint32_t NIGHT_CAP = 12;
    if (master > NIGHT_CAP) master = NIGHT_CAP;
  }
  float x = master / 255.0f;
  uint32_t g16 = (uint32_t)(65535.0f * powf(x, 2.2f));

  // TEMPORAL DITHER, AND WHEN IT MUST SWITCH ITSELF OFF.
  //
  // Below about level 8 an 8-bit PWM step is a visible jump and this device
  // spends most of its life down there, so the remainder is carried into the
  // next frame and the steps become a ramp. THAT IS A 60 fps MECHANISM. At the
  // ~10 Hz the animation page really delivers, "the next frame" is 100 ms away
  // and the carry stops being a ramp and becomes a 10 Hz square wave on the
  // bottom bit, the smoothing mechanism generating exactly the flicker it
  // exists to remove. So it is gated on the MEASURED interval, and above two
  // design frames it degrades to plain rounding, which at that rate is strictly
  // better: a constant input then produces a constant output.
  const bool dither_ok = (dt_ms <= LED_DITHER_MAX_DT_MS);

  for (uint8_t i = 0; i < N_LEDS; i++) {
    uint8_t l = (i < N_LEDS_FITTED) ? lv[i] : 0;
    // Fold the gamma scalar down to 8 bits FIRST. The one-expression form
    // peaked at 255 * 255 * 65535 = 4,261,413,375 against a uint32 ceiling of
    // 4,294,967,295, it did not overflow, but 0.8% of headroom on a value
    // three future edits could raise is not a margin, it is a trap. Folded, the
    // worst case is 255 * 255 * 256 = 16,646,400, which is 258x of headroom;
    // tools/ledcheck asserts that margin so a future edit cannot quietly eat it.
    uint32_t g8 = (g16 + 128) >> 8;                  // 0..256
    uint32_t ch16[3] = {
      (uint32_t)col.r * l * g8 / 255,
      (uint32_t)col.g * l * g8 / 255,
      (uint32_t)col.b * l * g8 / 255};
    uint8_t out[3];
    for (uint8_t k = 0; k < 3; k++) {
      if (dither_ok) {
        uint32_t v = ch16[k] + dither_err[i][k];
        out[k] = (uint8_t)(v >> 8);
        dither_err[i][k] = (uint16_t)(v & 0xFF);
      } else {
        out[k] = (uint8_t)((ch16[k] + 128) >> 8);
        dither_err[i][k] = 0;
      }
    }
    push_pixel(i, out[0], out[1], out[2]);
  }
  push_show();
}

// ---- the light-sensor blank ------------------------------------------------
// See the note in leds.h. The strip is eased down, not cut, and eased back up
// to the exact frame that was showing, so what the ADC gets is a genuine
// LED-free reading and what the room gets is a continuous LED image.
//
// The ramp is BLOCKING, like the 25 ms settle it brackets, because there is no
// other loop to run it on and adding a task would put a second writer on the
// RMT channel. It costs 2 * LED_BLANK_RAMP_MS, and sensors.cpp only pays it
// when the strip is actually lit, which, since IDLE IS DARK, is rare, and it
// rate-limits the lit case besides.
static void blank_step(uint8_t k) {
  for (uint8_t i = 0; i < N_LEDS; i++)
    strip.setPixelColor(i, strip.Color(
        (uint8_t)((uint16_t)shown[i][0] * k / 255),
        (uint8_t)((uint16_t)shown[i][1] * k / 255),
        (uint8_t)((uint16_t)shown[i][2] * k / 255)));
  strip.show();                     // NOT push_show: `shown` must survive this
}

bool led_light_blank_begin() {
  // Already dark: the reading is clean as it stands, so there is nothing to
  // blank, nothing to settle and nothing to restore. This is the common case.
  if (dark_latched) return false;
  const uint8_t STEPS = 6;
  for (uint8_t s = 1; s <= STEPS; s++) {
    blank_step((uint8_t)(255 - ramp((uint32_t)s * LED_BLANK_RAMP_MS / STEPS,
                                    LED_BLANK_RAMP_MS)));
    delay(LED_BLANK_RAMP_MS / STEPS);
  }
  strip.clear();
  strip.show();
  return true;
}

void led_light_blank_end() {
  const uint8_t STEPS = 6;
  for (uint8_t s = 1; s <= STEPS; s++) {
    blank_step(ramp((uint32_t)s * LED_BLANK_RAMP_MS / STEPS, LED_BLANK_RAMP_MS));
    if (s < STEPS) delay(LED_BLANK_RAMP_MS / STEPS);
  }
  dark_latched = false;
}
