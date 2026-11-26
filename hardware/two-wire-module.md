# Two-wire power-and-call module (option B)

Proposed 2026-11-26. All thermostats in the house have only two wires. Rather than harvesting power, a module at the far end of each thermostat cable puts continuous 24 VAC on the two wires and turns the thermostat's heat call back into a contact closure. This replaces the series harvester, triac, idle harvester, Li-ion cell and charger in [power-supply.md](power-supply.md); that design remains as the fallback.

## Zones

| Zones | Load | Module location | Module's 24 VAC (R, C) | Module contact replaces |
|---|---|---|---|---|
| 3 radiant | Erie SR100 switching relay driving Uponor thermal actuators | Beside the SR100s | SR100 R and C terminals (C currently unused) | Old thermostat across the SR100 thermostat terminals |
| 5 hydronic | Honeywell V8043-family zone valve (40004850-001 power head), 24 VAC 0.32 A, measured 0.336 A | At the zone valve transformer | Both ends of that transformer's 24 V winding | Old thermostat in series with the valve motor |

- Likely build: one 5-channel board for the hydronic zones and one 3-channel board for the radiant zones, from the same channel circuit.
- To check: whether the five hydronic valves share one transformer, and its VA rating. Five valves open at once is about 38 VA, plus about 5 VA for five thermostats; a 40 VA transformer would be marginal. Each SR100 has 9 VA available for external loads.
- A Taco SR506 on hand is a 120 VAC circulator relay, so it cannot drive the 24 V zone valves. Possible use: parts or enclosure only.

## Channel circuit (remote module)

- R → F1 (PTC, about 0.25–0.5 A) → cable wire **L1**. TVS D3 across R and C.
- Cable wire **L2** → node X.
- **Power half-cycle:** D1 from X to C (anode at X). Thermostat supply current returns this way.
- **Call half-cycle:** C → R1 → D2 → K1 coil → X, with C2 across the coil. Current flows only when the thermostat calls.
- K1: small PCB relay, 24 VDC coil (about 15–25 mA), normally open contact rated at least 1 A at 24 VAC (V8043 motor 0.32 A). The contact goes to terminals T1 and T2, wired where the old thermostat connected.
- C2: about 47–100 µF, 50 V, to hold K1 through the power half-cycle.
- R1: sized to the chosen coil, since C2 charges toward about 33 V. Probably a few hundred ohms.

## Thermostat input

- L1 → D4 → C3 (about 470 µF, 63 V) → buck converter (60 V or more at its input) → 5 V for the LilyGO. Thermostat ground is **L2**.
- **Call:** L2 → D5 → U2 (PhotoMOS solid-state relay, 60 V or more, at least 100 mA, LED driven from IO10) → L1. D5 blocks the power half-cycle so U2 cannot short the supply during calls.
- TVS across L1 and L2.
- Power available: roughly 1–2 W from the power half-cycle.

## Fail-safe behaviour

- Thermostat dead, unplugged or crashed: U2 off, K1 drops out, zone off.
- Cable cut: zone off.
- Cable shorted: the power half-cycle drives a large current through D1 and F1 opens. K1 may pull in briefly, then everything is off.

## Diodes

D1, D2, D4, D5: 1N4004 class.
