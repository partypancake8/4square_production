#!/usr/bin/env bash
# Build pageshot and render one page variant across all four slots.
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
g++ -std=c++17 -O2 -w -DARDUINO=10819 \
    -I ../layoutcheck/shim -I "$FW" -I "$FW/screens" -I "$FW/settings" -I "$GFX" \
    pageshot.cpp "$FW/screens/faces.cpp" "$FW/screens/pages.cpp" \
    "$FW/screens/anim.cpp" "$FW/settings/defaults.cpp" "$GFX/Adafruit_GFX.cpp" \
    -o "$OUT/pageshot"
"$OUT/pageshot" "$OUT" CLOCK "${1:-5}" "${2:-0}"
