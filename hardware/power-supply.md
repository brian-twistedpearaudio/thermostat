# Power supply and boiler interface

Working design, 2026-11-25. The architecture is agreed; values and parts are not final. It will be drawn in Fusion Electronics.

## Source

- Power is harvested from the boiler's **Rh–W** 24 VAC loop, which often measures about 26 VAC. There is no C wire.
- **During a heat call (series harvesting):** a series stage drops about 4 VAC in the R–W loop, and the boiler gets the remaining 20–22 VAC. Rectified, 4 VAC gives about 4.5–5 V DC after a Schottky or synchronous bridge. The power available is the clamp voltage times the loop current, about 1.1 W at 0.25 A.
  - A shunt clamp on the bridge's DC side (for example a TL431 driving a MOSFET) holds the drop constant.
- **While idle (trickle harvesting):** a high-impedance converter across the open switch draws a small current through the boiler's W input at full line voltage. It must stay below the current that makes the boiler start.

## Switching the boiler

- A **triac replaces the G6K relay** used in the prototype: a sensitive-gate part such as the Z0107 (IGT 5 mA in quadrants 1–3, 7 mA in quadrant 4).
- The gate is driven through an **opto-triac such as the MOC3023**, because with a series bridge harvester the circuit ground floats relative to R and W. The LED takes about 10 mA from GPIO10 through roughly 150–220 Ω, with a pull-down so it stays off during boot. Firmware is unchanged: GPIO10 HIGH calls for heat.
- An RC snubber across the triac (about 100 Ω plus 0.1 µF), or a snubberless part. Off-state leakage is about 1 mA at 26 VAC; check it against the boiler's drop-out current.
- The triac drops about 1–1.5 V when on, on top of the 4 V harvest drop. Check the boiler's minimum pull-in voltage.

## Protection

- A **fuse or PTC of about 0.5 A** in the R line, ahead of the TVS, so a miswire to 120 VAC blows the fuse.
- An **SMBJ43CA-13-F** bidirectional TVS across R–W: 43 V standoff, 47.8 V minimum breakdown, 69.4 V clamp at 8.6 A, 600 W. 26 VAC plus 10% peaks at about 40 V, so it does not conduct in normal use.
- Everything connected across the line (idle harvester, opto-triac, triac) is rated 100 V or more.

## Storage and charging

- A **protected Li-ion cell** (built-in protection board).
- Do **not** feed the harvested supply into the LilyGO's 5V pin. Its onboard TP4056 charger has no input current limit and would collapse the harvested rail.
- A charger on the carrier board with input-voltage regulation and power path, such as the **MCP73871** (VPCC). Its system output feeds the **LilyGO's battery connector**, so the onboard TP4056 stays idle (no VBUS) except while USB is plugged in for programming.
- The firmware already drives GPIO15 HIGH, which the display needs when running on battery.

## Measurements (2026-11-26)

| Loop | Thermostat | W current during a call | Harvest at ~4.5 V |
|---|---|---|---|
| Standard hydronic loop | Basic passive switch | 0.336 A | about 1.5 W |
| Radiant loop | Existing power-stealing thermostat | 0.04 A | about 0.18 W |

- **Hydronic, 0.336 A:** plenty of power, but unused harvest becomes heat in the enclosure: up to about 1.5 W in the shunt clamp, plus about 0.4 W in the triac and 0.2 W in the bridge, next to the temperature sensor. Plan a MOSFET that bypasses the series bridge once the cell is full, so the board harvests only when needed. The triac needs copper area or a larger package than TO-92. 0.336 A also exceeds the prototype G6K's 0.3 A contact rating.
- **Radiant, 0.04 A:** less than the board's average draw with Wi-Fi on, so the cell carries the board even during calls. Needs a larger harvest drop (if the controller accepts it), aggressive sleep, and a much slower server sync than the 10-second polling in `docs/sync.md`.
- Supporting both loops pushes the design toward the battery-centred version.

## Open items and measurements

1. ~~Boiler W loop current during a heat call.~~ Measured: 0.336 A (hydronic) and 0.04 A (radiant). Both are well above the triac's holding current.
2. The largest idle current from Rh to W that does not start the boiler. Start with about 10 kΩ (about 2.6 mA). On the radiant loop, the existing power-stealing thermostat's idle current (W current and Rh–W voltage while it is idle) is a safe reference. Decides whether the trickle covers the light-sleep load, and sizes the cell.
3. Boiler minimum pull-in voltage (the boiler sees about 20–21 V).
4. Firmware power management (display dimming or off, Wi-Fi duty cycling), depending on item 2.
5. Choice of harvester parts (bridge, clamp, idle converter) and cell capacity, once items 1 and 2 are known.
6. Which loops the thermostat must support (hydronic, radiant or both).
7. Idle harvester topology: its input floats relative to the series bridge's ground, so it probably needs a small isolated converter.
