// ELRS Sim RX: ExpressLRS receiver -> USB HID joystick for FPV simulators.
//
// Main loop responsibilities:
//   - pump TinyUSB
//   - drain the CRSF UART ring buffer into the parser
//   - on each RC frame, build and send a HID report
//   - run failsafe, LED, button, RX power and console tasks

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#include "app.h"
#include "status_led.h"
#include "board.h"
#include "cli.h"
#include "rx_uart.h"
#include "rx_power.h"
#include "config_store.h"

app_t app;

uint32_t app_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void app_send_rx_command(char c0, char c1)
{
    uint8_t frame[8];
    size_t n = crsf_build_rx_command(frame, c0, c1);
    rx_uart_write(frame, n);
}

bool app_rx_ready(uint32_t now_ms, const char **err)
{
    if (app.passthrough) { *err = "busy: passthrough active"; return false; }
    if (!rx_power_is_on() || rx_power_bind_cycle_active()) { *err = "receiver is off"; return false; }
    if ((uint32_t)(now_ms - rx_power_on_since_ms()) < BIND_AFTER_POWER_MS) {
        *err = "receiver still booting, retry in a moment";
        return false;
    }
    return true;
}

bool app_set_rx_model_id(uint8_t id, uint32_t now_ms, const char **err)
{
    // Not deferred like bind: the caller gets a definite answer instead.
    if (!app_rx_ready(now_ms, err)) return false;
    uint8_t frame[8];
    size_t n = crsf_build_rx_model_id(frame, id);
    rx_uart_write(frame, n);
    return true;
}

void app_set_rx_power(bool on, uint32_t now_ms)
{
    if (!on) {
        app.bind_pending = false;        // a stale bind must not fire on the next power-on
        app.bind_requested_ms = 0;
    }
    rx_power_set(on, now_ms);
}

void app_request_bind(uint32_t now_ms)
{
    app.bind_requested_ms = now_ms;
    app.bind_pattern_until_ms = now_ms + BIND_PATTERN_TIMEOUT_MS;
    if (!rx_power_is_on()) {
        rx_power_set(true, now_ms);
        app.bind_pending = true;          // send once the RX has booted
    } else if ((uint32_t)(now_ms - rx_power_on_since_ms()) < BIND_AFTER_POWER_MS) {
        app.bind_pending = true;
    } else {
        app_send_rx_command('b', 'd');
        app.bind_pending = false;
    }
}

void app_request_bind_cycle(uint32_t now_ms)
{
    app.bind_pending = false;
    app.bind_requested_ms = now_ms;
    app.bind_pattern_until_ms = now_ms + BIND_PATTERN_TIMEOUT_MS;
    rx_power_start_bind_cycle(now_ms);
}

// Write the profile store to flash.
static bool persist_profiles(void)
{
    profiles_finalize(&app.profiles);
    uint32_t t0 = app_now_ms();
    bool ok = config_store_save(&app.profiles);
    // The erase/program cycle runs with interrupts off for tens of ms, so RC
    // bytes were lost and the parser may hold a partial frame. Drop the
    // leftovers and credit the stall to the link timestamps so the failsafe
    // does not fire because of our own flash write.
    uint32_t stall = app_now_ms() - t0;
    rx_uart_discard();
    app.crsf.pos = 0;
    if (app.crsf.have_rc)   app.crsf.last_rc_ms    += stall;
    if (app.crsf.have_link) app.crsf.last_link_ms  += stall;
    app.crsf.last_frame_ms += stall;
    return ok;
}

void app_save_config(void)
{
    profiles_put(&app.profiles, app.profiles.active, &app.config);
    if (persist_profiles()) {
        app.config_dirty = false;
    }
}

bool app_profile_use(int n)
{
    if (n < 0 || n >= PROFILE_COUNT) return false;
    // Receiver power at plug-in is one setting for the dongle, not one per profile: carry it over.
    uint8_t rx_boot = app.config.rx_power_on_boot;
    profiles_get(&app.profiles, n, &app.config);
    app.config.rx_power_on_boot = rx_boot;
    config_finalize(&app.config);
    app.failsafe.timeout_ms = app.config.failsafe_ms;
    profiles_put(&app.profiles, n, &app.config);     // also brings an empty slot into use
    app.profiles.active = (uint8_t)n;
    bool ok = persist_profiles();
    app.config_dirty = !ok;
    return ok;
}

bool app_profile_save_as(int n, const char *name)
{
    if (n < 0 || n >= PROFILE_COUNT) return false;
    profiles_put(&app.profiles, n, &app.config);
    if (name && *name) profiles_rename(&app.profiles, n, name);
    app.profiles.active = (uint8_t)n;
    bool ok = persist_profiles();
    if (ok) app.config_dirty = false;
    return ok;
}

bool app_profile_rename(int n, const char *name)
{
    return profiles_rename(&app.profiles, n, name) && persist_profiles();
}

bool app_profile_delete(int n)
{
    return profiles_clear(&app.profiles, n) && persist_profiles();
}

void app_reboot_to_bootsel(bool no_drive)
{
    rx_power_set(false, app_now_ms());
    // Second argument is the boot ROM's disable-interface mask: 1 = no mass storage drive.
    reset_usb_boot(0, no_drive ? 1 : 0);
}

// ---------------------------------------------------------------------------
// HID
// ---------------------------------------------------------------------------
static bool send_report(const hid_report_t *r)
{
    if (!tud_hid_ready()) {
        app.reports_dropped++;
        return false;
    }
    bool ok = tud_hid_report(0, r, sizeof(*r));
    if (ok) {
        app.last_report = *r;
        app.reports_sent++;
    } else {
        app.reports_dropped++;
    }
    return ok;
}

// TinyUSB HID callbacks ------------------------------------------------------
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id;
    if (report_type != HID_REPORT_TYPE_INPUT) return 0;
    uint16_t n = sizeof(app.last_report);
    if (n > reqlen) n = reqlen;
    memcpy(buffer, &app.last_report, n);
    return n;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

// TinyUSB CDC callbacks ------------------------------------------------------
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding)
{
    (void)itf;
    cli_on_line_coding(coding->bit_rate);
}

// ---------------------------------------------------------------------------
// LED state selection (see docs/LED-BEHAVIOUR.md)
// ---------------------------------------------------------------------------
static void update_led(uint32_t now_ms)
{
    led_state_t s;
    const crsf_t *c = &app.crsf;
    bool rc_fresh   = c->have_rc   && (uint32_t)(now_ms - c->last_rc_ms)    < app.failsafe.timeout_ms;
    bool any_fresh  = (c->have_rc || c->have_link) && (uint32_t)(now_ms - c->last_frame_ms) < RX_SILENT_MS;
    bool booting    = (uint32_t)(now_ms - rx_power_on_since_ms()) < RX_BOOT_GRACE_MS;
    bool bind_shown = app.bind_requested_ms != 0 && (int32_t)(app.bind_pattern_until_ms - now_ms) > 0;

    if (!rx_power_is_on() && !rx_power_bind_cycle_active()) {
        s = LED_STATE_OFF;
    } else if (app.passthrough) {
        s = LED_STATE_SERIAL_UPDATE;
    } else if (rc_fresh) {
        s = LED_STATE_CONNECTED;
        app.bind_requested_ms = 0;        // bound and connected, pattern done
    } else if (bind_shown) {
        s = LED_STATE_BINDING;
    } else if (any_fresh || booting || rx_power_bind_cycle_active()) {
        s = LED_STATE_DISCONNECTED;
    } else {
        s = LED_STATE_WIFI;
    }

    if (s != app.led_state) {
        app.led_state = s;
        switch (s) {
        case LED_STATE_OFF:           led_seq_set_solid(&app.led, false); break;
        case LED_STATE_CONNECTED:     led_seq_set_solid(&app.led, true);  break;
        case LED_STATE_DISCONNECTED:  led_seq_set_pattern(&app.led, LEDSEQ_DISCONNECTED,  sizeof(LEDSEQ_DISCONNECTED),  now_ms); break;
        case LED_STATE_BINDING:       led_seq_set_pattern(&app.led, LEDSEQ_BINDING,       sizeof(LEDSEQ_BINDING),       now_ms); break;
        case LED_STATE_WIFI:          led_seq_set_pattern(&app.led, LEDSEQ_WIFI_UPDATE,   sizeof(LEDSEQ_WIFI_UPDATE),   now_ms); break;
        case LED_STATE_SERIAL_UPDATE: led_seq_set_pattern(&app.led, LEDSEQ_SERIAL_UPDATE, sizeof(LEDSEQ_SERIAL_UPDATE), now_ms); break;
        }
    }

    bool lit = led_seq_update(&app.led, now_ms);
    gpio_put(PIN_LED, lit);
    status_led_set(lit);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------
static void update_button(uint32_t now_ms)
{
#if ELRS_LITE
    (void)now_ms;                    // no button fitted; GP3 may be floating or reused
    return;
#endif
    bool pressed = !gpio_get(PIN_BUTTON);   // active low
    switch (button_update(&app.button, pressed, now_ms)) {
    case BUTTON_EVENT_SHORT:
        app_set_rx_power(!rx_power_is_on(), now_ms);
        break;
    case BUTTON_EVENT_LONG:
        app_request_bind(now_ms);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void)
{
    // GPIO
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);

    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);
    status_led_init();

    // Config
    if (!config_store_load(&app.profiles)) {
        profiles_init(&app.profiles, NULL);
        app.config_dirty = true;
    }
    profiles_get(&app.profiles, app.profiles.active, &app.config);

    // Core state
    app.boot_ms = app_now_ms();
    crsf_init(&app.crsf);
#if ELRS_RX_PARAMS
    crsf_params_init(&app.rx_params, CRSF_ADDR_FLIGHT_CTRL, CRSF_ADDR_RECEIVER);
#endif
    failsafe_init(&app.failsafe, app.config.failsafe_ms, FAILSAFE_RETRY_MS);
    led_seq_init(&app.led);
    app.led_state = LED_STATE_OFF;
    button_init(&app.button, BUTTON_DEBOUNCE_MS, BUTTON_LONG_MS);
    mapping_failsafe_report(&app.config, &app.last_report);

    // Receiver power + UART
    rx_power_init(app.config.rx_power_on_boot != 0);

    // USB + console
    tud_init(0);
    cli_init();

    uint8_t buf[128];
    uint32_t ts[128];
    hid_report_t report;

    while (true) {
        tud_task();
        uint32_t now = app_now_ms();

        rx_power_task(now);

        // Deferred bind command once the RX has had time to boot.
        if (app.bind_pending && app.bind_requested_ms != 0 && rx_power_is_on() && !rx_power_bind_cycle_active() &&
            (uint32_t)(now - rx_power_on_since_ms()) >= BIND_AFTER_POWER_MS) {
            app_send_rx_command('b', 'd');
            app.bind_pending = false;
        }

        // CRSF in -> HID out. In passthrough the console owns the UART.
        // Bytes carry their IRQ arrival time so inter-byte timeouts reflect
        // the wire, not main-loop stalls. One report per drain, with the
        // freshest channels, since the HID endpoint takes one per 1 ms poll.
        if (!app.passthrough) {
            size_t n = rx_uart_read_ts(buf, ts, sizeof(buf));
            bool rc_updated = false;
            for (size_t i = 0; i < n; i++) {
                uint8_t type = crsf_feed(&app.crsf, buf[i], ts[i]);
                if (type == CRSF_TYPE_RC_CHANNELS) {
                    rc_updated = true;
                }
#if ELRS_RX_PARAMS
                else if (type >= CRSF_TYPE_DEVICE_PING) {
                    // The parser's buffer still holds the frame it just accepted.
                    crsf_params_on_frame(&app.rx_params, app.crsf.buf, (size_t)app.crsf.frame_len + 2);
                }
#endif
            }
#if ELRS_RX_PARAMS
            uint8_t req[CRSF_PARAMS_TX_MAX];
            size_t req_len = crsf_params_poll(&app.rx_params, now, req);
            if (req_len) rx_uart_write(req, req_len);
#endif
            if (rc_updated) {
                mapping_build_report(&app.config, app.crsf.channels, &report);
                send_report(&report);
            }
        }

        // Failsafe
        failsafe_input_t in = {
            .now_ms       = now,
            .rx_powered   = rx_power_is_on() && !app.passthrough,
            .have_rc      = app.crsf.have_rc,
            .last_rc_ms   = app.crsf.last_rc_ms,
            .have_link    = app.crsf.have_link,
            .last_link_ms = app.crsf.last_link_ms,
            .lq           = app.crsf.link.uplink_lq,
        };
        if (failsafe_update(&app.failsafe, &in)) {
            mapping_failsafe_report(&app.config, &report);
            failsafe_report_sent(&app.failsafe, send_report(&report));
        }

        update_button(now);
        update_led(now);
        cli_task(now);
    }
}
