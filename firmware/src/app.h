// Shared application state between main loop and console.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "crsf.h"
#include "mapping.h"
#include "profiles.h"
#include "failsafe.h"
#include "led_seq.h"
#include "button.h"
#ifndef ELRS_RX_PARAMS
#define ELRS_RX_PARAMS 0
#endif
#if ELRS_RX_PARAMS
#include "crsf_params.h"
#endif

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
    config_t    config;             // working copy of the active profile; edits land here
    bool        config_dirty;       // working copy differs from what is stored
    profile_store_t profiles;       // what is in flash: all slots and which one is active
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

#if ELRS_RX_PARAMS
    crsf_params_t rx_params;        // receiver settings client, driven by the console
#endif

    uint32_t    boot_ms;
} app_t;

extern app_t app;

uint32_t app_now_ms(void);

// Actions callable from the console
void app_set_rx_power(bool on, uint32_t now_ms); // also cancels any pending bind when turning off
void app_request_bind(uint32_t now_ms);          // "bd" command path
void app_request_bind_cycle(uint32_t now_ms);    // power-cycle path
void app_send_rx_command(char c0, char c1);
// Set the receiver's model-match ID (0-63, or CRSF_MODEL_ID_OFF). Returns false
// with a reason if the receiver cannot take it right now.
// True if the receiver is powered, booted and not in passthrough, i.e. it can
// take a command now. Otherwise *err says why not.
bool app_rx_ready(uint32_t now_ms, const char **err);
bool app_set_rx_model_id(uint8_t id, uint32_t now_ms, const char **err);
void app_save_config(void);                       // working copy -> active profile -> flash
// Profiles, n = 0..PROFILE_COUNT-1. Each returns false if nothing was changed or stored.
bool app_profile_use(int n);                      // load slot n (creating it from defaults if empty); drops unsaved edits
bool app_profile_save_as(int n, const char *name); // working copy -> slot n, which becomes active; name may be NULL
bool app_profile_rename(int n, const char *name);
bool app_profile_delete(int n);                   // not the active one
// no_drive: start the bootloader with only its PICOBOOT interface, so no RPI-RP2 drive appears.
void app_reboot_to_bootsel(bool no_drive);
