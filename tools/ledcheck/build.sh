#!/usr/bin/env bash
# Build and run the LED prover.
#
# Same contract as tools/layoutcheck/build.sh, and for the same reason: this
# compiles the FIRMWARE'S OWN leds.cpp -- the source that ships -- so what it
# proves is a property of the shipping binary and not of a model of it. The only
# things modelled are the WS2812 strip (shim/Adafruit_NeoPixel.h), which is the
# part the PC does not have, the 24LC256 behind `cfg` (shim/Wire.h), and
# disp_light_window(), which belongs to the display layer and is documented as
# modelled at the top of ledcheck.cpp.
#
# store.cpp and defaults.cpp come along because leds.cpp reads `cfg` -- the real
# led_mode and led_bright the device boots with. Linking the real ones rather
# than declaring a stub `cfg` is the same discipline layoutcheck works under: a
# stub would let the LED layer agree with a settings inventory the device does
# not have. faces.cpp and Adafruit_GFX follow defaults.cpp, which calls
# widget_allows().
#
# The shim here is a FULL one rather than a borrow from a sibling prover: this
# tree has no storecheck to share Arduino.h/Wire.h with.
#
# Nonzero exit means do not flash.
#
# scriptcheck exception: DONE marker. This is a prover, not an action script,
# and its finish marker is the line the gate greps for -- "RESULT: PASS" or
# "RESULT: FAIL -- n of m checks failed", printed by ledcheck.cpp's main().
# A second, different DONE line would be a second thing that could disagree
# with the exit code. Same convention as tools/layoutcheck/build.sh.
set -e
cd "$(dirname "$0")"

FW=../../firmware/foursquare/src
if [ -z "${GFX:-}" ]; then
  for c in "$HOME/Arduino/libraries/Adafruit_GFX_Library" \
           "$HOME/Documents/Arduino/libraries/Adafruit_GFX_Library"; do
    [ -f "$c/Adafruit_GFX.cpp" ] && GFX="$c" && break
  done
fi
OUT=out
mkdir -p "$OUT"

DEMO_FLAGS=()
if [ "${DEMO:-0}" = "1" ]; then DEMO_FLAGS=(-DDEMO_BUILD); fi

g++ "${DEMO_FLAGS[@]}" -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
    -DARDUINO=10819 \
    -I shim -I "$FW" -I "$FW/screens" -I "$FW/settings" -I "$GFX" \
    ledcheck.cpp \
    "$FW/app/leds.cpp" \
    "$FW/settings/store.cpp" "$FW/settings/defaults.cpp" \
    "$FW/screens/faces.cpp" \
    "$GFX/Adafruit_GFX.cpp" \
    -o "$OUT/ledcheck"

exec "$OUT/ledcheck"
