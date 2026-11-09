#include "tusb.h"
#include "usb_descriptors.h"
#include "mapping.h"
#include "pico/unique_id.h"
#include <string.h>

#ifndef PRODUCT_NAME
#define PRODUCT_NAME "ELRS Sim RX"
#endif

// ---------------------------------------------------------------------------
// Device descriptor
// ---------------------------------------------------------------------------
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    // Composite device: class defined per interface, with IAD for CDC.
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = USB_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

// ---------------------------------------------------------------------------
// HID report descriptor: joystick with 8 x 16-bit axes and NUM_BUTTONS buttons.
// Layout must match hid_report_t in mapping.h.
// ---------------------------------------------------------------------------
static const uint8_t desc_hid_report[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_JOYSTICK),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_COLLECTION(HID_COLLECTION_PHYSICAL),
            // 8 axes, int16
            HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
            HID_USAGE(HID_USAGE_DESKTOP_X),
            HID_USAGE(HID_USAGE_DESKTOP_Y),
            HID_USAGE(HID_USAGE_DESKTOP_Z),
            HID_USAGE(HID_USAGE_DESKTOP_RX),
            HID_USAGE(HID_USAGE_DESKTOP_RY),
            HID_USAGE(HID_USAGE_DESKTOP_RZ),
            HID_USAGE(HID_USAGE_DESKTOP_SLIDER),
            HID_USAGE(HID_USAGE_DESKTOP_DIAL),
            HID_LOGICAL_MIN_N(AXIS_MIN, 2),
            HID_LOGICAL_MAX_N(AXIS_MAX, 2),
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(NUM_AXES),
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
            // buttons
            HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
            HID_USAGE_MIN(1),
            HID_USAGE_MAX(NUM_BUTTONS),
            HID_LOGICAL_MIN(0),
            HID_LOGICAL_MAX(1),
            HID_REPORT_SIZE(1),
            HID_REPORT_COUNT(NUM_BUTTONS),
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
            // pad to a byte boundary
            HID_REPORT_SIZE(NUM_BUTTON_BYTES * 8 - NUM_BUTTONS),
            HID_REPORT_COUNT(1),
            HID_INPUT(HID_CONSTANT),
        HID_COLLECTION_END,
    HID_COLLECTION_END
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

// ---------------------------------------------------------------------------
// Configuration descriptor
// ---------------------------------------------------------------------------
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    // Config: 1 configuration, ITF_NUM_TOTAL interfaces, string 0, total len, attributes, 100 mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // HID: interface, string idx, protocol none, report desc len, EP IN, EP size, polling interval 1 ms
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 4, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                       EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),

    // CDC: interface pair, string idx, notif EP, notif size, data OUT, data IN, data EP size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_COMM, 5, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

// ---------------------------------------------------------------------------
// String descriptors
// ---------------------------------------------------------------------------
static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

static const char *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },   // 0: English (US)
    PRODUCT_NAME,                   // 1: manufacturer
    PRODUCT_NAME,                   // 2: product
    serial_str,                     // 3: serial (board unique id)
    PRODUCT_NAME " Joystick",       // 4: HID interface
    PRODUCT_NAME " Console",        // 5: CDC interface
};

static uint16_t desc_str[48];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    if (index == 3 && serial_str[0] == 0) {
        pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
    }

    uint8_t chr_count;
    if (index == 0) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 46) chr_count = 46;
        for (uint8_t i = 0; i < chr_count; i++) {
            desc_str[1 + i] = (uint16_t)str[i];
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
