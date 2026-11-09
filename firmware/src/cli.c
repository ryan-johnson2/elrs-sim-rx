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

// Passthrough leaves when the host has been silent this long, or disconnects.
#define PASSTHROUGH_IDLE_MS 10000

static char     line[LINE_MAX];
static size_t   line_len;
static bool     bf_mode;            // after '#', Betaflight-style prompt
static bool     stream;             // periodic JSON channel stream
static uint32_t stream_last_ms;
static uint32_t stream_interval_ms = 50;

static bool     passthrough;
static uint32_t passthrough_last_host_ms;
static uint32_t passthrough_baud;

static volatile bool pending_bootsel;

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------
static void out_write(const char *s, size_t n)
{
    while (n > 0) {
        uint32_t w = tud_cdc_write(s, (uint32_t)n);
        s += w;
        n -= w;
        if (n > 0) {
            tud_task();
            tud_cdc_write_flush();
        }
    }
}

static void out_str(const char *s)
{
    out_write(s, strlen(s));
}

static void out_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;
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
    passthrough = true;
    passthrough_baud = baud;
    passthrough_last_host_ms = now_ms;
    if (!rx_power_is_on()) {
        rx_power_set(true, now_ms);
    }
    rx_uart_set_baud(baud);
    app.passthrough = true;
}

static void passthrough_exit(void)
{
    passthrough = false;
    app.passthrough = false;
    bf_mode = false;
    rx_uart_set_baud(CRSF_BAUD);
    // RX may have been reflashed; counters restart with the next frame.
    crsf_init(&app.crsf);
}

static void passthrough_task(uint32_t now_ms)
{
    uint8_t buf[256];

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

    if (!tud_cdc_connected() ||
        (uint32_t)(now_ms - passthrough_last_host_ms) > PASSTHROUGH_IDLE_MS) {
        passthrough_exit();
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
    out_printf("{\"product\":\"%s\",\"version\":\"%s\",\"uptime_ms\":%lu,"
               "\"rx_power\":%s,\"link\":%s,\"failsafe\":%s,\"led\":\"%s\","
               "\"lq\":%u,\"rssi\":-%u,\"snr\":%d,\"rfmode\":%u,"
               "\"frames_ok\":%lu,\"frames_crc_err\":%lu,\"frames_timeout\":%lu,"
               "\"uart_overruns\":%lu,\"reports_sent\":%lu,\"reports_dropped\":%lu,"
               "\"last_rc_age_ms\":%ld,\"config_dirty\":%s}\r\n",
               PRODUCT_NAME, FW_VERSION, (unsigned long)(now_ms - app.boot_ms),
               rx_power_is_on() ? "true" : "false",
               failsafe_is_active(&app.failsafe) ? "false" : "true",
               failsafe_is_active(&app.failsafe) ? "true" : "false",
               led_state_name(app.led_state),
               app.crsf.link.uplink_lq, app.crsf.link.uplink_rssi1,
               app.crsf.link.uplink_snr, app.crsf.link.rf_mode,
               (unsigned long)app.crsf.frames_ok, (unsigned long)app.crsf.frames_crc_err,
               (unsigned long)app.crsf.frames_timeout, (unsigned long)rx_uart_rx_overruns(),
               (unsigned long)app.reports_sent, (unsigned long)app.reports_dropped,
               app.crsf.have_rc ? (long)(now_ms - app.crsf.last_rc_ms) : -1L,
               app.config_dirty ? "true" : "false");
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
    out_printf("],\"throttle_axis\":%d,\"rx_power_on_boot\":%s,\"failsafe_ms\":%u}\r\n",
               c->throttle_axis == 0xFF ? -1 : (int)c->throttle_axis,
               c->rx_power_on_boot ? "true" : "false", c->failsafe_ms);
}

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
        "  rxpower on|off|toggle        receiver power now\r\n"
        "  rxboot on|off                receiver power at plug-in\r\n"
        "  bind                         send CRSF bind command to RX\r\n"
        "  bindcycle                    power-cycle RX 3x to enter bind\r\n"
        "  rxbootloader                 send CRSF bootloader command to RX\r\n"
        "  save | defaults | reboot | dfu\r\n"
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
    char *argv[6];
    int argc = split(cmdline, argv, 6);
    if (argc == 0) {
        out_prompt();
        return;
    }
    const char *cmd = argv[0];

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        cmd_help();
    } else if (!strcmp(cmd, "version")) {
        if (bf_mode) handle_bf_command(argc, argv, now_ms);
        else out_printf("{\"product\":\"%s\",\"version\":\"%s\"}\r\n", PRODUCT_NAME, FW_VERSION);
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
    } else if (!strcmp(cmd, "rxpower")) {
        if (argc < 2) emit_err("usage: rxpower on|off|toggle");
        else {
            bool on = rx_power_is_on();
            if (!strcmp(argv[1], "on")) on = true;
            else if (!strcmp(argv[1], "off")) on = false;
            else if (!strcmp(argv[1], "toggle")) on = !on;
            else { emit_err("usage: rxpower on|off|toggle"); goto done; }
            rx_power_set(on, now_ms);
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
    } else if (!strcmp(cmd, "rxbootloader")) {
        app_send_rx_command('b', 'l');
        emit_ok();
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
        out_str("{\"ok\":true,\"bootsel\":true}\r\n");
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
    passthrough = false;
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
    if (passthrough && bit_rate >= 9600 && bit_rate <= 2000000) {
        passthrough_baud = bit_rate;
        rx_uart_set_baud(bit_rate);
    }
}

void cli_task(uint32_t now_ms)
{
    if (pending_bootsel) {
        app_reboot_to_bootsel();
    }

    if (passthrough) {
        passthrough_task(now_ms);
        return;
    }

    if (!tud_cdc_connected()) {
        line_len = 0;
        bf_mode = false;
        stream = false;
        return;
    }

    while (tud_cdc_available()) {
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
            } else if (bf_mode) {
                out_prompt();
                tud_cdc_write_flush();
            }
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
