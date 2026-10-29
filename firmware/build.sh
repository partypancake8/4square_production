#!/usr/bin/env bash
# Build the 4square firmware with arduino-cli.
#
#   firmware/build.sh demo          -> DEMO image (no radio, no secrets needed)
#   firmware/build.sh full          -> full image (WiFi + OTA, needs src/secrets.h)
#   firmware/build.sh demo --upload /dev/ttyACM0   -> build, then flash over USB
#   firmware/build.sh demo --no-check               -> skip the layout prover gate
#
# Before compiling, the layout prover (tools/layoutcheck) renders every screen
# the chosen variant can draw and the build stops if any of them fails. It needs
# g++ and Python with Pillow; --no-check skips it.
#
# Output lands in firmware/out/<variant>/ : foursquare.ino.bin (the app),
# foursquare.ino.merged.bin (bootloader + partitions + app, flash at 0x0),
# plus the .elf and .map.
#
# Requirements (see README "Building from source"):
#   arduino-cli, core esp32:esp32 3.x, libraries Adafruit GFX Library,
#   Adafruit SSD1306, Adafruit BusIO, Adafruit NeoPixel.
set -e
cd "$(dirname "$0")"

VARIANT="${1:?usage: build.sh demo|full [--upload PORT]}"
shift
case "$VARIANT" in
  demo) EXTRA="-DDEMO_BUILD" ;;
  full) EXTRA="" ;;
  *) echo "unknown variant '$VARIANT' (demo|full)"; exit 1 ;;
esac

PORT=""
CHECK=1
while [ $# -gt 0 ]; do
  case "$1" in
    --upload) PORT="$2"; shift 2 ;;
    --no-check) CHECK=0; shift ;;
    *) echo "unknown argument $1"; exit 1 ;;
  esac
done

# CDCOnBoot=cdc: the ESP32-C3 SuperMini has native USB CDC and no UART bridge,
# so Serial must be routed to USB. PartitionScheme=no_fs: 2 MB per app slot,
# OTA retained, no filesystem (settings live in the external 24LC256).
FQBN="esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=no_fs"
SKETCH="$PWD/foursquare"
OUT="$PWD/out/$VARIANT"

if [ "$VARIANT" = "full" ] && [ ! -f "$SKETCH/src/secrets.h" ]; then
  echo "FAILED: $SKETCH/src/secrets.h is missing."
  echo "Copy src/secrets.h.example to src/secrets.h and fill in your WiFi"
  echo "credentials, or build the demo image: build.sh demo"
  exit 1
fi

# -ffile-prefix-map strips the build machine's home directory out of the
# __FILE__ strings the ESP32 core bakes into its asserts, so the .bin does not
# carry anyone's local paths.
PMAP="-ffile-prefix-map=$HOME/="

if [ "$CHECK" = 1 ]; then
  echo "== layout prover ($VARIANT) =="
  if [ "$VARIANT" = demo ]; then DEMO=1 ../tools/layoutcheck/build.sh
  else ../tools/layoutcheck/build.sh; fi
  echo
fi

echo "== building $VARIANT ($FQBN) =="
rm -rf "$OUT"
arduino-cli compile --clean --jobs 4 --fqbn "$FQBN" \
  --build-property "compiler.c.extra_flags=$PMAP" \
  --build-property "compiler.cpp.extra_flags=$EXTRA $PMAP" \
  --output-dir "$OUT" "$SKETCH"
echo
ls -l "$OUT"/*.bin

if [ -n "$PORT" ]; then
  echo
  echo "== uploading to $PORT =="
  arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$OUT" "$SKETCH"
fi
echo
echo "DONE - $VARIANT build in $OUT"
