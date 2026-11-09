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
| `web/` | Single-file Web Serial configuration page |
| `docs/` | Hardware BOM, pin plan, LED behaviour, firmware and web UI notes |

## Building

All builds run inside a Docker image that holds the Pico SDK, ARM GCC and picotool.
Nothing needs to be installed on the host except Docker.

```
tools/build.sh        # firmware -> firmware/build/elrs_sim_rx.uf2
tools/test.sh         # host unit tests
tools/shell.sh        # shell inside the container
```

Flash with `tools/flash.sh`. It reboots a running board into the bootloader over its
serial port, or you hold BOOT on the RP2040-Zero while plugging it in. The UF2 is copied
to the `RPI-RP2` drive, or loaded with picotool from the container if nothing auto-mounted.
For non-root USB access, install `tools/99-elrs-sim-rx.rules` into `/etc/udev/rules.d/`.

## Configuring

Open `web/index.html` in Chrome or Edge (Web Serial), click Connect, and pick the device.
It shows live channels, link stats, receiver power and bind controls, and the full
channel-to-axis and channel-to-button mapping. Changes apply instantly; "Save to device"
writes them to flash. `web/index.html?mock` runs against a simulated device.

The same port also answers a plain terminal at any baud rate: type `help`.

Receiver firmware and bind phrase are flashed through the dongle with the
[ExpressLRS Web Flasher](https://expresslrs.github.io/web-flasher/) using the
Betaflight passthrough method.

## Hardware

See `docs/HBOM.md` for parts, pin plan and the Phase 1 schematic, and
`docs/LED-BEHAVIOUR.md` for the status LED spec.

## License

Firmware and documentation are licensed under the Apache License 2.0. See `LICENSE`.
