# Ordering rev O from JLCPCB

What was ordered in July 2026: 10 bare boards plus 10 assembled, JLCPCB
Economic PCBA, bottom side only, 74 x 74 mm, 2 layer, 1.6 mm, HASL. The
through-hole kit came from LCSC as a separate order.

## 1. Board and assembly

1. On jlcpcb.com, upload `4square-revO-gerbers.zip`. JLC should report a
   "2 layer board of 74x74mm". Any soldermask colour works. Keep tented vias.
2. Turn on PCB Assembly: Economic, **bottom side**. PCB quantity and assembly
   quantity are separate fields.
3. Upload `bom.csv` and `cpl.csv`. JLC should detect and confirm 37 parts on the
   bottom side. The CPL uses the same absolute coordinate frame as the gerbers,
   so every part should land on its pads with no offset warning.
4. Surface finish: HASL works. ENIG is a little safer for U5, the SHT31, which is
   a 0.5 mm pitch DFN with a thermal pad.

## 2. Check the placement preview before paying

JLC marks pin 1 only on orientation-critical parts. In the Component Placements
viewer a magenta pin-1 dot and a white triangle should sit together; apart means
the rotation is wrong.

| ref | CPL rotation | what to look for |
|---|---|---|
| U1, U2, U4, U5, U6 | as in `cpl.csv` | pin-1 dot and triangle together |
| U7 | 270 | pin-1 dot and triangle together |
| D3 | as in `cpl.csv` | cathode band toward the protected 5 V node |
| R7 | as in `cpl.csv` | JLC labels the pads C and E; collector (pin 1, +3V3) is the end away from the LIGHT silk |
| **D2, D4, D5** | as in `cpl.csv` | **these came back reversed on rev O.** The GL5050's chamfer marks pin 3 (GND), not pin 1. In the preview, make sure the LED's GND pin is on the pad that is continuous with ground, and rotate 180 degrees if it is not. See `HARDWARE.md`. |

Passives have no marker and their rotation is free.

## 3. Parts to re-check at order time

- **R12** (`C23179`, 470R 0603). Any 470 ohm 0603 basic part substitutes.
- **D2/D4/D5** (`C51933368`, GL5050RGB01H-W). If it shows as Standard-only,
  `C26167850` (TZ-5050S2RGB-5V-I4-H1) is a drop-in on the same footprint.
- `C80862` (SHT31) and `C130026` (PCA9548A) have had thin stock.
- **BT1**, the Keystone 3034 clip (`C5199422`), had single-digit stock. The
  tape-and-reel version `C5213768` (3034TR) is the fallback. The RTC runs
  without a cell; the clip only matters across power cuts.

The board uses the SHT31 instead of the cheaper AHT20 because JLC only
assembles the AHT20 on Standard PCBA, which adds a setup fee and 5 mm edge
rails and costs more overall.

## 4. The hand-solder kit (`kit-bom.csv`)

Ordered from LCSC. Quantities in the file are for five boards.

- rear USB-C (J9), CR2032 clip (BT1), 1N5819 (D1)
- axial 1/4 W resistors: 4.7k (R1, R2), 10k (R3), 1k (R4), 5.1k (R5, R6)
- four 6x6 mm tactile switches (SW1-SW4)
- 24LC256 EEPROM in DIP-8 (U3) and its socket
- one 1x40 female header strip, cut into 4x 1x4 (J1-J4), 2x 1x8 (J5, J6) and
  1x 1x6 (J8)

Plus a CR2032 cell, the ESP32-C3 SuperMini, and four 0.96 in SSD1306 I2C OLED
modules (all-white, 4-pin GND VCC SCL SDA).
