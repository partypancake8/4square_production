#!/usr/bin/env bash
# Build and run the layout prover, then render the contact sheet.
#
# This compiles the FIRMWARE'S OWN faces.cpp, pages.cpp, anim.cpp and
# defaults.cpp -- the source that ships -- against the real Adafruit_GFX library
# the device links, so what it proves is a property of the shipping binary and
# not of a model of it.
set -e
cd "$(dirname "$0")"

FW=../../firmware/foursquare/src
# Linux keeps the library in ~/Arduino; the Arduino IDE on macOS keeps it in
# ~/Documents/Arduino. $GFX overrides both.
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
    layoutcheck.cpp \
    "$FW/screens/faces.cpp" "$FW/screens/pages.cpp" \
    "$FW/screens/anim.cpp" "$FW/settings/defaults.cpp" \
    "$GFX/Adafruit_GFX.cpp" \
    -o "$OUT/layoutcheck"

set +e
"$OUT/layoutcheck" "$OUT"
RC=$?
set -e

python3 contactsheet.py "$OUT" "$OUT/layout-contact-sheet.png"
echo
echo "contact sheet: $(realpath "$OUT/layout-contact-sheet.png")"
exit $RC
