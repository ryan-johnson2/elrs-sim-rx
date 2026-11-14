# Windows notes

## Running the dongle: no drivers

The firmware is a standard composite USB device: a HID joystick plus a CDC serial port,
with the interface association descriptor Windows expects. Windows 10 and 11 bind their
built-in drivers (`hidusb`, `usbser`) automatically. The joystick appears in "Set up USB
game controllers" and the serial port appears as a COM port. Nothing to install.

Windows caches the device name per VID/PID. If the product string ever changes under the
same IDs, the old name sticks until the entry is removed in Device Manager.

## Flashing firmware from the web page: one driver, once

The RP2040 bootloader ("RP2 Boot", USB 2E8A:0003) exposes two interfaces:

| Interface | What | Windows driver |
|-----------|------|----------------|
| 0 | Mass storage, the `RPI-RP2` drive | built-in `USBSTOR`, leave it alone |
| 1 | PICOBOOT vendor interface, used by picotool and by the web page over WebUSB | none by default; needs **WinUSB** |

The RP2040 boot ROM does not carry the Microsoft OS descriptors that would make Windows
bind WinUSB on its own, so the browser cannot see interface 1 until a driver is bound.
This is a one-time step per PC. Same procedure picotool documents.

1. Download Zadig from https://zadig.akeo.ie/ (single exe, no install).
2. Put the board in bootloader mode: hold BOOT while plugging it in, or use "1. Enter
   update mode" on the web page.
3. Run Zadig. Menu **Options > List All Devices**.
4. In the dropdown pick **RP2 Boot (Interface 1)**. Not Interface 0.
5. Make sure the target driver (right-hand box) says **WinUSB**, then click
   **Install Driver** (or Replace Driver).
6. Unplug and replug. Chrome or Edge will now list "RP2 Boot" in the USB picker on
   step 2 of the Firmware panel.

If the picker shows nothing, Device Manager will show "RP2 Boot" with a warning icon
under "Other devices": the driver is not bound yet.

## The Explorer window that opens in update mode

The bootloader's `RPI-RP2` drive makes Windows AutoPlay open an Explorer window each time the
board enters update mode. Two ways to stop that:

- **Windows setting:** Settings > Bluetooth & devices > AutoPlay, set "Removable drive" to
  "Take no action". Works with every flashing method.
- **Firmware panel option:** "Do not show the RPI-RP2 drive while updating" starts the
  bootloader without its drive. Only the WebUSB route works in that mode. Untested so far:
  with the drive gone the bootloader is no longer a two-interface device, so Windows may not
  apply the WinUSB driver that Zadig bound to "RP2 Boot (Interface 1)", and Zadig may have to
  be run once more for it. If the page cannot find the board, replug it; it starts normally.

## Flashing without any driver

Download the UF2 from the Firmware panel, hold BOOT while plugging in, and copy the file
onto the `RPI-RP2` drive that appears. Explorer handles it, the board reboots. This is
what the "Manual method" section on the page describes.

## Web Serial and WebUSB in general

Both need Chrome, Edge or Opera, and either HTTPS or `localhost`. The hosted page is
HTTPS. The local dev server (`tools/serve.sh`) offers HTTPS with a self-signed certificate;
accept the browser warning once.

## Sources

- picotool README, "Zadig" section: https://github.com/raspberrypi/picotool/blob/master/README.md
- Raspberry Pi forum thread on RP2 Boot and WinUSB: https://forums.raspberrypi.com/viewtopic.php?t=321116
- Microsoft Q&A on Pico COM ports: https://learn.microsoft.com/en-us/answers/questions/2156610/how-to-resolve-com-port-issues-for-raspberry-pi-pi
