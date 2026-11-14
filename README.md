# ELRS Sim RX

An ExpressLRS receiver to USB gamepad adapter for FPV simulators, built on an RP2040. Plug it
in, bind your radio to it, and the radio shows up as a game controller in Liftoff,
Velocidrone, Uncrashed, Tryp and anything else that reads a standard HID gamepad. You fly the
sim over the same radio link, packet rate and radio setup you fly for real.

## How this was made

All of the code and all of the hardware design in this repository were produced with AI:
Anthropic's Claude, working through Claude Code. That covers the firmware, the configuration
page, the tests, the schematics and PCB layouts, the printed cases and the documentation.

A person directed the work, built the hardware, and tested it on the bench and in simulators.
Read it with that in mind: parts of it have had real use, parts have only passed automated
checks, and the status table below says which.

## Status

| Part | State |
|---|---|
| Firmware core: gamepad, CRSF, failsafe, receiver power, bind, model match, passthrough | In daily use on the perfboard prototype |
| Newer firmware features: profiles, the lite build, the onboard status LED, bootloader without the drive | Pass the automated tests; little or no time on hardware yet |
| Configuration page | In use; the four-tab layout, profiles and one-click update are tested against a simulated device only |
| Prototype hardware (breadboard, then perfboard) | Built and working |
| Carrier PCB v0.2 | Ordered, not yet received or tested |
| Carrier case | Designed and checked for overlaps in CAD; not printed yet |
| Hosting on GitHub Pages, release and development builds | Workflow written, never run |

## What it does

- **Gamepad:** 8 axes at 16-bit resolution, with the full 11-bit CRSF channel range kept, and
  36 buttons derived from switch channels. A report goes out on every CRSF frame, up to the
  1 kHz USB polling rate.
- **Failsafe:** sticks centred and throttle low within 100 ms of losing the link.
- **Receiver power:** a front-panel button switches the receiver off and on, so the dongle
  can stay plugged in while you fly something else. A long press puts the receiver into bind
  mode.
- **Status light:** the button's LED ring mirrors the receiver's own status LED patterns.
- **Profiles:** 16 named channel-to-axis and channel-to-button mappings stored on the dongle.
- **Receiver updates:** Betaflight-compatible passthrough, so the ExpressLRS Web Flasher or
  Configurator can update the receiver and set its bind phrase through the dongle.
- **Receiver settings:** model match, and on ExpressLRS 4.0 or newer the receiver's own
  settings, from the configuration page.
- **No drivers:** a standard USB gamepad plus a serial port on Windows, Linux and macOS.

## Hardware

The current hardware is a small carrier PCB: the fab fits the power switch and LED driver, and
an RP2040-Zero module, the receiver and the button leads are soldered on by hand.
**[docs/HARDWARE.md](docs/HARDWARE.md)** is the index.

| | Page |
|---|---|
| Carrier PCB v0.2 | [hardware/carrier/](hardware/carrier/README.md) |
| Case for the carrier | [hardware/case-carrier/](hardware/case-carrier/README.md) |
| Parts list, pin plan, costs | [docs/HBOM.md](docs/HBOM.md) |
| Bring-up checklist | [docs/BRINGUP.md](docs/BRINGUP.md) |
| What the status light means | [docs/LED-BEHAVIOUR.md](docs/LED-BEHAVIOUR.md) |

Hardware still being worked on lives on the `devel` branch: a board with the RP2040 fitted
directly, and the step-by-step guides and case for the hand-built prototype that proved the
design.

The simplest build needs no PCB at all: an RP2040-Zero and a receiver joined by four wires
(5V, GND, GP0 to the receiver's RX pad, GP1 to its TX pad), running the lite firmware.

## Firmware

Pico SDK and TinyUSB, in C. One build tree produces two files:

| File | For |
|---|---|
| `elrs_sim_rx.uf2` | Any board with the power switch, button and LED ring |
| `elrs_sim_rx_lite.uf2` | A module wired straight to a receiver. The receiver is on whenever USB is |

To flash: hold BOOT while plugging the board in, then copy the file onto the `RPI-RP2` drive
that appears. After that, updates can be done from the configuration page.

Details, the console commands and the flash layout are in
[docs/FIRMWARE.md](docs/FIRMWARE.md).

## Configuring

`web/index.html` is a single static page that talks to the dongle over Web Serial, so it
needs Chrome or Edge on a desktop. Click Connect and pick the device. It has four tabs:

- **Controller:** profiles and saving, live channels, the axis and button mapping, link
  status, receiver power, bind, model match and the receiver's own settings.
- **Receiver firmware:** how to update the receiver's ExpressLRS firmware through the dongle.
- **Dongle firmware:** update the dongle itself.
- **Console:** the raw console and counters.

Changes apply at once; **Save to device** writes them to the active profile. Adding `?mock`
to the address runs the page against a simulated device.

The same serial port answers a plain terminal at any baud rate: type `help`.

See [docs/WEBUI.md](docs/WEBUI.md) for how the page works and is hosted, and
[docs/WINDOWS.md](docs/WINDOWS.md) for the one optional Windows driver step.

## Building

Everything runs in Docker; nothing else needs installing on the host.

```
tools/build.sh        # firmware -> firmware/build/elrs_sim_rx.uf2 and elrs_sim_rx_lite.uf2
tools/test.sh         # host unit tests for the hardware-independent core
tools/test-web.sh     # configuration page and bootloader tests (Playwright image)
tools/serve.sh        # serve the page and the latest firmware over HTTPS on the LAN
tools/flash.sh        # flash a board plugged into this machine (Linux)
tools/shell.sh        # shell inside the build container
```

`tools/serve.sh` is the easy way to work when the build machine and the PC with the dongle
are different computers: open the address it prints in Chrome or Edge on that PC.

Hardware builds (PCB generation and checks, order files, case exports) are listed in
[docs/HARDWARE.md](docs/HARDWARE.md).

## Layout

| Path | Contents |
|------|----------|
| `firmware/` | Firmware. `core/` is hardware independent and unit tested; `src/` is the RP2040 side |
| `tests/` | Host unit tests, and browser tests for the configuration page |
| `web/` | The configuration page and the bootloader flasher |
| `hardware/` | KiCad project for the carrier PCB, its generator scripts, and its OpenSCAD case |
| `docs/` | Hardware index, parts list, bring-up checklist, firmware and page notes |
| `tools/` | Build container and helper scripts |
| `.github/workflows/` | CI: build, test, and publishing of the page and firmware |

Only sources are committed. Firmware binaries, PCB renders and order files, case STLs and the
wiring PDF are generated by the commands above.

## License

Firmware and documentation are licensed under the Apache License 2.0. See `LICENSE`.
