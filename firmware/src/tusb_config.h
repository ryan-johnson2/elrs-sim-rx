#pragma once

#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_OS             OPT_OS_PICO

#define CFG_TUD_ENDPOINT0_SIZE  64

// Composite device: HID joystick + CDC serial console.
#define CFG_TUD_HID             1
#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

#define CFG_TUD_HID_EP_BUFSIZE  32

#define CFG_TUD_CDC_RX_BUFSIZE  512
#define CFG_TUD_CDC_TX_BUFSIZE  2048
#define CFG_TUD_CDC_EP_BUFSIZE  64
