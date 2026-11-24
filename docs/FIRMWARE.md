# Firmware

Pico SDK + TinyUSB, C11. Single core, no RTOS, one main loop.

## Layout

```
firmware/
  core/        hardware-independent logic, also compiled by tests/
    crc.c        CRC-8 DVB-S2 (CRSF), CRC-32 (config block)
    crsf.c       CRSF byte-fed parser, RC channel unpack, link stats, frame builders
    mapping.c    config_t, channel -> axis/button mapping, HID report build, failsafe report
    failsafe.c   link-loss state machine with retry-until-delivered
    led_seq.c    ExpressLRS LED pattern sequencer
    button.c     debounce + short/long press
  src/         RP2040 specifics
    main.c           init and main loop, HID send, LED state selection, button actions
    cli.c            USB serial console, JSON output, Betaflight CLI emulation, passthrough bridge
    rx_uart.c        UART0 at 420000 with IRQ ring buffer, tri-state when RX is off
    rx_power.c       load switch GPIO, power-cycle bind sequence
    config_store.c   config_t in the last flash sector
    usb_descriptors.c  composite HID joystick + CDC
    board.h          pin assignments and timing constants
```

## Two builds

`tools/build.sh` produces two UF2 files from the same sources (`ELRS_LITE` in `CMakeLists.txt`):

| File | Hardware | Differences |
|------|----------|-------------|
| `elrs_sim_rx.uf2` (full) | Carrier or prototype with the receiver power switch, button and LED ring | Everything in this document |
| `elrs_sim_rx_lite.uf2` (lite) | RP2040-Zero wired straight to the receiver: 5V, GND, GP0 to the receiver's RX pad, GP1 to its TX pad | The receiver is on whenever USB is. GP2, GP3 and GP4 are not used. `rxpower`, `rxboot` and `bindcycle` answer with an error, and the config page hides them |

Both drive the module's onboard RGB LED (GP16, `src/status_led.c`) with the same on/off
pattern as the ring, in dim white so the clone's colour order does not matter. On the lite
build it is the only status light. `version` and `status` report `"variant":"full"` or
`"lite"`.

## Main loop

```
tud_task()
rx_power_task()                       power-cycle bind sequence, if running
deferred "bd" bind command            1.5 s after RX power-up
drain UART ring -> crsf_feed()        on each RC frame: build report, tud_hid_report()
failsafe_update()                     on engage: send failsafe report, retry every 20 ms
button_update()                       short = RX power toggle, long = bind
update_led()                          pick pattern, drive PIN_LED
cli_task()                            console or passthrough bridge
```

HID reports go out once per main-loop pass in which at least one RC frame was decoded,
carrying the freshest channels, never on a timer. At the ELRS packet rate (250 to 500 Hz)
that is one report per frame. The HID endpoint polls at 1 ms; if it is busy the report is
dropped and counted in `reports_dropped`, and the next pass carries the current state.

UART bytes are timestamped in the receive interrupt, and the CRSF parser's inter-byte
timeout uses those arrival times, so a slow main-loop pass cannot be mistaken for a gap on
the wire.

Console output is bounded: if the host stops draining the serial port, output is dropped
after 5 ms rather than stalling the loop. `status` reports `console_dropped` and
`console_truncated` so that shows up in the dev panel.

Saving the config erases and programs a flash sector with interrupts off for tens of
milliseconds. RC bytes received during that window are lost; afterwards the firmware drops
any partial frame, discards the UART backlog, and credits the stall to the link timestamps
so the failsafe does not trigger because of our own flash write.

## HID report

Usage page Generic Desktop, usage Joystick. 21 bytes:

| Offset | Field | Encoding |
|--------|-------|----------|
| 0..15 | 8 axes: X, Y, Z, Rx, Ry, Rz, Slider, Dial | int16, logical -32767..32767 |
| 16..20 | 36 buttons + 4 padding bits | 1 bit each |

Channel value 992 maps to exactly 0, 1811 to 32767, 172 to -32767. Every one of the 1640
distinct CRSF values maps to a distinct axis value.

## Default mapping (AETR)

| Axis | Channel | | Buttons | Channel | Positions |
|------|---------|-|---------|---------|-----------|
| X | 1 roll | | 0, 1, 2 | 5 | low, mid, high |
| Y | 2 pitch | | 3, 4, 5 | 6 | low, mid, high |
| Z | 3 throttle (failsafe low) | | ... | ... | ... |
| Rx | 4 yaw | | 33, 34, 35 | 16 | low, mid, high |
| Ry, Rz, Slider, Dial | 5, 6, 7, 8 | | | | |

Button thresholds in microseconds: low < 1300, mid 1300..1700, high > 1700.

Note: in Hybrid and Wide switch modes ELRS puts LQ and RSSI on channels 15 and 16. Either
run the radio in "Full Res 16ch" mode or set buttons 30 to 35 to channel 0 in the console.

## Failsafe

Engages when any of: RX unpowered, no RC frame for `failsafe_ms` (default 100), or the
receiver reports LQ 0 in a link-stats frame that is not stale. Link stats arrive every
100 ms; an LQ-0 report counts until an RC frame is more than 150 ms newer than it. This
covers the ELRS "Set Position" failsafe mode, where the receiver keeps emitting RC frames
after the link is gone. Sends all axes centred,
throttle axis at minimum, no buttons, and retries every 20 ms until TinyUSB accepts the
report. Re-arms on the next good RC frame.

## Receiver power and bind

- `PIN_RX_POWER` high turns the RX on (via 2N7000 -> AO3401A on the prototype, TPS22918 EN
  on the carrier board).
- When off, UART0 is deinitialised and GP0/GP1 are set to inputs with pulls disabled so the
  RX cannot be back-powered through its serial pins.
- Default at plug-in is on, configurable with `rxboot on|off`.
- `bind` sends the raw CRSF command `EC 04 32 'b' 'd' crc` on the UART, which ExpressLRS
  receivers handle in `RXEndpoint::handleRaw`. If the RX was off or just powered, the
  command is deferred until 1.5 s after power-up.
- `bindcycle` instead does three power cycles under two seconds each, which trips the
  receiver's power-on counter. Fallback for receivers that ignore the serial command.
- `rxbootloader` sends `'b' 'l'`, which reboots the RX into its ROM bootloader.
- `modelmatch <0-63|off>` sends `EC 05 32 'm' 'm' <id> crc` (`FF` for off), handled in the
  same `handleRaw`. The receiver stores the ID in its own config; the dongle keeps nothing.
  Reading it back needs the receiver settings below. The ID must equal the radio model's Receiver number when Model
  Match is on in the ExpressLRS Lua. The command is refused, not deferred, if the RX is off,
  within 1.5 s of power-up, or in passthrough. The receiver drops the link briefly while it
  saves.
- On a model mismatch the receiver sends neither RC nor link statistics frames, so the
  dongle sees silence: the ring shows "disconnected" and then, after 30 s, the WiFi pattern,
  even though the radio is on and the receiver's own LED is triple-blinking. See
  `LED-BEHAVIOUR.md` for the details and a possible fix.

## Receiver settings (CRSF parameter protocol)

`core/crsf_params.c` is a client for the CRSF device ping and parameter read/write frames,
the protocol the ExpressLRS Lua script uses. It is hardware independent like the rest of
`core/`: `crsf_params_on_frame()` takes complete frames from the CRSF parser,
`crsf_params_poll()` returns the next frame to send, one request at a time. It reassembles
chunked fields (a frame holds 56 bytes of field data), resends after 200 ms up to three
times, ignores duplicate chunks, and `crsf_params_parse_field()` decodes the result.

- Built in by default, about 4 KB of flash and 0.4 KB of RAM. `-DELRS_RX_PARAMS=OFF` at
  CMake time leaves it out, and `rxinfo` / `rxparam` become unknown commands.
- Requests go out with origin `C8` (flight controller) to `EC` (receiver). The receiver
  routes its replies back to the port the request came from.
- **Needs ExpressLRS 4.0 or newer on the receiver.** 3.x forwards these frames to the
  transmitter instead of answering them, so every request times out ("no reply from
  receiver"). `modelmatch`, `bind` and `rxbootloader` work on both.
- A write is followed by a read of the same field, 150 ms later, so the reply shows what
  the receiver now holds. Commands answer by themselves and are not re-read, since reading
  a command resets it.
- While a request is running the console reads no further input; its reply is the reply to
  `rxinfo` / `rxparam`. Worst case is 600 ms per chunk.
- Changing the receiver's `Protocol` away from CRSF makes it unreadable to the dongle. The
  only way back is the receiver's own WiFi page.

## Console

USB CDC. Line-oriented, commands are plain words, replies are one JSON object per line.
Any baud works. Opening at 1200 baud reboots the board into the UF2 bootloader.

```
help                          command list
version                       {"product":..,"version":..}
status                        link, failsafe, LED state, counters
channels                      {"ch":[16 raw values],"lq":..,"rssi":..,"snr":..,"rfmode":..,"link":..}
stream on|off [ms]            emit `channels` periodically (default 50 ms)
map                           full mapping as JSON
axis <0-7> <ch> [invert]      assign a channel to an axis (ch 0 = centre)
button <0-35> <ch> <low|mid|high|none>
throttle <axis|none>          axis forced low on failsafe
failsafe <20-250>             RC gap in ms
profile                       {"active":n,"profiles":[{"n":1,"used":true,"name":..},..]} for all 16 slots
profile use <1-16>            switch profile; unsaved changes are dropped; an empty slot starts from defaults
profile save <1-16> [name]    save the current mapping to a slot and switch to it
profile name <1-16> <name>    rename (letters, digits, space and - _ . + ( ) #, 15 characters)
profile delete <1-16>         empty a slot, not the active one
rxpower on|off|toggle         receiver power now
rxboot on|off                 receiver power at plug-in
bind | bindcycle | rxbootloader
modelmatch <0-63|off>         set the receiver's model-match ID
rxprobe                       wiring check, see below (full build only)
rxraw                         {"uart":..,"baud":..,"bytes":N,"last":"c8 0c 14 .."}: bytes from the receiver
pins                          level on each pin the board uses, read back from the pad
rxinfo                        {"name":..,"sw":"4.0.1","fields":N,..} from the receiver
rxparam <id>                  one receiver setting: type, name, value/options/text
rxparam <id> <value> [1|2|4]  write it (value width in bytes, default 1), reply as for a read
save | defaults | reboot
dfu [nodrive]                 reboot into the UF2 bootloader. `nodrive` starts it with only its PICOBOOT
                              interface, so no RPI-RP2 drive appears (for flashing over WebUSB)
```

Mapping changes take effect immediately and are lost on reboot until `save`, which writes
them to the active profile. `status` reports `config_dirty` so a UI can show an unsaved
indicator, and `profile`, the active slot number.

## Wiring diagnostics

For a board that powers up but never links. All three are safe to run at any time.

- `status` counts `uart_bytes`, every byte received from the receiver since boot, next to the
  frame counters. Zero bytes means nothing reaches GP1 at all. Bytes without `frames_ok` means
  something arrives that is not CRSF at 420000 baud. A receiver that has no bind phrase yet and
  is waiting to bind sends nothing, so zero is not on its own proof of a fault.
- `rxraw` shows the last 32 of those bytes in hex. CRSF frames from the receiver start `c8`.
- `pins` reads the level on GP0 to GP4 back from the pads. Both serial pins idle at 1 while the
  receiver is on.
- `rxprobe` answers "is the receiver's TX pad really on GP1" without depending on the receiver's
  firmware or bind state. It switches the receiver off, makes GP0 and GP1 inputs with pull-ups,
  switches it on again and counts edges on each pin for 1.2 s. An ESP receiver's boot ROM prints
  a banner on its TX pad at every power-up, so that pin shows hundreds of edges. The reply gives
  both counts, the levels with the receiver off and on, and a verdict:

  | Verdict | Meaning |
  |---|---|
  | `ok: receiver transmits on GP1` | Wired correctly |
  | `swapped: receiver transmits on GP0` | The receiver's TX and RX pads are the wrong way round |
  | `silent: ...` | The receiver did not start, or its TX pad reaches neither pin: check the joints from the pad to the module's GP1 pin, at both ends |
  | `both pins toggle: ...` | TX and RX are bridged |

  The link drops for the two seconds it takes. A multimeter cannot make this distinction: both
  lines idle at 3.3 V whether they are right, swapped or open (GP1 has a pull-up).

## Betaflight CLI emulation and passthrough

Sending a bare `#` switches the console into Betaflight mode: the banner
`Entering CLI Mode, type 'exit' to return, or 'help'` followed by a `# ` prompt, input echo,
and a prompt after each reply. In that mode the following are answered the way the
ExpressLRS flasher expects (see `src/python/BFinitPassthrough.py` in the ExpressLRS repo):

| Command | Reply |
|---------|-------|
| `get serialrx_provider` | `serialrx_provider = CRSF` |
| `get serialrx_inverted` | `serialrx_inverted = OFF` |
| `get serialrx_halfduplex` | `serialrx_halfduplex = OFF` |
| `get rx_spi_protocol` | `rx_spi_protocol = OFF` |
| `serial` | `serial 5 64 115200 57600 0 115200` (function 64 = serial RX) |
| `serialpassthrough 5 <baud>` | `Port 5 opened, baud = <baud>.` then bridge mode |
| `exit` | leaves Betaflight mode |

In bridge mode every byte from USB goes to the RX UART and back, HID sends the failsafe
report, the LED shows the serial-update pattern, and the UART baud follows the host's
line-coding changes so esptool can switch speeds. The flasher closes and reopens
the port between its steps, so a closed port does not end the bridge: it exits once the
port has stayed closed for 10 s, or the host has held it open but sent nothing for 60 s.

This is enough for the ExpressLRS Web Flasher's "Betaflight passthrough" method to flash
the receiver and set its bind phrase through the dongle.

## Flash layout

Settings live in the last 4 KB sector of the 2 MB flash as one `profile_store_t`
(`core/profiles.c`, about 2.1 KB): 16 slots, each a `config_t` plus a name, and the number of
the active slot. The store and each used slot carry a magic, version, size and CRC-32;
anything invalid falls back to a single default profile at boot. Saving skips the erase if the
stored block already matches.

The main loop works on a copy of the active slot (`app.config`). Receiver power at plug-in is
kept the same across profiles: switching carries the current value into the new slot.

A sector written by firmware from before profiles (a bare `config_t`) is read as profile 1.
Going back to that older firmware after saving loses the mapping, since it cannot read the
new layout and starts from defaults.

## USB identity

VID 0x1209 / PID 0x0001 is the pid.codes test pair, for development only. Manufacturer and
product strings come from `PRODUCT_NAME` (CMake cache variable, default "ELRS Sim RX").
Serial number is the RP2040 flash unique ID.
