# 4square bill of materials, one clock

Everything that goes into one finished clock. The machine-readable files are
`bom.csv` (JLCPCB assembly) and `kit-bom.csv` (the LCSC hand-solder kit).

## Assembled by JLCPCB (bottom side, from `bom.csv`)

| qty | part | refs | LCSC |
|---|---|---|---|
| 1 | PCA9548APW I2C mux, TSSOP-24 | U1 | C130026 |
| 1 | AP2112K-3.3 LDO, SOT-23-5 | U2 | C51118 |
| 1 | DS3231MZ RTC, SO-8 | U4 | C107410 |
| 1 | SHT31-DIS-B temperature and humidity, DFN-8 | U5 | C80862 |
| 1 | SN74AHCT1G125 buffer, SOT-23-5 | U6 | C7484 |
| 1 | SRV05-4 ESD array, SOT-23-6 | U7 | C85364 |
| 3 | GL5050RGB01H-W addressable RGB LED | D2, D4, D5 | C51933368 |
| 1 | SMAJ5.0A TVS, SMA | D3 | C87074 |
| 1 | 750 mA polyfuse, 1206 | F1 | C371166 |
| 1 | PT17-21C phototransistor, 0805 | R7 | C100090 |
| 4 | 1k 0603 | R8-R11 | C21190 |
| 1 | 470R 0603 | R12 | C23179 |
| 2 | 10k 0603 1% | R13, R14 | C25804 |
| 12 | 100n 0603 | C1, C5-C8, C10-C14, C16, C17 | C14663 |
| 4 | 10u 0805 | C2-C4, C18 | C15850 |
| 2 | 22u 0805 | C9, C15 | C45783 |

## Hand-soldered kit (from `kit-bom.csv`)

| qty | part | refs | LCSC |
|---|---|---|---|
| 1 | Keystone 3034 CR2032 holder | BT1 | C5199422 |
| 1 | USB-C receptacle, 6 pin, power only | J9 | C5187470 |
| 1 | 1N5819 Schottky, DO-41 | D1 | C110032 |
| 2 | 4.7k 1/4 W axial | R1, R2 | C173023 |
| 1 | 10k 1/4 W axial | R3 | |
| 1 | 1k 1/4 W axial | R4 | |
| 2 | 5.1k 1/4 W axial | R5, R6 | |
| 4 | 6x6 mm tactile switch | SW1-SW4 | C7528713 |
| 1 | 24LC256 EEPROM, DIP-8 | U3 | C19193485 |
| 1 | DIP-8 socket | U3 | C2874007 |
| 1 | 1x40 female header strip, cut to 4x 1x4, 2x 1x8, 1x 1x6 | J1-J6, J8 | C9811 |

## Modules and mechanical

| qty | part |
|---|---|
| 1 | ESP32-C3 SuperMini |
| 4 | 0.96 in SSD1306 128x64 I2C OLED module, all white, 4 pin |
| 1 | CR2032 cell |
| 4 | M3 x 16 mm self-tapping screws |
| 1 | 74 x 74 mm piece of 3 mm cast acrylic, laser cut (or the printed back) |
| about 55 g | PETG for the shell, retainer halves and standoff frame |
