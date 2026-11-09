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
Firefox and Safari do not implement it. A Tauri or Electron wrapper would cover those if
it ever matters; the page is written so the `Device` class is the only thing that would
change.

## Protocol

Plain text commands, one JSON object per reply line. See `docs/FIRMWARE.md` for the
command list. The page keeps a FIFO of pending commands and resolves the oldest one with
the next non-stream JSON line. Lines containing a `ch` array are the `stream` output and
go straight to the channel display.

On connect the page does: `stream off`, `map`, `status`, `stream on 50`, then polls
`status` once a second.

## Mock mode

`web/index.html?mock` (or the footer link) connects to an in-page simulated device with
moving sticks and switches. Useful for layout work without hardware.

## ExpressLRS flasher handoff

Only one page can hold a serial port. To update the receiver or change the bind phrase,
disconnect here, open the ExpressLRS Web Flasher, pick the receiver target, choose the
"Betaflight passthrough" flashing method, and select the ELRS Sim RX serial port. The
firmware answers the flasher's Betaflight CLI queries and bridges the UART.

## Testing

`tools/test-web.sh` runs `tests/web/test_webui.mjs` in the official Playwright Docker
image: loads the page in mock mode, exercises connect, mapping edits, power toggle,
save, defaults, console and disconnect, checks for console errors and phone-width
overflow, and writes screenshots to `tests/web/shots/`. No browser is needed on the host.
