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

HID reports go out on every RC frame (250 to 500 Hz depending on the ELRS packet rate),
never on a timer. The HID endpoint polls at 1 ms. If the endpoint is busy the frame is
dropped and counted in `reports_dropped`; the next frame carries the fresh state anyway.

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

Engages when any of: RX unpowered, no RC frame for `failsafe_ms` (default 100), or a
link-stats frame newer than the last RC frame reports LQ 0. Sends all axes centred,
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
rxpower on|off|toggle         receiver power now
rxboot on|off                 receiver power at plug-in
bind | bindcycle | rxbootloader
save | defaults | reboot | dfu
```

Mapping changes take effect immediately and are lost on reboot until `save`.
`status` reports `config_dirty` so a UI can show an unsaved indicator.

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
line-coding changes so esptool can switch speeds. The bridge exits when the host closes
the port or has been silent for 10 s.

This is enough for the ExpressLRS Web Flasher's "Betaflight passthrough" method to flash
the receiver and set its bind phrase through the dongle.

## Flash layout

Config lives in the last 4 KB sector of the 2 MB flash. `config_t` carries a magic,
version, size and CRC-32; anything invalid falls back to defaults at boot. Saving skips the
erase if the stored block already matches.

## USB identity

VID 0x1209 / PID 0x0001 is the pid.codes test pair, for development only. Manufacturer and
product strings come from `PRODUCT_NAME` (CMake cache variable, default "ELRS Sim RX").
Serial number is the RP2040 flash unique ID.
