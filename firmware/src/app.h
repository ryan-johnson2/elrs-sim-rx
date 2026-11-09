// Shared application state between main loop and console.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "crsf.h"
#include "mapping.h"
#include "failsafe.h"
#include "led_seq.h"
#include "button.h"

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_CONNECTED,
    LED_STATE_DISCONNECTED,
    LED_STATE_BINDING,
    LED_STATE_WIFI,
    LED_STATE_SERIAL_UPDATE,
} led_state_t;

typedef struct {
    crsf_t      crsf;
    config_t    config;
    bool        config_dirty;
    failsafe_t  failsafe;
    led_seq_t   led;
    led_state_t led_state;
    button_t    button;

    hid_report_t last_report;
    uint32_t    reports_sent;
    uint32_t    reports_dropped;    // HID endpoint busy

    // bind handling
    bool        bind_pending;       // send "bd" once the RX has booted
    uint32_t    bind_requested_ms;  // 0 = none
    uint32_t    bind_pattern_until_ms;

    // console-driven passthrough (set by cli)
    bool        passthrough;

    uint32_t    boot_ms;
} app_t;

extern app_t app;

uint32_t app_now_ms(void);

// Actions callable from the console
void app_request_bind(uint32_t now_ms);          // "bd" command path
void app_request_bind_cycle(uint32_t now_ms);    // power-cycle path
void app_send_rx_command(char c0, char c1);
void app_save_config(void);
void app_reboot_to_bootsel(void);
