// PAGESHOT, render ONE page variant across all four slots, as the device
// draws it, so a human can look at it.
//
// The prover answers "is this legal". It cannot answer "why is CLOCK v5 over
// the lit-pixel budget", because the answer to that is a picture. This links
// the SAME pages.cpp / faces.cpp the firmware ships, so what comes out is the
// real frame and not an illustration of one.
//
//   ./pageshot <outdir> <page> <variant> [value]
//
// Writes frames.bin / frames.txt in the prover's dump format, which
// contactsheet.py and shot.py already know how to read.

#include <Adafruit_GFX.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "faces.h"
#include "pages.h"
#include "display.h"

static const int WBYTES = (SCR_W + 7) / 8;
static GFXcanvas1 canvas(SCR_W, SCR_H);

// pages.cpp calls this hook as it draws each element; we do not care which
// element is which here, only about the finished frame.
static void noop_hook(const char *) {}

int main(int argc, char **argv) {
  face_elem_hook = noop_hook;

  const char *outdir = argc > 1 ? argv[1] : ".";
  int variant        = argc > 3 ? atoi(argv[3]) : 5;
  int value          = argc > 4 ? atoi(argv[4]) : 0;

  char p1[512], p2[512];
  snprintf(p1, sizeof p1, "%s/frames.bin", outdir);
  snprintf(p2, sizeof p2, "%s/frames.txt", outdir);
  FILE *fb = fopen(p1, "wb"), *ft = fopen(p2, "w");
  if (!fb || !ft) { fprintf(stderr, "cannot write to %s\n", outdir); return 1; }

  // The same value sweep the prover uses, so a value quoted by the prover
  // lands on the same frame here.
  PageData d{};
  d.clock.hour    = (uint8_t)(value % 24);
  d.clock.minute  = (uint8_t)value;
  d.clock.second  = (uint8_t)value;
  d.clock.day     = (uint8_t)(1 + (value % 31));
  d.clock.month   = (uint8_t)(1 + (value % 12));
  d.clock.year    = (uint16_t)(2000 + value);
  d.clock.weekday = fx_weekday(d.clock.year, d.clock.month, d.clock.day);
  d.clock.hour24  = (value & 1) != 0;
  d.clock.temp_c10 = 231; d.clock.humidity = 46; d.clock.valid = true;

  for (uint8_t slot = 0; slot < N_SCREENS; slot++) {
    canvas.fillScreen(0);
    page_render(canvas, PG_CLOCK, slot, (uint8_t)variant, d);

    const uint8_t *buf = canvas.getBuffer();
    int lit = 0;
    for (int y = 0; y < SCR_H; y++)
      for (int x = 0; x < SCR_W; x++)
        if (buf[y * WBYTES + (x >> 3)] & (0x80 >> (x & 7))) lit++;
    double ff = 100.0 * lit / (SCR_W * SCR_H);

    fwrite(buf, 1, (size_t)WBYTES * SCR_H, fb);
    fprintf(ft, "CLOCK v%d slot%u  v=%d  %.1f%% lit\n", variant, slot, value, ff);
    printf("slot %u: %4d px lit, %.1f%%\n", slot, lit, ff);
  }
  fclose(fb); fclose(ft);
  printf("wrote %s\n", p1);
  return 0;
}
