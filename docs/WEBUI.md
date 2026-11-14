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
| GitHub Pages | published by CI, see below | Zero cost, HTTPS, which Web Serial requires. Intended default once a remote exists. |
| Local file | open `web/index.html` in Chrome | `file://` counts as a secure context in Chromium, so Web Serial works offline. |
| `python3 -m http.server -d web` | `http://localhost:8000` | localhost is a secure context too. |

### What gets published

Two channels, set up in `.github/workflows/ci.yml` and not yet run against a real repository:

| Channel | Comes from | Published as |
|---------|------------|--------------|
| Release | a `v*` tag on `main` | A GitHub release with both UF2 files, their manifest and the page. The site root serves this page |
| Development | every push to `devel` | A rolling prerelease named `devel-build`, replaced each time. Served at `/devel/` |

After either one, the `pages` job downloads the latest release and the devel build and runs
`tools/assemble_site.py`, which lays out the site and merges the two manifests. Both pages
list all four firmware files; the root page defaults to the release full build, the `/devel/`
page to the development full build. Pushes to `main` without a tag are built and tested but
not published. Until the first release exists, the site root serves the devel page.

One-time repository setup: Pages source set to "GitHub Actions", and the `github-pages`
environment allowed to deploy from the `devel` branch and from `v*` tags.

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
`status` once a second (quietly, see Pages below).

## Pages

One file, four tabs, chosen by the URL hash so each can be linked to:

| Tab | Hash | What is on it |
|-----|------|---------------|
| Controller | `#main` (default) | Profile and saving first, then channels, axes, buttons, status, receiver power / bind / model match and receiver settings |
| Receiver firmware | `#receiver-update` | Step-by-step instructions for updating the receiver's ExpressLRS firmware through the dongle's passthrough, with a fault table |
| Dongle firmware | `#firmware` | The firmware panel below, with its flash log folded away, plus Reboot dongle and a plain Enter update mode button |
| Console | `#console` | The raw console, the frame and HID counters, and the channel-stream toggle |

There is no developer mode any more; the Console tab replaces it.

The once-a-second `status` poll is sent as a quiet command: neither it nor its reply is
written to the console unless "show status polling" is ticked. The poll itself stays,
because it is what keeps the status panel, the header pills, the unsaved-changes marker and
the counters current.

The dev server (`tools/serve.sh`) makes the local builds available to the firmware panel. The
hosted copy on GitHub Pages gets its firmware list from a manifest published alongside the
page by the `pages` job in `.github/workflows/ci.yml`.

## Firmware panel

Flashing runs in the browser over WebUSB using the RP2040 bootloader's PICOBOOT
interface (`web/picoboot.js`, protocol from pico-sdk `boot/picoboot.h` and picotool).

1. Pick a firmware: the dropdown lists `firmware/manifest.json` entries served next to
   the page (written by `tools/build.sh`), or Browse for a local `.uf2`. Hosted files are
   SHA-256 checked against the manifest; every file is parsed as a UF2 before flashing.
2. **Update firmware** does the rest in one click: it sends `dfu` over the open serial
   connection, waits up to 10 s for the bootloader to appear among the USB devices this page
   already has access to (`navigator.usb.getDevices()`), then flashes: exclusive access, exit
   XIP, one erase per contiguous sector run, 4 KB writes, reboot.
3. The first time, the browser has not been given access to "RP2 Boot" yet, and a page may
   only ask for that from a click. The status line then says to use **2. Select device and
   flash**, which opens the USB picker. The permission is remembered, so later updates need
   only the one click.

The step-by-step buttons stay for that first time and for boards with no firmware (hold BOOT
while plugging in, then step 2). Progress and a log are shown; the log is under "Flash log".

**Do not show the RPI-RP2 drive while updating** sends `dfu nodrive`, which starts the
bootloader with only its PICOBOOT interface, so Windows opens no Explorer window. Not yet
tried on hardware. The manual drag-and-drop route cannot be used in that mode, and the WinUSB
driver binding on Windows may differ from the normal bootloader's; see `docs/WINDOWS.md`.

**Download UF2** gives the file for the manual route: copy it onto the `RPI-RP2` drive.
Windows needs WinUSB bound to "RP2 Boot (Interface 1)" once for the WebUSB route; see
`docs/WINDOWS.md`. The manual route needs no driver.

`tests/web/test_picoboot.mjs` flashes the real build UF2 into a simulated BOOTSEL device
and checks the resulting image byte for byte, plus the command order and error paths.

## Mock mode

`web/index.html?mock` (or the footer link) connects to an in-page simulated device with
moving sticks and switches. Useful for layout work without hardware.

## Profiles

The "Profile and saving" section on the Controller tab lists the dongle's used profiles for
switching, and all 16 slots for **Save current mapping here**, **Rename** and **Delete**.
Switching with unsaved changes asks first; so does overwriting another profile. The controls
stay hidden when the firmware answers `profile` with an error (builds from before profiles).

## Model match

The Receiver section on the Controller tab has a model match selector (Off, 0 to 63) and **Set on receiver**,
which sends `modelmatch <id>`. Pick the radio model's Receiver number if Model Match is on
in the ExpressLRS Lua, Off otherwise. The receiver stores the value. Until the
receiver settings are loaded the selector only remembers the last value sent from this
browser; once they are, it shows what the receiver reports and re-reads it after each set.

## Receiver settings

**Load from receiver** sends `rxinfo`, then `rxparam <id>` for each setting, and renders
them by type: selections as dropdowns, numbers as inputs, info lines as text, folders as
headings with their children indented, commands as a Run button that follows the
click / confirm / poll steps. Hidden settings are skipped. A change is written at once and
everything is read again, because one setting can show or hide others. Changing `Protocol`
to anything but CRSF asks first, as it cuts the dongle off from the receiver. Receivers on
ExpressLRS 3.x do not answer and the section says so.

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
  connect, mapping edits, power toggle, save, defaults, the tabs, the console, the
  full firmware update flow against the in-page mock bootloader, and disconnect. Checks
  for browser console errors and layout overflow, and writes screenshots to
  `tests/web/shots/`.
