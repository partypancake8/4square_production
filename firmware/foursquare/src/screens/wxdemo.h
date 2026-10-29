// wxdemo.h, THE BAKED FORECAST the showreel's weather segment shows.
//
// BAKED ON PURPOSE. The demo build has no radio at all (see demo.h), and the
// shipping build's WiFi cannot hold an association on this board, so a live
// forecast has nowhere to come from. What the segment demonstrates is the
// PRESENTATION, one day per panel, an animated 1-bit sky, the temperature  - 
// and for a marketing clip the weather should be pleasant anyway.
//
// This block is the WHOLE of the data: change the city and the four rows and
// the segment follows. Day names are labels, not derived from the RTC, a
// baked forecast with computed day names would claim a freshness it does not
// have. The TL panel shows the CITY (today is implied); the other three show
// the day.
//
// `kind` is a WxKind from anim.h: WX_SUNNY, WX_PARTLY, WX_RAIN. One rainy day
// is kept in deliberately, the rain drawing is the most charming of the
// three, and an all-sun forecast reads as a mockup.
#pragma once
#include <stdint.h>
#include "anim.h"   // WxKind

struct WxDay {
  const char *label;    // what the panel's top line says (city or day)
  uint8_t     kind;     // WxKind, which sky drawing plays
  int8_t      temp_f;   // shown as-is with an F suffix
};

static const WxDay WX_DEMO[4] = {
  { "DETROIT", WX_SUNNY,  78 },   // TL, the city; today
  { "FRI",     WX_PARTLY, 75 },   // TR
  { "SAT",     WX_RAIN,   68 },   // BL
  { "SUN",     WX_SUNNY,  80 },   // BR
};
