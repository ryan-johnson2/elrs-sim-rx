#include "mapping.h"
#include "crc.h"
#include <string.h>

static const char *const axis_names[NUM_AXES] = {
    "X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"
};

void config_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    // AETR: ch1 aileron -> X, ch2 elevator -> Y, ch3 throttle -> Z, ch4 rudder -> Rx.
    // ch5..8 also on the remaining axes so switches/pots are visible as analog.
    for (int i = 0; i < NUM_AXES; i++) {
        cfg->axis_channel[i] = (uint8_t)(i + 1);
    }
    cfg->axis_invert = 0;

    // ch5..16 -> three buttons each (low, mid, high).
    for (int i = 0; i < NUM_BUTTONS; i++) {
        cfg->button_channel[i] = (uint8_t)(5 + i / 3);
        cfg->button_level[i]   = (uint8_t)(BTN_LOW + i % 3);
    }

    cfg->throttle_axis     = AXIS_Z;
    cfg->rx_power_on_boot  = 1;
    cfg->failsafe_ms       = 100;

    config_finalize(cfg);
}

uint32_t config_compute_crc(const config_t *cfg)
{
    return crc32_ieee((const uint8_t *)cfg, offsetof(config_t, crc));
}

void config_finalize(config_t *cfg)
{
    cfg->magic   = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;
    cfg->size    = (uint16_t)sizeof(config_t);
    cfg->crc     = config_compute_crc(cfg);
}

bool config_is_valid(const config_t *cfg)
{
    if (cfg->magic != CONFIG_MAGIC) return false;
    if (cfg->version != CONFIG_VERSION) return false;
    if (cfg->size != sizeof(config_t)) return false;
    if (cfg->crc != config_compute_crc(cfg)) return false;
    for (int i = 0; i < NUM_AXES; i++) {
        if (cfg->axis_channel[i] > CRSF_NUM_CHANNELS) return false;
    }
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (cfg->button_channel[i] > CRSF_NUM_CHANNELS) return false;
        if (cfg->button_level[i] > BTN_HIGH) return false;
    }
    if (cfg->throttle_axis != 0xFF && cfg->throttle_axis >= NUM_AXES) return false;
    if (cfg->failsafe_ms < FAILSAFE_MS_MIN || cfg->failsafe_ms > FAILSAFE_MS_MAX) return false;
    return true;
}

int16_t mapping_channel_to_axis(uint16_t ch, bool invert)
{
    // 172..1811 -> -32767..32767 with 992 at exactly 0.
    int32_t v = ((int32_t)ch - CRSF_CH_MID) * AXIS_MAX / (CRSF_CH_MAX - CRSF_CH_MID);
    if (v < AXIS_MIN) v = AXIS_MIN;
    if (v > AXIS_MAX) v = AXIS_MAX;
    if (invert) v = -v;
    return (int16_t)v;
}

bool mapping_button_pressed(uint16_t ch, btn_level_t level)
{
    uint16_t us = crsf_channel_to_us(ch);
    switch (level) {
    case BTN_LOW:  return us < BTN_LOW_MAX_US;
    case BTN_MID:  return us >= BTN_LOW_MAX_US && us <= BTN_HIGH_MIN_US;
    case BTN_HIGH: return us > BTN_HIGH_MIN_US;
    default:       return false;
    }
}

void mapping_build_report(const config_t *cfg, const uint16_t ch[CRSF_NUM_CHANNELS], hid_report_t *out)
{
    memset(out, 0, sizeof(*out));

    for (int a = 0; a < NUM_AXES; a++) {
        uint8_t src = cfg->axis_channel[a];
        uint16_t raw = (src >= 1 && src <= CRSF_NUM_CHANNELS) ? ch[src - 1] : CRSF_CH_MID;
        out->axes[a] = mapping_channel_to_axis(raw, (cfg->axis_invert >> a) & 1);
    }

    for (int b = 0; b < NUM_BUTTONS; b++) {
        uint8_t src = cfg->button_channel[b];
        if (src < 1 || src > CRSF_NUM_CHANNELS) continue;
        if (mapping_button_pressed(ch[src - 1], (btn_level_t)cfg->button_level[b])) {
            out->buttons[b / 8] |= (uint8_t)(1u << (b % 8));
        }
    }
}

void mapping_failsafe_report(const config_t *cfg, hid_report_t *out)
{
    memset(out, 0, sizeof(*out));   // all axes centred, no buttons
    if (cfg->throttle_axis < NUM_AXES) {
        bool inv = (cfg->axis_invert >> cfg->throttle_axis) & 1;
        out->axes[cfg->throttle_axis] = inv ? AXIS_MAX : AXIS_MIN;
    }
}

const char *mapping_axis_name(int axis)
{
    return (axis >= 0 && axis < NUM_AXES) ? axis_names[axis] : "?";
}

const char *mapping_level_name(btn_level_t level)
{
    switch (level) {
    case BTN_LOW:  return "low";
    case BTN_MID:  return "mid";
    case BTN_HIGH: return "high";
    default:       return "none";
    }
}
