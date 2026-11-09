// Channel-to-HID mapping and persistent configuration. Hardware independent.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "crsf.h"

#define NUM_AXES        8
#define NUM_BUTTONS     36      // 12 switch channels (5..16) x 3 positions
#define NUM_BUTTON_BYTES ((NUM_BUTTONS + 7) / 8)

#define AXIS_MIN        (-32767)
#define AXIS_MAX        ( 32767)

// Button thresholds, in microseconds (988..2012 scale).
#define BTN_LOW_MAX_US  1300
#define BTN_HIGH_MIN_US 1700

#define CONFIG_MAGIC    0x58525345u   // "ESRX"
#define CONFIG_VERSION  1

typedef enum {
    BTN_NONE = 0,
    BTN_LOW  = 1,   // pressed when channel < BTN_LOW_MAX_US
    BTN_MID  = 2,   // pressed when BTN_LOW_MAX_US <= channel <= BTN_HIGH_MIN_US
    BTN_HIGH = 3,   // pressed when channel > BTN_HIGH_MIN_US
} btn_level_t;

// Axis order in the HID report: X, Y, Z, Rx, Ry, Rz, Slider, Dial.
enum { AXIS_X, AXIS_Y, AXIS_Z, AXIS_RX, AXIS_RY, AXIS_RZ, AXIS_SLIDER, AXIS_DIAL };

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;

    uint8_t  axis_channel[NUM_AXES];       // 1..16, 0 = axis held at centre
    uint8_t  axis_invert;                  // bitmask, bit n inverts axis n
    uint8_t  button_channel[NUM_BUTTONS];  // 1..16, 0 = button unused
    uint8_t  button_level[NUM_BUTTONS];    // btn_level_t
    uint8_t  throttle_axis;                // axis index driven to minimum on failsafe, 0xFF = none
    uint8_t  rx_power_on_boot;             // 1 = power the receiver when USB is connected
    uint8_t  failsafe_ms;                  // RC frame gap that triggers failsafe
    uint8_t  reserved[13];

    uint32_t crc;                          // crc32 of everything above
} config_t;

// HID input report. Must match the report descriptor in usb_descriptors.c.
typedef struct __attribute__((packed)) {
    int16_t axes[NUM_AXES];
    uint8_t buttons[NUM_BUTTON_BYTES];
} hid_report_t;

void     config_defaults(config_t *cfg);
uint32_t config_compute_crc(const config_t *cfg);
void     config_finalize(config_t *cfg);           // sets magic/version/size/crc
bool     config_is_valid(const config_t *cfg);

int16_t  mapping_channel_to_axis(uint16_t ch, bool invert);
bool     mapping_button_pressed(uint16_t ch, btn_level_t level);
void     mapping_build_report(const config_t *cfg, const uint16_t ch[CRSF_NUM_CHANNELS], hid_report_t *out);
void     mapping_failsafe_report(const config_t *cfg, hid_report_t *out);

const char *mapping_axis_name(int axis);
const char *mapping_level_name(btn_level_t level);
