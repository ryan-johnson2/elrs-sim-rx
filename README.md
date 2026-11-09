# ELRS Sim RX

An ExpressLRS receiver to USB HID gamepad adapter for FPV simulators, built on an
RP2040. Plug it in, bind your radio to it, and the radio shows up as a game controller
in Liftoff, Velocidrone, Uncrashed, Tryp and anything else that reads a standard HID
gamepad.

## Features

- 8 axes at 16-bit resolution (full 11-bit CRSF channel resolution preserved), up to 36
  buttons derived from switch channels, 1 kHz USB polling, report sent on every CRSF frame.
- Failsafe: throttle low and sticks centred within 100 ms of link loss.
- Receiver power switched from a front-panel button, so the dongle can stay plugged in
  with the receiver off. Long press puts the receiver into bind mode.
- The button's LED ring mirrors the receiver's own status LED patterns.
- USB serial console for live channel readout and channel-to-axis/button mapping, saved
  to flash. Same port serves a browser-based config UI via Web Serial.
- Betaflight-compatible passthrough so the ExpressLRS Web Flasher can update the receiver
  firmware and bind phrase through the dongle.

## Layout

| Path | Contents |
|------|----------|
| `firmware/` | Pico SDK + TinyUSB firmware in C |
| `tests/` | Host-side unit tests for the hardware-independent core (CRSF parser, mapping, failsafe, LED sequencer) |
| `tools/` | Docker build environment and helper scripts |
| `docs/` | Hardware BOM, pin plan, LED behaviour spec |

## Building

All builds run inside a Docker image that holds the Pico SDK, ARM GCC and picotool.
Nothing needs to be installed on the host except Docker.

```
tools/build.sh        # firmware -> firmware/build/elrs_sim_rx.uf2
tools/test.sh         # host unit tests
tools/shell.sh        # shell inside the container
```

Flash: hold BOOT on the RP2040-Zero while plugging it in, then copy the `.uf2` onto the
`RPI-RP2` drive that appears.

## Hardware

See `docs/HBOM.md` for parts, pin plan and the Phase 1 schematic, and
`docs/LED-BEHAVIOUR.md` for the status LED spec.

## License

Firmware and documentation are licensed under the Apache License 2.0. See `LICENSE`.
