# Front-panel LED behaviour

Goal: the green LED ring in the front-panel pushbutton mirrors the ExpressLRS receiver's
own status LED, so the enclosure needs no light pipe lined up with the RX.

The RP2040 drives the ring LED and reproduces the RX patterns from what it can observe on
the CRSF UART plus what it did itself (powered the RX, requested bind). It never taps the
RX's LED pad.

## ExpressLRS receiver LED sequences (source: `src/lib/LED/devLED.cpp`, master 2026-11-08)

Durations are on/off pairs in milliseconds.

| RX state | Pattern | Sequence |
|----------|---------|----------|
| connected | solid on | |
| disconnected (no TX in range) | slow blink | 500 on, 500 off |
| binding | double blink | 100 on, 100 off, 100 on, 1000 off |
| wifiUpdate | fast flicker | 20 on, 30 off |
| model mismatch (connected, wrong model id) | triple blink | 100 on, 100 off, 100 on, 100 off, 100 on, 1000 off |
| radioFailed / noCrossfire | short blip | 200 on, 1000 off |
| serialUpdate (passthrough flashing) | blip, two flickers | 200 on, 50 off, 50 on, 50 off, 50 on, 400 off |

## What the RP2040 can observe

| Observation on CRSF UART | Meaning |
|--------------------------|---------|
| RC channel frames (0x16) arriving | RX connected to TX. |
| Link statistics frames (0x14) arriving, no RC frames | RX powered and running, no TX link. The RX sends link stats every interval even while disconnected (`checkSendLinkStatsToFc`). |
| No frames at all for more than about 1 s while RX power is on | RX is in WiFi mode, in the ROM bootloader, or not responding. The RX enters WiFi automatically after `wifi-on-interval` seconds without a connection (default 60 s). |
| RX power is off (we switched it off) | Nothing expected. |

Plus the RP2040's own knowledge: it powered the RX on at time T, it sent the `bd` bind
command or ran the power-cycle bind sequence, it is in passthrough mode.

## Mapping to the ring LED

| Firmware state | Ring LED | Matches RX |
|----------------|----------|------------|
| RX power off | off | RX LED is also off |
| RX on, RC frames flowing | solid | connected |
| RX on, link stats only | 500/500 slow blink | disconnected |
| RX on, bind requested by us (until RC frames arrive or timeout) | 100/100/100/1000 double blink | binding |
| RX on, no frames for > 1 s | 20/30 fast flicker | wifiUpdate (or RX dead; the user sees the same thing the RX LED would show) |
| Passthrough active | 200/50/50/50/50/400 | serialUpdate |
| USB not enumerated | off | n/a |

Not reproduced: model mismatch (not relevant for a sim dongle; no RC frames are output in
that state so it would show as "disconnected"), radioFailed (hardware fault, would show as
fast flicker). Both are acceptable approximations.

## Hardware

The ring LED has an internal series resistor sized for 12 V and is rated 3 to 24 V. Drive
it from the 5 V rail, switched low-side by a small N-channel MOSFET from a GPIO:

```
5V ──► LED(+)   LED(−) ──► drain
GPIO ──[100R]──► gate         2N7000 or BSS138
                 source ──► GND
100k gate pull-down to GND so the LED is off at reset
```

Do not sink the LED directly into an RP2040 pin from 5 V. The RP2040 is not 5 V tolerant,
and the LED's internal resistor is not sized for 3.3 V drive either (it would be dim).

PWM on the gate gives brightness control if the ring is too bright in a dark room.

## Consequence for the onboard WS2812

The RP2040-Zero's WS2812 is no longer needed for link state. Keep it for debug during
development, optionally repurpose it later for LQ (green to red gradient) if a light pipe
ever gets added. Not a Phase 1 concern.
