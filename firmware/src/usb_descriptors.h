#pragma once
#include <stdint.h>

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_CDC_COMM,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#define EPNUM_HID_IN        0x81
#define EPNUM_CDC_NOTIF     0x82
#define EPNUM_CDC_OUT       0x03
#define EPNUM_CDC_IN        0x83

// pid.codes test PID. Replace with an allocated PID before distributing.
#define USB_VID             0x1209
#define USB_PID             0x0001
#define USB_BCD_DEVICE      0x0100

