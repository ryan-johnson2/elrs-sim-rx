# Web UI

`web/index.html` is a single-file static page. No build step, no backend, no dependencies.

## Hosting

The page is hosted externally, not on the device. The RP2040 has no network interface,
so "built into the firmware" would mean either a USB mass-storage drive carrying the HTML
(Windows pops a drive window on every plug-in, and the page would still need Web Serial)
or a WebUSB landing-page URL (Chrome on Mac/Linux shows a "go to this URL" toast on
plug-in, Windows does not). Neither beats a plain hosted page.

Options, all of which work with the same file:

| Where | How | Notes |
|-------|-----|-------|
| GitHub Pages | serve `web/` from the repo | Zero cost, HTTPS, which Web Serial requires. Intended default once a remote exists. |
| Local file | open `web/index.html` in Chrome | `file://` counts as a secure context in Chromium, so Web Serial works offline. |
| `python3 -m http.server -d web` | `http://localhost:8000` | localhost is a secure context too. |

The firmware's USB product string tells users what they plugged in; the README and the
device's `help` output point at the hosted URL.

## Browser support

Web Serial exists in Chrome, Edge and Opera on desktop (Windows, macOS, Linux, ChromeOS).
Firefox and Safari do not implement it, and no phone browser does, so the page is
desktop-only by nature and is laid out for a desktop window, including a half-width one
next to a simulator. A Tauri or Electron wrapper would cover those if
it ever matters; the page is written so the `Device` class is the only thing that would
change.

## Protocol

Plain text commands, one JSON object per reply line. See `docs/FIRMWARE.md` for the
command list. The page keeps a FIFO of pending commands and resolves the oldest one with
the next non-stream JSON line. Lines containing a `ch` array are the `stream` output and
go straight to the channel display.

On connect the page does: `stream off`, `map`, `status`, `stream on 50`, then polls
`status` once a second.

## Developer mode

The header's **Dev** toggle (also `?dev` in the URL, remembered in localStorage) reveals
the panels a normal user does not need: the raw console, frame and HID counters, the
channel-stream toggle and the flash log. Everything else is the same page, so there is one
codebase and one deployment. The dev server (`tools/serve.sh`) is what makes the local
build available to the Firmware panel; the hosted copy on GitHub Pages gets its firmware
list from a manifest published alongside the page (to be wired up with releases).

## Firmware panel

Flashing runs in the browser over WebUSB using the RP2040 bootloader's PICOBOOT
interface (`web/picoboot.js`, protocol from pico-sdk `boot/picoboot.h` and picotool).

1. Pick a firmware: the dropdown lists `firmware/manifest.json` entries served next to
   the page (written by `tools/build.sh`), or Browse for a local `.uf2`. Hosted files are
   SHA-256 checked against the manifest; every file is parsed as a UF2 before flashing.
2. **Enter update mode** sends `dfu` over the open serial connection and disconnects.
   The board re-enumerates as "RP2 Boot". With no serial connection, hold BOOT while
   plugging in instead.
3. **Select device and flash** opens the browser's USB picker, then: exclusive access,
   exit XIP, one erase per contiguous sector run, 4 KB writes, reboot. Progress and a log
   are shown; the log is in the dev panel.

**Download UF2** gives the file for the manual route: copy it onto the `RPI-RP2` drive.
Windows needs WinUSB bound to "RP2 Boot (Interface 1)" once for the WebUSB route; see
`docs/WINDOWS.md`. The manual route needs no driver.

`tests/web/test_picoboot.mjs` flashes the real build UF2 into a simulated BOOTSEL device
and checks the resulting image byte for byte, plus the command order and error paths.

## Mock mode

`web/index.html?mock` (or the footer link) connects to an in-page simulated device with
moving sticks and switches. Useful for layout work without hardware.

## ExpressLRS flasher handoff

Only one page can hold a serial port. To update the receiver or change the bind phrase,
disconnect here, open the ExpressLRS Web Flasher, pick the receiver target, choose the
"Betaflight passthrough" flashing method, and select the ELRS Sim RX serial port. The
firmware answers the flasher's Betaflight CLI queries and bridges the UART.

## Testing

`tools/test-web.sh` runs both web tests in the official Playwright Docker image, so no
browser or Node is needed on the host:

- `tests/web/test_picoboot.mjs` (Node): UF2 parsing, flash planning and the PICOBOOT
  command sequence against a simulated bootloader, using the real build output.
- `tests/web/test_webui.mjs` (Chromium): loads the page in mock mode and exercises
  connect, mapping edits, power toggle, save, defaults, the dev toggle, the console, the
  full firmware update flow against the in-page mock bootloader, and disconnect. Checks
  for browser console errors and layout overflow, and writes screenshots to
  `tests/web/shots/`.
