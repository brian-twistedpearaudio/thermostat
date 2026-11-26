# Prototype wiring

Bench prototype, 2026-11-25: LilyGO T-Display-S3 Touch, a BMP280 module and an Omron G6K-2P-DC5 relay driven by a BC550. The final board replaces the relay with a triac; see [power-supply.md](power-supply.md).

Header pin numbers follow the LilyGO numbering: P1 pin 1 is the 3V3 end, P2 pin 1 is the 5V end. Check against the board's silkscreen before soldering.

## Header pinout

| Pin | P1 | P2 |
|---|---|---|
| 1 | 3V3 | 5V (VBUS) |
| 2 | GND | GND |
| 3 | GND | NC |
| 4 | NC | NC |
| 5 | IO16 (touch INT) | IO13 |
| 6 | IO21 (touch RST) | IO12 |
| 7 | IO17 (I2C SCL) | IO11 |
| 8 | IO18 (I2C SDA) | IO10 |
| 9 | IO44 | IO3 |
| 10 | IO43 | IO2 |
| 11 | GND | IO1 |
| 12 | GND | 3V3 |

## BMP280 (all on header P1)

| BMP280 | LilyGO |
|---|---|
| VCC | P1-1 (3V3). Many BMP280 modules are 3.3 V only; do not use 5V |
| GND | P1-2 |
| SCL | P1-7 (IO17) |
| SDA | P1-8 (IO18) |
| CSB, SDO | Leave as the module comes |

- The I2C bus is shared with the CST816 touch controller (address 0x15). The board already has 10 kΩ pull-ups on IO17 and IO18.
- The firmware detects a BMP280, BME280 or BME688 at 0x76 or 0x77 by chip ID. A BMP280 has no humidity sensor.
- Mount the sensor a few inches from the board on short leads, or it reads warm.

## Relay driver (all on header P2)

| From | To |
|---|---|
| P2-8 (IO10) | 1 kΩ → BC550 base (pin 2) |
| BC550 base (pin 2) | 10 kΩ → GND (keeps the relay off during boot and reset) |
| BC550 emitter (pin 3) | GND (P2-2) |
| BC550 collector (pin 1) | G6K pin 8 (coil −) |
| G6K pin 1 (coil +) | 5V (P2-1) |
| 1N4148 | Across the coil: band (cathode) to pin 1, anode to pin 8 |
| G6K pin 3 (COM) and pin 4 (NO) | Boiler Rh and W |
| G6K pins 2, 5, 6, 7 | Unused |

- **BC550 pinout:** 1 collector, 2 base, 3 emitter, reading left to right with the flat face toward you and the legs down (onsemi/Fairchild). Older Philips/NXP parts are reversed (1 emitter, 3 collector); check yours.
- **G6K-2P pins:** 1 coil +, 8 coil −; pole 1: 3 COM, 2 NC, 4 NO; pole 2: 6 COM, 7 NC, 5 NO.
- **Normally open contact:** the boiler is called only while the relay is energised, so a reset, crash or power loss leaves it off.
- **Coil:** the 5 V coil draws 21.1 mA (237 Ω), well within the BC550's 100 mA.
- **Contact rating:** 0.3 A at 125 VAC. Check the boiler's W current; if it is near 0.3 A, use a larger relay or the triac design.
- **P2-1 (5V):** only live while USB is connected.
