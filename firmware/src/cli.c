#include "cli.h"
#include "app.h"
#include "board.h"
#include "rx_uart.h"
#include "rx_power.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
#ifndef PRODUCT_NAME
#define PRODUCT_NAME "ELRS Sim RX"
#endif

#define LINE_MAX 128

// The ExpressLRS flasher closes and reopens the port between its steps (CLI
// setup at 115200, bootloader reset at 420000, then esptool), exactly as it
// does against a flight controller, where passthrough lasts until reboot. So a
// closed port must not end the bridge at once. It ends when the port has stayed
// closed this long (flasher finished or gave up) ...
#define PASSTHROUGH_CLOSED_MS 10000
// ... or when the host has kept it open but sent nothing for this long. Long
// enough for esptool to sit waiting on a flash erase.
#define PASSTHROUGH_IDLE_MS 60000

static char     line[LINE_MAX];
static size_t   line_len;
static bool     bf_mode;            // after '#', Betaflight-style prompt
static bool     stream;             // periodic JSON channel stream
static uint32_t stream_last_ms;
static uint32_t stream_interval_ms = 50;

static uint32_t passthrough_last_host_ms;
static uint32_t passthrough_last_open_ms;
static uint32_t passthrough_baud;

static volatile bool pending_bootsel;
static bool     bootsel_no_drive;   // `dfu nodrive`: bootloader without the RPI-RP2 drive

#if ELRS_RX_PARAMS
// A receiver settings request is running; its reply is the reply to the
// console command that started it, so no further input is read until then.
static bool rx_params_waiting;
#endif

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------
// Bounded: if the host is not draining the port, drop output after a few ms
// rather than stall the main loop (HID, failsafe, LED all run from it).
#define OUT_WRITE_TIMEOUT_MS 5
static uint32_t out_dropped_bytes;

static void out_write(const char *s, size_t n)
{
    if (!tud_cdc_connected()) { out_dropped_bytes += n; return; }
    absolute_time_t deadline = make_timeout_time_ms(OUT_WRITE_TIMEOUT_MS);
    while (n > 0) {
        uint32_t w = tud_cdc_write(s, (uint32_t)n);
        s += w;
        n -= w;
        if (n > 0) {
            tud_cdc_write_flush();
            tud_task();
            if (time_reached(deadline) || !tud_cdc_connected()) {
                out_dropped_bytes += n;
                return;
            }
        }
    }
}

static void out_str(const char *s)
{
    out_write(s, strlen(s));
}

static uint32_t out_truncated;   // debug counter: a single out_printf exceeded its buffer

static void out_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) { n = sizeof(buf) - 1; out_truncated++; }
    out_write(buf, (size_t)n);
}

static void out_prompt(void)
{
    if (bf_mode) {
        out_str("\r\n# ");
    }
}

// ---------------------------------------------------------------------------
// Passthrough
// ---------------------------------------------------------------------------
static void passthrough_enter(uint32_t baud, uint32_t now_ms)
{
    passthrough_baud = baud;
    passthrough_last_host_ms = now_ms;
    passthrough_last_open_ms = now_ms;
    if (!rx_power_is_on()) {
        rx_power_set(true, now_ms);
    }
    rx_uart_set_baud(baud);
    app.passthrough = true;
}

static void passthrough_exit(void)
{
    app.passthrough = false;
    bf_mode = false;
    rx_uart_set_baud(CRSF_BAUD);
    // RX may have been reflashed; counters restart with the next frame.
    crsf_init(&app.crsf);
}

static void passthrough_task(uint32_t now_ms)
{
    uint8_t buf[256];

    if (tud_cdc_connected()) passthrough_last_open_ms = now_ms;
    if ((uint32_t)(now_ms - passthrough_last_open_ms) > PASSTHROUGH_CLOSED_MS ||
        (uint32_t)(now_ms - passthrough_last_host_ms) > PASSTHROUGH_IDLE_MS) {
        passthrough_exit();
        return;
    }

    // host -> RX
    if (tud_cdc_available()) {
        uint32_t n = tud_cdc_read(buf, sizeof(buf));
        if (n > 0) {
            rx_uart_write(buf, n);
            passthrough_last_host_ms = now_ms;
        }
    }

    // RX -> host
    size_t n = rx_uart_read(buf, sizeof(buf));
    if (n > 0) {
        out_write((const char *)buf, n);
        tud_cdc_write_flush();
    }
}

// ---------------------------------------------------------------------------
// JSON emitters
// ---------------------------------------------------------------------------
static void emit_channels_json(void)
{
    out_str("{\"ch\":[");
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        out_printf("%s%u", i ? "," : "", app.crsf.channels[i]);
    }
    out_printf("],\"lq\":%u,\"rssi\":-%u,\"snr\":%d,\"rfmode\":%u,\"link\":%s}\r\n",
               app.crsf.link.uplink_lq, app.crsf.link.uplink_rssi1,
               app.crsf.link.uplink_snr, app.crsf.link.rf_mode,
               failsafe_is_active(&app.failsafe) ? "false" : "true");
}

static const char *led_state_name(led_state_t s)
{
    switch (s) {
    case LED_STATE_OFF:           return "off";
    case LED_STATE_CONNECTED:     return "connected";
    case LED_STATE_DISCONNECTED:  return "disconnected";
    case LED_STATE_BINDING:       return "binding";
    case LED_STATE_WIFI:          return "wifi";
    case LED_STATE_SERIAL_UPDATE: return "serial_update";
    default:                      return "?";
    }
}

static void emit_status_json(uint32_t now_ms)
{
    // Emitted in pieces: the whole line is ~400 bytes, larger than one printf buffer.
    out_printf("{\"product\":\"%s\",\"version\":\"%s\",\"variant\":\"%s\",\"uptime_ms\":%lu,"
               "\"rx_power\":%s,\"link\":%s,\"failsafe\":%s,\"led\":\"%s\",",
               PRODUCT_NAME, FW_VERSION, VARIANT_NAME, (unsigned long)(now_ms - app.boot_ms),
               rx_power_is_on() ? "true" : "false",
               failsafe_is_active(&app.failsafe) ? "false" : "true",
               failsafe_is_active(&app.failsafe) ? "true" : "false",
               led_state_name(app.led_state));
    out_printf("\"lq\":%u,\"rssi\":-%u,\"snr\":%d,\"rfmode\":%u,\"last_rc_age_ms\":%ld,",
               app.crsf.link.uplink_lq, app.crsf.link.uplink_rssi1,
               app.crsf.link.uplink_snr, app.crsf.link.rf_mode,
               app.crsf.have_rc ? (long)(now_ms - app.crsf.last_rc_ms) : -1L);
    out_printf("\"frames_ok\":%lu,\"frames_crc_err\":%lu,\"frames_timeout\":%lu,\"uart_overruns\":%lu,",
               (unsigned long)app.crsf.frames_ok, (unsigned long)app.crsf.frames_crc_err,
               (unsigned long)app.crsf.frames_timeout, (unsigned long)rx_uart_rx_overruns());
    out_printf("\"reports_sent\":%lu,\"reports_dropped\":%lu,\"console_dropped\":%lu,\"console_truncated\":%lu,"
               "\"profile\":%u,\"config_dirty\":%s}\r\n",
               (unsigned long)app.reports_sent, (unsigned long)app.reports_dropped,
               (unsigned long)out_dropped_bytes, (unsigned long)out_truncated,
               (unsigned)app.profiles.active + 1, app.config_dirty ? "true" : "false");
}

static void emit_map_json(void)
{
    const config_t *c = &app.config;
    out_str("{\"axes\":[");
    for (int a = 0; a < NUM_AXES; a++) {
        out_printf("%s{\"name\":\"%s\",\"ch\":%u,\"invert\":%s}", a ? "," : "",
                   mapping_axis_name(a), c->axis_channel[a],
                   ((c->axis_invert >> a) & 1) ? "true" : "false");
    }
    out_str("],\"buttons\":[");
    for (int b = 0; b < NUM_BUTTONS; b++) {
        out_printf("%s{\"ch\":%u,\"level\":\"%s\"}", b ? "," : "",
                   c->button_channel[b], mapping_level_name((btn_level_t)c->button_level[b]));
    }
    char name[PROFILE_NAME_LEN];
    profiles_name(&app.profiles, app.profiles.active, name);    // cleaned on the way in, safe to print as is
    out_printf("],\"throttle_axis\":%d,\"rx_power_on_boot\":%s,\"failsafe_ms\":%u,"
               "\"profile\":%u,\"profile_name\":\"%s\"}\r\n",
               c->throttle_axis == 0xFF ? -1 : (int)c->throttle_axis,
               c->rx_power_on_boot ? "true" : "false", c->failsafe_ms,
               (unsigned)app.profiles.active + 1, name);
}

// All 16 slots. `ok` is included so the same line answers the profile commands.
static void emit_profiles_json(void)
{
    out_printf("{\"ok\":true,\"active\":%u,\"profiles\":[", (unsigned)app.profiles.active + 1);
    for (int n = 0; n < PROFILE_COUNT; n++) {
        char name[PROFILE_NAME_LEN];
        profiles_name(&app.profiles, n, name);
        out_printf("%s{\"n\":%d,\"used\":%s,\"name\":\"%s\"}", n ? "," : "", n + 1,
                   app.profiles.slot[n].used ? "true" : "false", name);
    }
    out_str("]}\r\n");
}

static void emit_err(const char *msg);

#if ELRS_RX_PARAMS
// JSON string from device-supplied text: quotes, backslashes, control bytes
// and anything non-ASCII are escaped (bytes map to U+0000..U+00FF).
static void out_json_str(const char *s)
{
    char buf[64];
    size_t n = 0;
    buf[n++] = '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (n > sizeof(buf) - 8) { out_write(buf, n); n = 0; }
        if (c == '"' || c == '\\') { buf[n++] = '\\'; buf[n++] = (char)c; }
        else if (c < 0x20 || c >= 0x7F) n += (size_t)snprintf(&buf[n], 7, "\\u%04x", c);
        else buf[n++] = (char)c;
    }
    buf[n++] = '"';
    out_write(buf, n);
}

static void emit_rx_params_result(void)
{
    const crsf_params_t *p = &app.rx_params;
    if (p->state != CRSF_PARAMS_DONE) {
        out_printf("{\"ok\":false,\"error\":\"%s\"}\r\n",
                   p->err == CRSF_PARAMS_ERR_TIMEOUT  ? "no reply from receiver" :
                   p->err == CRSF_PARAMS_ERR_OVERFLOW ? "receiver setting too large" :
                                                        "bad reply from receiver");
        return;
    }
    if (p->op == CRSF_PARAMS_OP_PING) {
        const crsf_device_info_t *d = &p->info;
        out_str("{\"ok\":true,\"name\":");
        out_json_str(d->name);
        out_printf(",\"serial\":%lu,\"hw\":%lu,\"sw\":\"%u.%u.%u\",\"fields\":%u,\"param_version\":%u}\r\n",
                   (unsigned long)d->serial, (unsigned long)d->hw_version,
                   (unsigned)((d->sw_version >> 16) & 0xFF), (unsigned)((d->sw_version >> 8) & 0xFF),
                   (unsigned)(d->sw_version & 0xFF), d->field_count, d->param_version);
        return;
    }
    crsf_param_field_t f;
    if (!crsf_params_parse_field(p->field, (size_t)p->field_len + 1, &f)) {
        emit_err("bad reply from receiver");
        return;
    }
    out_printf("{\"ok\":true,\"id\":%u,\"parent\":%u,\"type\":%u,\"hidden\":%s,\"name\":",
               p->field_id, f.parent, f.type, f.hidden ? "true" : "false");
    out_json_str(f.name);
    switch (f.type) {
    case CRSF_PARAM_SELECTION:
        out_str(",\"options\":");
        out_json_str(f.options);
        // fall through
    case CRSF_PARAM_UINT8: case CRSF_PARAM_INT8: case CRSF_PARAM_UINT16:
    case CRSF_PARAM_INT16: case CRSF_PARAM_UINT32: case CRSF_PARAM_INT32:
        if (f.type == CRSF_PARAM_UINT32) {
            out_printf(",\"value\":%lu,\"min\":%lu,\"max\":%lu,\"units\":", (unsigned long)(uint32_t)f.value,
                       (unsigned long)(uint32_t)f.min, (unsigned long)(uint32_t)f.max);
        } else {
            out_printf(",\"value\":%ld,\"min\":%ld,\"max\":%ld,\"units\":", (long)f.value, (long)f.min, (long)f.max);
        }
        out_json_str(f.units);
        break;
    case CRSF_PARAM_COMMAND:
        out_printf(",\"step\":%u,\"timeout\":%u", f.step, f.timeout);
        // fall through
    case CRSF_PARAM_STRING:
    case CRSF_PARAM_INFO:
        out_str(",\"text\":");
        out_json_str(f.text);
        break;
    case CRSF_PARAM_FOLDER:
        out_str(",\"children\":[");
        for (unsigned i = 0; i < f.n_children; i++) out_printf("%s%u", i ? "," : "", f.children[i]);
        out_str("]");
        break;
    default:
        break;
    }
    out_str("}\r\n");
}
#endif

static void emit_ok(void)
{
    out_str("{\"ok\":true}\r\n");
}

static void emit_err(const char *msg)
{
    out_printf("{\"ok\":false,\"error\":\"%s\"}\r\n", msg);
}

// ---------------------------------------------------------------------------
// Command handling
// ---------------------------------------------------------------------------
static int split(char *s, char **argv, int max)
{
    int argc = 0;
    char *p = s;
    while (*p && argc < max) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
    }
    return argc;
}

static bool parse_level(const char *s, btn_level_t *out)
{
    if (!strcmp(s, "low"))  { *out = BTN_LOW;  return true; }
    if (!strcmp(s, "mid"))  { *out = BTN_MID;  return true; }
    if (!strcmp(s, "high")) { *out = BTN_HIGH; return true; }
    if (!strcmp(s, "none")) { *out = BTN_NONE; return true; }
    return false;
}

static void cmd_help(void)
{
    out_str(
        PRODUCT_NAME " " FW_VERSION "\r\n"
        "  help                         this text\r\n"
        "  version                      firmware version\r\n"
        "  status                       link, failsafe, counters (JSON)\r\n"
        "  channels                     raw CRSF channels + link stats (JSON)\r\n"
        "  stream on|off [ms]           periodic channels output\r\n"
        "  map                          current mapping (JSON)\r\n"
        "  axis <0-7> <ch 1-16|0> [invert]\r\n"
        "  button <0-35> <ch 1-16|0> <low|mid|high|none>\r\n"
        "  throttle <axis 0-7|none>     axis driven low on failsafe\r\n"
        "  failsafe <ms>                RC gap before failsafe (20-250)\r\n"
        "  profile                      list the 16 mapping profiles\r\n"
        "  profile use <1-16>           switch profile (drops unsaved changes)\r\n"
        "  profile save <1-16> [name]   save the current mapping to a slot and switch to it\r\n"
        "  profile name <1-16> <name>   rename a profile\r\n"
        "  profile delete <1-16>        empty a slot (not the active one)\r\n"
#if !ELRS_LITE
        "  rxpower on|off|toggle        receiver power now\r\n"
        "  rxboot on|off                receiver power at plug-in\r\n"
#endif
        "  bind                         send CRSF bind command to RX\r\n"
#if !ELRS_LITE
        "  bindcycle                    power-cycle RX 3x to enter bind\r\n"
#endif
        "  modelmatch <0-63|off>        set the receiver's model-match ID\r\n"
#if ELRS_RX_PARAMS
        "  rxinfo                       receiver name, version, number of settings\r\n"
        "  rxparam <id>                 read receiver setting <id> (1..count)\r\n"
        "  rxparam <id> <value> [bytes] write it (bytes 1, 2 or 4; default 1)\r\n"
#endif
        "  rxbootloader                 send CRSF bootloader command to RX\r\n"
        "  save | defaults | reboot\r\n"
        "  dfu [nodrive]                reboot into the bootloader; nodrive hides the RPI-RP2 drive\r\n"
        "  #                            Betaflight CLI mode (for ELRS flasher)\r\n");
}

static void handle_bf_command(int argc, char **argv, uint32_t now_ms)
{
    // Minimal Betaflight CLI surface used by the ExpressLRS flasher:
    //   get serialrx_provider / serialrx_inverted / serialrx_halfduplex / rx_spi_protocol
    //   serial                 -> table with a CRSF (function 64) port
    //   serialpassthrough N B  -> bridge
    //   exit
    if (!strcmp(argv[0], "get") && argc >= 2) {
        if (!strcmp(argv[1], "serialrx_provider"))        out_str("serialrx_provider = CRSF\r\n");
        else if (!strcmp(argv[1], "serialrx_inverted"))   out_str("serialrx_inverted = OFF\r\n");
        else if (!strcmp(argv[1], "serialrx_halfduplex")) out_str("serialrx_halfduplex = OFF\r\n");
        else if (!strcmp(argv[1], "rx_spi_protocol"))     out_str("rx_spi_protocol = OFF\r\n");
        else out_printf("%s = 0\r\n", argv[1]);
        return;
    }
    if (!strcmp(argv[0], "serial")) {
        // identifier, function mask (64 = RX_SERIAL), msp/gps/telemetry/blackbox bauds
        out_str("serial 5 64 115200 57600 0 115200\r\n");
        return;
    }
    if (!strcmp(argv[0], "serialpassthrough")) {
        uint32_t baud = CRSF_BAUD;
        if (argc >= 3) {
            uint32_t b = (uint32_t)strtoul(argv[2], NULL, 10);
            if (b >= 9600 && b <= 2000000) baud = b;
        }
        out_printf("Port 5 opened, baud = %lu.\r\n", (unsigned long)baud);
        tud_cdc_write_flush();
        passthrough_enter(baud, now_ms);
        return;
    }
    if (!strcmp(argv[0], "exit")) {
        bf_mode = false;
        out_str("\r\nLeaving CLI mode\r\n");
        return;
    }
    if (!strcmp(argv[0], "version")) {
        out_str("# Betaflight / " PRODUCT_NAME " " FW_VERSION " (emulated CLI)\r\n");
        return;
    }
    out_printf("###ERROR: UNKNOWN COMMAND: '%s'###\r\n", argv[0]);
}

static void handle_command(char *cmdline, uint32_t now_ms)
{
    char *argv[10];                 // room for a profile name of several words
    int argc = split(cmdline, argv, 10);
    if (argc == 0) {
        out_prompt();
        return;
    }
    const char *cmd = argv[0];

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        cmd_help();
    } else if (!strcmp(cmd, "version")) {
        if (bf_mode) handle_bf_command(argc, argv, now_ms);
        else out_printf("{\"product\":\"%s\",\"version\":\"%s\",\"variant\":\"%s\"}\r\n", PRODUCT_NAME, FW_VERSION, VARIANT_NAME);
    } else if (!strcmp(cmd, "status")) {
        emit_status_json(now_ms);
    } else if (!strcmp(cmd, "channels")) {
        emit_channels_json();
    } else if (!strcmp(cmd, "stream")) {
        if (argc >= 2 && !strcmp(argv[1], "on")) {
            stream = true;
            if (argc >= 3) {
                int ms = atoi(argv[2]);
                if (ms >= 10 && ms <= 1000) stream_interval_ms = (uint32_t)ms;
            }
            emit_ok();
        } else if (argc >= 2 && !strcmp(argv[1], "off")) {
            stream = false;
            emit_ok();
        } else {
            emit_err("usage: stream on|off [ms]");
        }
    } else if (!strcmp(cmd, "map")) {
        emit_map_json();
    } else if (!strcmp(cmd, "axis")) {
        if (argc < 3) { emit_err("usage: axis <0-7> <ch> [invert]"); }
        else {
            int a = atoi(argv[1]);
            int ch = atoi(argv[2]);
            bool inv = (argc >= 4 && !strcmp(argv[3], "invert"));
            if (a < 0 || a >= NUM_AXES || ch < 0 || ch > CRSF_NUM_CHANNELS) {
                emit_err("out of range");
            } else {
                app.config.axis_channel[a] = (uint8_t)ch;
                if (inv) app.config.axis_invert |= (uint8_t)(1u << a);
                else     app.config.axis_invert &= (uint8_t)~(1u << a);
                app.config_dirty = true;
                emit_ok();
            }
        }
    } else if (!strcmp(cmd, "button")) {
        btn_level_t lvl;
        if (argc < 4 || !parse_level(argv[3], &lvl)) {
            emit_err("usage: button <0-35> <ch> <low|mid|high|none>");
        } else {
            int b = atoi(argv[1]);
            int ch = atoi(argv[2]);
            if (b < 0 || b >= NUM_BUTTONS || ch < 0 || ch > CRSF_NUM_CHANNELS) {
                emit_err("out of range");
            } else {
                app.config.button_channel[b] = (uint8_t)ch;
                app.config.button_level[b] = (uint8_t)lvl;
                app.config_dirty = true;
                emit_ok();
            }
        }
    } else if (!strcmp(cmd, "throttle")) {
        if (argc < 2) { emit_err("usage: throttle <axis|none>"); }
        else if (!strcmp(argv[1], "none")) {
            app.config.throttle_axis = 0xFF; app.config_dirty = true; emit_ok();
        } else {
            int a = atoi(argv[1]);
            if (a < 0 || a >= NUM_AXES) emit_err("out of range");
            else { app.config.throttle_axis = (uint8_t)a; app.config_dirty = true; emit_ok(); }
        }
    } else if (!strcmp(cmd, "failsafe")) {
        int ms = (argc >= 2) ? atoi(argv[1]) : 0;
        if (ms < 20 || ms > 250) emit_err("usage: failsafe <20-250>");
        else {
            app.config.failsafe_ms = (uint8_t)ms;
            app.failsafe.timeout_ms = (uint32_t)ms;
            app.config_dirty = true;
            emit_ok();
        }
#if ELRS_LITE
    } else if (!strcmp(cmd, "rxpower") || !strcmp(cmd, "rxboot") || !strcmp(cmd, "bindcycle")) {
        emit_err("not on this build: no receiver power switch");
#endif
    } else if (!strcmp(cmd, "rxpower")) {
        if (argc < 2) emit_err("usage: rxpower on|off|toggle");
        else {
            bool on = rx_power_is_on();
            if (!strcmp(argv[1], "on")) on = true;
            else if (!strcmp(argv[1], "off")) on = false;
            else if (!strcmp(argv[1], "toggle")) on = !on;
            else { emit_err("usage: rxpower on|off|toggle"); goto done; }
            app_set_rx_power(on, now_ms);
            out_printf("{\"ok\":true,\"rx_power\":%s}\r\n", on ? "true" : "false");
        }
    } else if (!strcmp(cmd, "rxboot")) {
        if (argc < 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
            emit_err("usage: rxboot on|off");
        } else {
            app.config.rx_power_on_boot = !strcmp(argv[1], "on");
            app.config_dirty = true;
            emit_ok();
        }
    } else if (!strcmp(cmd, "bind")) {
        app_request_bind(now_ms);
        emit_ok();
    } else if (!strcmp(cmd, "bindcycle")) {
        app_request_bind_cycle(now_ms);
        emit_ok();
    } else if (!strcmp(cmd, "modelmatch")) {
        // Stored by the receiver, not by us. ID must equal the radio model's
        // "Receiver number" when Model Match is on in the ELRS Lua.
        char *end = NULL;
        long id = (argc >= 2) ? strtol(argv[1], &end, 10) : -1;
        bool off = (argc >= 2 && !strcmp(argv[1], "off"));
        const char *err = NULL;
        if (!off && (argc < 2 || end == argv[1] || *end || id < 0 || id > CRSF_MODEL_ID_MAX)) {
            emit_err("usage: modelmatch <0-63|off>");
        } else if (!app_set_rx_model_id(off ? CRSF_MODEL_ID_OFF : (uint8_t)id, now_ms, &err)) {
            emit_err(err);
        } else {
            emit_ok();
        }
#if ELRS_RX_PARAMS
    } else if (!strcmp(cmd, "rxinfo") || !strcmp(cmd, "rxparam")) {
        // The reply comes from cli_task once the receiver has answered.
        const char *err = NULL;
        char *e1 = NULL, *e2 = NULL, *e3 = NULL;
        bool info = cmd[2] == 'i';
        long id = (!info && argc >= 2) ? strtol(argv[1], &e1, 10) : 0;
        long val = (argc >= 3) ? strtol(argv[2], &e2, 0) : 0;
        long width = (argc >= 4) ? strtol(argv[3], &e3, 10) : 1;
        if (!info && (argc < 2 || *e1 || id < 0 || id > 255 || (argc >= 3 && (e2 == argv[2] || *e2)) ||
                      (argc >= 4 && (*e3 || (width != 1 && width != 2 && width != 4))))) {
            emit_err("usage: rxparam <id> [value [1|2|4]]");
        } else if (!app_rx_ready(now_ms, &err)) {
            emit_err(err);
        } else {
            crsf_params_cancel(&app.rx_params);
            if (info) {
                crsf_params_ping(&app.rx_params);
            } else if (argc >= 3) {
                uint8_t be[4];                         // big-endian, two's complement
                for (long i = 0; i < width; i++) be[i] = (uint8_t)((uint32_t)val >> (8 * (width - 1 - i)));
                crsf_params_write(&app.rx_params, (uint8_t)id, be, (size_t)width);
            } else {
                crsf_params_read(&app.rx_params, (uint8_t)id);
            }
            rx_params_waiting = true;
            return;
        }
#endif
    } else if (!strcmp(cmd, "rxbootloader")) {
        app_send_rx_command('b', 'l');
        emit_ok();
    } else if (!strcmp(cmd, "profile")) {
        // Slot numbers are 1..16 on the console. A name is the rest of the line.
        int n = (argc >= 3) ? atoi(argv[2]) - 1 : -1;
        char name[48] = "";
        for (int i = 3; i < argc; i++) {
            size_t len = strlen(name);
            snprintf(name + len, sizeof(name) - len, "%s%s", i > 3 ? " " : "", argv[i]);
        }
        if (argc == 1 || !strcmp(argv[1], "list")) {
            emit_profiles_json();
        } else if (n < 0 || n >= PROFILE_COUNT) {
            emit_err("usage: profile [use|save|name|delete] <1-16> [name]");
        } else if (!strcmp(argv[1], "use")) {
            if (app_profile_use(n)) emit_profiles_json(); else emit_err("could not store the profile");
        } else if (!strcmp(argv[1], "save")) {
            if (app_profile_save_as(n, name)) emit_profiles_json(); else emit_err("could not store the profile");
        } else if (!strcmp(argv[1], "name")) {
            if (!app.profiles.slot[n].used) emit_err("that slot is empty");
            else if (app_profile_rename(n, name)) emit_profiles_json();
            else emit_err("name needs letters or digits; allowed: A-Z a-z 0-9 space - _ . + ( ) #");
        } else if (!strcmp(argv[1], "delete")) {
            if (n == app.profiles.active) emit_err("cannot delete the active profile; switch to another first");
            else if (app_profile_delete(n)) emit_profiles_json();
            else emit_err("could not store the change");
        } else {
            emit_err("usage: profile [use|save|name|delete] <1-16> [name]");
        }
    } else if (!strcmp(cmd, "save")) {
        app_save_config();
        out_printf("{\"ok\":%s}\r\n", app.config_dirty ? "false" : "true");
    } else if (!strcmp(cmd, "defaults")) {
        config_defaults(&app.config);
        app.failsafe.timeout_ms = app.config.failsafe_ms;
        app.config_dirty = true;
        emit_ok();
    } else if (!strcmp(cmd, "reboot")) {
        out_str("{\"ok\":true,\"rebooting\":true}\r\n");
        tud_cdc_write_flush();
        sleep_ms(50);
        watchdog_reboot(0, 0, 0);
    } else if (!strcmp(cmd, "dfu")) {
        bootsel_no_drive = (argc >= 2 && !strcmp(argv[1], "nodrive"));
        out_printf("{\"ok\":true,\"bootsel\":true,\"drive\":%s}\r\n", bootsel_no_drive ? "false" : "true");
        tud_cdc_write_flush();
        pending_bootsel = true;
    } else if (bf_mode) {
        handle_bf_command(argc, argv, now_ms);
    } else {
        emit_err("unknown command, try help");
    }
done:
    out_prompt();
    tud_cdc_write_flush();
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void cli_init(void)
{
    line_len = 0;
    bf_mode = false;
    stream = false;
    pending_bootsel = false;
}

void cli_on_line_coding(uint32_t bit_rate)
{
    // 1200 baud "touch" = reboot into the UF2 bootloader (Arduino convention).
    if (bit_rate == 1200) {
        pending_bootsel = true;
        return;
    }
    // While bridging, follow the host's baud so esptool can change speed.
    if (app.passthrough && bit_rate >= 9600 && bit_rate <= 2000000) {
        passthrough_baud = bit_rate;
        rx_uart_set_baud(bit_rate);
    }
}

void cli_task(uint32_t now_ms)
{
    if (pending_bootsel) {
        app_reboot_to_bootsel(bootsel_no_drive);
    }

    if (app.passthrough) {
        passthrough_task(now_ms);
        return;
    }

    if (!tud_cdc_connected()) {
        line_len = 0;
        bf_mode = false;
        stream = false;
#if ELRS_RX_PARAMS
        rx_params_waiting = false;
        crsf_params_cancel(&app.rx_params);
#endif
        return;
    }

#if ELRS_RX_PARAMS
    if (rx_params_waiting && app.rx_params.state != CRSF_PARAMS_BUSY) {
        rx_params_waiting = false;
        emit_rx_params_result();
        out_prompt();
    }
    while (!rx_params_waiting && tud_cdc_available()) {
#else
    while (tud_cdc_available()) {
#endif
        uint8_t c;
        if (tud_cdc_read(&c, 1) != 1) break;

        // Betaflight entry: a bare '#' answers with the CLI banner and prompt.
        if (c == '#' && line_len == 0) {
            bf_mode = true;
            out_str("\r\nEntering CLI Mode, type 'exit' to return, or 'help'\r\n\r\n# ");
            tud_cdc_write_flush();
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (line_len > 0) {
                line[line_len] = 0;
                if (bf_mode) out_str("\r\n");   // echo newline like BF does
                handle_command(line, now_ms);
                line_len = 0;
            }
            // An empty line prints nothing, as in Betaflight. The ExpressLRS
            // flasher ends every command with CR LF and reads up to the first
            // "# "; a second prompt for the LF arrives late and is taken as
            // the (empty) reply to its next command.
            continue;
        }

        if (c == 0x08 || c == 0x7F) {         // backspace
            if (line_len > 0) line_len--;
            continue;
        }

        if (line_len < LINE_MAX - 1 && c >= 0x20 && c < 0x7F) {
            line[line_len++] = (char)c;
            if (bf_mode) out_write((const char *)&c, 1);   // echo in BF mode
        }
    }

    if (stream && (uint32_t)(now_ms - stream_last_ms) >= stream_interval_ms) {
        stream_last_ms = now_ms;
        emit_channels_json();
    }

    tud_cdc_write_flush();
}
