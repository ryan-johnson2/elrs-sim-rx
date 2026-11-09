# Bring-up checklist

For the first day with hardware. Work top to bottom; each step depends on the one before.
Record results inline (date, board number, pass/fail, notes) and commit the file.

## 0. Before touching the receiver

- [ ] Run `picotool info -a` on each RP2040-Zero (via `tools/docker-run.sh picotool info -a`
      with the board in BOOTSEL, or `tools/flash.sh` will do the detection). Log flash chip
      and size per board in `docs/HBOM.md`. Clones vary.
- [ ] `tools/build.sh` on a clean checkout. Note the manifest commit.
- [ ] Update the EP2 over its own WiFi hotspot to current ExpressLRS 3.x matching your TX
      module. Bind it traditionally to the radio while it is still on the bench with a
      USB-to-5V supply, so "RX works at all" is known before the dongle is involved.

## 1. RP2040 alone, no receiver, no button, no LED

Flash `elrs_sim_rx.uf2` (hold BOOT, plug in, copy, or `tools/flash.sh`).

- [ ] Enumerates as one device with a HID joystick and a serial port.
      Linux: `lsusb -d 1209:0001 -v | grep -E "bInterfaceClass|iProduct"`; `ls /dev/serial/by-id/`.
      Windows: Device Manager shows a game controller and a COM port, no warnings.
- [ ] Joystick visible in the OS: Linux `jstest-gtk` or `evtest`; Windows "Set up USB game
      controllers". 8 axes, 36 buttons. Axes sit centred, Z (throttle) at minimum: that is
      the failsafe report, correct with no receiver.
- [ ] Serial console answers: `screen /dev/ttyACM0` or any terminal, type `help`, `status`.
      `status` should show `rx_power: true`, `link: false`, `failsafe: true`, `led: "wifi"`
      after a second or so (no frames at all = assumed WiFi/unresponsive).
- [ ] `rxpower off` then `status`: `rx_power: false`, `led: "off"`. `rxpower on` restores.
- [ ] Web page: connect, status populates, channels all 992 (centre), mapping table renders.
- [ ] `dfu` (or the page's "Firmware update mode") drops the board into BOOTSEL and
      `RPI-RP2` appears. Flash again from the page via WebUSB (Linux: udev rule installed;
      Windows: Zadig/WinUSB done). This proves the whole update loop before anything else
      is soldered.

## 2. Add the LED ring and button (breadboard)

Wire per `docs/HBOM.md` schematic: 2N7000 on GP4 for the LED, button GP3 to GND.

- [ ] LED follows `status` → `led`: with RX power on and no receiver it should be the fast
      WiFi flicker; `rxpower off` makes it dark.
- [ ] Short press toggles RX power (watch `status` and the LED).
- [ ] Long press (1.5 s): `status` shows `led: "binding"` (double blink) for up to 60 s.
      Nothing else can happen yet without a receiver.

## 3. Add the receiver, powered directly from 5 V first

Bypass the P-FET for now: EP2 5V straight to the board's 5V pin. TX→GP1, RX→GP0, GND.

- [ ] With the radio off: `status` shows `link: false`, `led: "disconnected"` (slow blink),
      `frames_ok` climbing slowly (link-stats frames arrive even without a TX). If
      `frames_ok` stays 0 and `frames_crc_err` or `uart_overruns` climb, check baud and
      swap TX/RX.
- [ ] Radio on: `link: true`, `failsafe: false`, `led: "connected"` (solid), `frames_ok`
      climbing at the packet rate, `reports_sent` climbing at the same rate,
      `reports_dropped` near zero. `last_rc_age_ms` small.
- [ ] Web page channel bars follow the sticks. Axis values follow with full travel
      (−32767..32767 at the endpoints after radio calibration). Switch channels light the
      expected buttons.
- [ ] OS joystick tester shows the same. Then a sim: Velocidrone controller setup sees the
      device, axes map, full range, no jitter at centre.
- [ ] Radio off mid-flight in the tester: within ~100 ms sticks centre and throttle drops,
      `failsafe: true`. Radio back on: control returns, `failsafe: false`.
- [ ] Packet rate change on the radio (e.g. 250 Hz → 500 Hz): `rfmode` in `status` changes,
      report rate follows.
- [ ] Switch mode check: if the radio is in Hybrid or Wide, channels 15 and 16 will show
      LQ/RSSI movement in the channel bars. Expected; either set Full Res 16ch or set
      buttons 30–35 to none.

## 4. Receiver through the P-FET switch

Move the EP2 5V to the AO3401A drain; GP2 → 2N7000 → gate, 10 k pull-up.

- [ ] `rxpower off`: receiver LED goes dark, `frames_ok` stops. Measure RX 5V pad: ~0 V.
      GP0 and GP1 measured against GND: ~0 V (no back-powering).
- [ ] `rxpower on`: receiver boots, link returns within ~2 s.
- [ ] Short press on the button does the same.
- [ ] Measure RX 5V with the FET on: drop under 100 mV at the receiver's draw.

## 5. Bind and passthrough

- [ ] Unbind the receiver (flash with no bind phrase, or `bind` from a different radio
      profile). `bind` on the console or long press: receiver LED double-blinks, radio's
      ELRS Lua "Bind" completes it, link comes up. Confirms the `bd` serial command works.
- [ ] `bindcycle`: watch the receiver LED go through three boots and end in bind mode.
- [ ] ExpressLRS Web Flasher, receiver target `HappyModel EP 2.4GHz RX`, method
      "Betaflight passthrough", choose the dongle's serial port, set a bind phrase, Flash.
      Expect the flasher's CLI handshake to pass, the RX to reboot into bootloader, and the
      flash to complete. Dongle LED shows the serial-update pattern during.
      Afterwards the dongle drops out of passthrough on its own (host closes port) and
      the link comes back.
- [ ] `rxbootloader` on the console: receiver LED pattern changes (ROM bootloader), and
      `rxpower off` / `on` recovers it.

## 6. Soak

- [ ] Leave connected with the radio on for 30+ minutes: `frames_crc_err`,
      `frames_timeout`, `uart_overruns`, `reports_dropped` stay low and flat. No USB
      disconnects in `dmesg` (Linux) or Device Manager.
- [ ] Unplug/replug five times: always enumerates, `rxboot` setting respected.
- [ ] `save` a changed mapping, replug, `map` shows it persisted.

## Known unknowns to confirm

- RP2040-Zero clone flash chip compatibility with the default boot stage.
- Whether the EP2 pad order on the batch received matches the HBOM assumption.
- WebUSB picker behaviour on the desktop OS used (Linux udev / Windows WinUSB).
- Whether `PASSTHROUGH_IDLE_MS` (10 s) is long enough for the flasher's pauses.
- Real current draw of the EP2 through the AO3401A, and whether the 10 µF + 100 nF is
  enough to avoid brownout on TX bursts.
