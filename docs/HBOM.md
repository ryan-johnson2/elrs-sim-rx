# Hardware Bill of Materials

ELRS receiver to USB HID gamepad adapter for FPV simulators.

## Phases

| Phase | Goal | Build | Status |
|-------|------|-------|--------|
| 1a | Prove the firmware: HID, CRSF, failsafe, RX power toggle, bind, passthrough, LED mirroring | Breadboard | parts ordered |
| 1b | First usable units in a 3D printed case, same parts moved to perfboard | Perfboard | after 1a |
| 2 | Custom carrier PCB with SMD load switch, module soldered in and receiver pads | KiCad, 2-layer | v0.2 in `hardware/carrier/`, 5 boards ordered 2026-11-14 |

Every Phase 1 part is chosen so it moves from breadboard to perfboard without substitution.
Phase 2 swaps the discrete power switch for an integrated load switch and nothing else changes electrically.

Status key: `ordered`, `on hand`, `to order`, `deferred`.

## Phase 1 parts (breadboard and perfboard)

| # | Item | Part / spec | Qty | Source | Status | Notes |
|---|------|-------------|-----|--------|--------|-------|
| 1 | Microcontroller board | DWEII RP2040-Zero (Waveshare RP2040-Zero clone). RP2040, 2 MB flash, USB-C, WS2812 on GP16, BOOT + RESET buttons, castellated edges, 18 x 23.5 mm | 6 | [Amazon B0GZMDGT7Z](https://www.amazon.com/dp/B0GZMDGT7Z) | ordered | Ships with male headers pre-soldered. Drops straight into the breadboard. On perfboard, solder the pins through directly (board sits about 2.5 mm above the perfboard) or use female header sockets to keep it removable. Flat castellation mounting is not an option without desoldering the headers. Run `picotool info -a` on arrival and log the flash chip per board. |
| 2 | ELRS receiver | Happymodel EP2. ESP8285 + SX1280, ceramic antenna, 10 x 10 x 6 mm, 5 V in, pads 5V / GND / TX / RX, boot button, status LED. Firmware target `HappyModel_EP_2400_RX` | TBD | [Amazon B092J1VP6S](https://www.amazon.com/dp/B092J1VP6S) | ordered | Update over its WiFi hotspot (10.0.0.1, password `expresslrs`) before first use. Confirm pad labels on arrival. |
| 3 | Front-panel pushbutton with LED ring | FILN 12 mm ultra-thin momentary, IP67, black shell, green LED ring 3 to 24 V with internal resistor, 13.4 mm tall, pre-wired 15 cm 26 AWG leads with plug. 3-pack | 1 pack | [Amazon B0FN6Z1Q45](https://www.amazon.com/dp/B0FN6Z1Q45) | ordered | Contact to GND on a GPIO. LED ring driven by item 5 to mirror the RX status LED (LED-BEHAVIOUR.md). Case hole 12.2 mm. Confirm momentary variant. |
| 4 | RX power switch, P-channel MOSFET | AO3401A (SOT-23 marking A19T), logic-level, on a SOT-23-3 to DIP adapter | 2 + adapter pack | FET: [Amazon B09WN8WP8M](https://www.amazon.com/dp/B09WN8WP8M); SOT-23-3 adapters: [Amazon B0GHNCK1LG](https://www.amazon.com/dp/B0GHNCK1LG) | ordered | High-side on the RX 5 V rail. Gate pulled low by a 2N7000 from a GPIO, 10 kΩ pull-up to 5 V so the RX is off at boot. About 6 mm square on the adapter. TO-92 P-FETs are unsuitable (5 to 14 Ω on-resistance); TO-220 IRF9540N works but is bulky. |
| 5 | N-channel MOSFETs | 2N7000, TO-92 | 2 per build | [Amazon B0CBKHJQZF](https://www.amazon.com/dp/B0CBKHJQZF) | ordered | One drives the LED ring from 5 V, one pulls the P-channel gate low. 100 Ω gate resistor and 100 kΩ pull-down on each. |
| 6 | Passives | 100 Ω, 10 kΩ, 100 kΩ resistors; 10 µF electrolytic or ceramic, 6.3 V or higher; 100 nF ceramic | from kit | ELEGOO kit (item 7) | ordered | 10 µF plus 100 nF across RX 5 V and GND, close to the receiver. |
| 7 | Breadboard kit | ELEGOO Upgraded Electronics Fun Kit: 830-point breadboard, M-M jumpers, resistor assortment, electrolytic and ceramic caps, PN2222, LEDs, MB102 power module | 1 | [Amazon B09YRJQRFF](https://www.amazon.com/dp/B09YRJQRFF) | ordered | Do not use the MB102 module; the board runs from USB 5 V. |
| 8 | Perfboard (Phase 1b) | 2.54 mm pitch, double-sided plated through-hole, roughly 4 x 6 cm or cut to size | 2 | on hand | on hand | Small enough for the case; RP2040-Zero, SOT-23 adapters and TO-92s all sit on 2.54 mm pitch. |
| 9 | Enclosure (Phase 1b) | 3D printed, two-part, hole for the FILN button and cutout for USB-C | 1 | own print | deferred | No light pipe needed: the button ring mirrors RX status. |
| 10 | USB-C data cable | Data-capable | 1 | on hand | on hand | |
| 11 | Hookup wire | 28 to 30 AWG silicone, 4 colours | | on hand | on hand | Red 5V, black GND, green MCU TX, blue MCU RX. |
| 12 | Jumpers, optional female headers | M-M and M-F Dupont; 2.54 mm female header strip if the RP2040-Zero should be socketed on perfboard | | on hand | on hand | Male headers no longer needed; the RP2040-Zero ships with them fitted. |
| 13 | Heat shrink or tape | | | on hand | on hand | Insulate the RX against the board. |

## Pin plan (RP2040-Zero)

| GPIO | Direction | Function | Connects to |
|------|-----------|----------|-------------|
| GP0 | out | UART0 TX, CRSF out (bind command, bootloader command, passthrough) | RX pad "RX" |
| GP1 | in | UART0 RX, CRSF in at 420000 baud | RX pad "TX" |
| GP2 | out | RX power enable | 2N7000 gate, which pulls the AO3401A gate low |
| GP3 | in, pull-up | Front-panel button, active low | FILN switch contact, other side to GND |
| GP4 | out | LED ring driver | 2N7000 gate; LED ring from 5 V to 2N7000 drain |
| GP16 | out | Onboard WS2812 | debug only |

GP0 and GP1 must be set to high-impedance inputs whenever RX power is off, so neither line back-powers the receiver.

Prior-art firmware (CRSFJoystick family, agtbaskara) uses GP4 TX / GP5 RX on UART1. To run their prebuilt UF2 as a baseline, wire temporarily to GP4 / GP5.

## Phase 1 schematic (text)

```
USB 5V ──┬──────────────────────────────┬── FILN LED(+)
         │                              │
        [10k]                      FILN LED(−)
         │                              │
         ├── AO3401A source         2N7000 drain  (LED driver)
         │   AO3401A gate ──┐      GP4 ─[100R]─ gate, 100k to GND
         │   AO3401A drain ─┼── RX 5V      source ── GND
         │                  │       └──[10uF + 100nF]── GND
         │            2N7000 drain  (power switch driver)
         │     GP2 ─[100R]─ gate, 100k to GND
         │            source ── GND
RX GND ── GND
RX TX  ── GP1          RX RX ── GP0
FILN switch: one contact GP3, other GND
```

## Phase 2: carrier board (draft in `hardware/carrier/`)

Only the differences from Phase 1 are listed. Everything else carries over.

| # | Item | Replaces | Notes |
|---|------|----------|-------|
| 20 | Integrated load switch TPS22918, SOT-23-6 | AO3401A + 2N7000 + 10 kΩ | Logic-level ON, controlled slew, quick output discharge. GP2 drives ON directly, with a 100 kΩ pull-down so the RX stays off until firmware runs. |
| 21 | 2N7002, SOT-23 | 2N7000 LED driver | Same circuit, SMD. |
| 22 | 0603 passives | kit passives | |
| 23 | RX landing: 4 SMD pads matching the EP2 castellated edge | hand wiring | RX soldered flat, castellations onto pads, RX5808 style. Footprint is receiver-specific. |
| 24 | Two 1x9 and one 1x5 female sockets, 2.54 mm, 8.5 mm tall | perfboard mounting | Primary option: the same pre-soldered RP2040-Zero plugs in (headers on both sides and on the end opposite USB). The SMD parts sit under the module. Flat castellated mounting or a bare RP2040 stay as possible later variants. |
| 25 | 2-layer PCB, 22 x 39.1 mm, KiCad | perfboard | JLCPCB or similar, 5 minimum. Fab assembles the SMD parts (top side only); sockets, RX and button leads are hand-soldered. |
| 26 | SWD pads | none | Only if debugging without USB. |

## Open decisions

- Quantity of EP2 receivers (item 2). Two is the useful minimum.
- Flash chip per RP2040-Zero clone, logged from `picotool info -a` on arrival.
- Phase 2: items listed under "Check before ordering" in `hardware/carrier/README.md`.

## Change log

- 2026-11-09: Initial HBOM. Target board switched from Seeed XIAO RP2040 to DWEII RP2040-Zero clone.
- 2026-11-09: Receiver chosen: Happymodel EP2.
- 2026-11-09: Front-panel button: FILN 12 mm momentary, black, green LED ring. Ring LED mirrors RX status via firmware; added LED-BEHAVIOUR.md.
- 2026-11-09: RX power switch changed from Pololu 2808 to AO3401A on SOT-23 adapter, driven by 2N7000.
- 2026-11-09: Restructured around Phase 1a breadboard, 1b perfboard in case, 2 carrier board. Added pin plan and text schematic. Removed TPS22918-on-adapter test option as redundant.
- 2026-11-09: DWEII boards arrive with headers pre-soldered; perfboard mounting is through-hole or socketed, not castellated.
- 2026-11-10: Phase 2 carrier drafted in KiCad (`hardware/carrier/`). Primary option is the socketed pre-soldered RP2040-Zero with the RX soldered on and fab-assembled SMD. Load switch fixed as TPS22918, LED driver as 2N7002.
- 2026-11-14: Carrier v0.2 ordered from JLCPCB, 5 boards assembled (top side, 9 parts each). Actual cost: boards $4, assembly including parts about $53 (Standard PCBA tier), shipping $27, import duties about $36, total $120, so $24 per assembled board. Per finished unit with receiver, module, button and case: roughly $47. For a production run, revisit the assembly tier, shipping method and order quantity first.
