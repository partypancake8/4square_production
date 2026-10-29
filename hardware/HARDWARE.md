# 4square hardware, rev O

A two-layer, 74 x 74 mm, 1.6 mm FR4 board. The four OLED modules plug into
sockets on the front; everything else is on the back, where a clear acrylic
cover shows it off. Surface-mount parts are on the back only, so the board can
be ordered as JLCPCB Economic PCBA (one side), and a short kit of through-hole
parts is soldered by hand afterwards.

Rev O is the board that was ordered and built (10 bare boards plus 10 assembled,
July 2026). It passed KiCad DRC with 0 violations and 0 unconnected items at
JLCPCB's real limits.

> The front silkscreen still reads "REV N - KIT EDITION". The back reads
> "4square REV O". Rev O is rev N with the RTC swapped to the SO-8 DS3231MZ, the
> SHT31 kept, and six silkscreen fixes; the front label was never updated.

## Files

| file | what it is |
|---|---|
| `4square-revO-gerbers.zip` | gerbers and drill file, upload as-is to JLCPCB |
| `bom.csv` | JLCPCB assembly BOM, 37 parts, LCSC part numbers |
| `cpl.csv` | JLCPCB pick-and-place file, bottom side, absolute coordinates |
| `kit-bom.csv` | the hand-solder kit, ordered from LCSC |
| `ORDER-NOTES.md` | how to place the order and what to check in the preview |
| `4square-revO-board.pdf` | plots of the board: front silk, front copper, back copper, back silk, back fab |
| `kicad/4square-revO.kicad_pcb` | the KiCad 9 board file |

The board was generated from a netlist script rather than drawn in the
schematic editor, so there is no `.kicad_sch`. The board file carries every net
and the PDF shows every layer. 3D model paths in the board file point at
`${KIPRJMOD}/models/`; the custom models are not included, so the 3D viewer
shows those parts as empty footprints.

## What is on it

| ref | part | where | notes |
|---|---|---|---|
| J5, J6 | ESP32-C3 SuperMini module | back, on 1x8 sockets | native USB-C, no UART bridge |
| J1-J4 | 4x SSD1306 0.96 in 128x64 OLED modules, I2C, 0x3C | front, on 1x4 sockets | all white panels, no yellow band |
| U1 | PCA9548A I2C mux, 0x70 | back, assembled | one OLED per channel; A0-A2 grounded; R3 10k on RESET |
| U2 | AP2112K-3.3 LDO, 600 mA | back, assembled | a dedicated 3.3 V rail for the screens, mux, pull-ups and light sensor |
| U3 | 24LC256 EEPROM, 0x50 | back, kit, socketed DIP-8 | settings store; WP grounded |
| U4 | DS3231MZ RTC, 0x68 | back, assembled | CR2032 backup in BT1; INT/SQW to GPIO20 with R14 10k pull-up |
| U5 | SHT31-DIS-B temperature and humidity, 0x44 | back, assembled | under the vent slots in the acrylic back |
| R7 | PT17-21C phototransistor, with R4 1k | back, assembled | ambient light for auto-dim |
| D2, D4, D5 | 3x GL5050RGB01H-W addressable RGB LEDs | back, assembled | vertical column, daisy-chained from GPIO7 through U6 74AHCT1G125 (5 V level shift) and R12 470R; R13 10k holds the input low at boot |
| SW1-SW4 | 6 mm through-hole tactile switches | back, kit | MODE, SET, UP, DOWN; each via a 1k series resistor (R8-R11) to GND, internal pull-ups |
| J9 | rear USB-C, power only | back, kit | everyday power; R5/R6 5.1k on CC1/CC2 |
| D1, F1, D3 | 1N5819 Schottky, 750 mA polyfuse, SMAJ5.0A TVS | | the J9 power branch; not in the path when powered from the SuperMini's own USB-C |
| J8 | 1x6 expansion header | back | 3.3 V, GND and spare mux channels 0 and 3, ESD-clamped by U7 SRV05-4 |
| R1, R2 | 4.7k axial | back, kit | main-bus I2C pull-ups |
| BT1 | Keystone 3034 CR2032 clip | back, kit | |
| H1-H4 | M3 mounting holes, 3.2 mm, at (4, 4) (70, 4) (4, 70) (70, 70) | | the case screws go through these |

The axial resistors on the back are deliberate: they are visible through the
acrylic and form a colour-band ladder.

## Pin map (ESP32-C3, from `firmware/foursquare/src/config.h`)

| GPIO | function |
|---|---|
| 0 | SW1 MODE |
| 1 | SW2 SET |
| 3 | SW3 UP |
| 10 | SW4 DOWN |
| 4 | light sensor, ADC1_CH4 |
| 5 | I2C SDA |
| 6 | I2C SCL |
| 7 | addressable LED data, through U6 |
| 20 | DS3231 INT/SQW (not used by the firmware) |

None of the button pins are C3 strap pins (2, 8, 9), so a stuck button cannot
stop the board booting. The SuperMini's onboard LED on GPIO8 is not usable on
this board; serial is the readout.

## Panel positions and mux channels

| connector | position | shows | PCA9548A channel |
|---|---|---|---|
| J1 | top-left | hours | 2 |
| J2 | top-right | minutes | 6 |
| J3 | bottom-left | weekday | 1 |
| J4 | bottom-right | date | 7 |

The firmware indexes panels by position (slot 0 = top-left). The channel
numbers come from the netlist, not from the silkscreen.

## I2C

The four OLEDs are behind the mux; the RTC, the sensor and the EEPROM are on
the main bus at 400 kHz. The `Adafruit_SSD1306` constructor's sixth argument
(`clkAfter`) defaults to 100 kHz and silently slows the whole bus; the firmware
passes 400000. A stuck slave can only be freed by clocking SCL nine times with
the pin open-drain, which `bus_recover()` does.

## Things the rev O silkscreen gets wrong

- **LED orientation.** The D2/D4/D5 footprint chamfer marks pin 1 (+5 V), but
  the GL5050RGB01H-W's own chamfer marks pin 3 (GND). The assembled rev O boards
  came back with all three LEDs reversed, which collapses the 5 V rail, and they
  were rotated by hand with hot air. Orient them by the datasheet and a
  continuity check to ground, not by the silk. On the back of the board, USB at
  the bottom, D2's pads are: top-left GND, top-right DOUT, bottom-left DIN,
  bottom-right +5 V. +5 V and GND are always diagonal.
- **R4** reads 10K on the silk. It is 1k.

## Bench notes

- The LED chain is daisy-chained: only D2 is driven from the board. With D2
  removed, D4 and D5 have no data. Any single-LED test has to tap `LED_DIN`
  (D2 pad 4, or the LED-side pad of R12).
- Never connect an LED's DOUT back toward U6. Two push-pull outputs tied
  together killed a test LED.
- The board is native USB CDC and does not reset on DTR/RTS.
- The WiFi association on this board is weak (around -80 dBm next to the
  router). The firmware runs entirely offline if it has to; the RTC keeps time.
