#include "crsf.h"
#include "crc.h"
#include <string.h>

void crsf_init(crsf_t *c)
{
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        c->channels[i] = CRSF_CH_MID;
    }
}

static bool is_sync(uint8_t b)
{
    // 0xC8 is what the RX puts on its serial port. Accept the other common
    // device addresses too so a frame captured elsewhere still parses.
    return b == CRSF_SYNC_BYTE || b == 0xEE || b == 0xEA || b == CRSF_ADDR_RECEIVER;
}

static void unpack_channels(crsf_t *c, const uint8_t *p)
{
    // 16 channels x 11 bits, little-endian bit packing.
    uint32_t bits = 0;
    int nbits = 0;
    int ch = 0;
    for (int i = 0; i < 22 && ch < CRSF_NUM_CHANNELS; i++) {
        bits |= (uint32_t)p[i] << nbits;
        nbits += 8;
        while (nbits >= 11 && ch < CRSF_NUM_CHANNELS) {
            c->channels[ch++] = bits & 0x7FF;
            bits >>= 11;
            nbits -= 11;
        }
    }
}

static void handle_frame(crsf_t *c, uint32_t now_ms)
{
    uint8_t type = c->buf[2];
    const uint8_t *payload = &c->buf[3];
    uint8_t payload_len = c->frame_len - 2;   // minus type and crc

    c->frames_ok++;
    c->last_frame_ms = now_ms;

    switch (type) {
    case CRSF_TYPE_RC_CHANNELS:
        if (payload_len >= 22) {
            unpack_channels(c, payload);
            c->have_rc = true;
            c->last_rc_ms = now_ms;
        }
        break;
    case CRSF_TYPE_LINK_STATS:
        if (payload_len >= 10) {
            c->link.uplink_rssi1    = payload[0];
            c->link.uplink_rssi2    = payload[1];
            c->link.uplink_lq       = payload[2];
            c->link.uplink_snr      = (int8_t)payload[3];
            c->link.active_antenna  = payload[4];
            c->link.rf_mode         = payload[5];
            c->link.uplink_tx_power = payload[6];
            c->link.downlink_rssi   = payload[7];
            c->link.downlink_lq     = payload[8];
            c->link.downlink_snr    = (int8_t)payload[9];
            c->have_link = true;
            c->last_link_ms = now_ms;
        }
        break;
    default:
        break;
    }
}

uint8_t crsf_feed(crsf_t *c, uint8_t byte, uint32_t now_ms)
{
    // Resync on a gap in the middle of a frame.
    if (c->pos > 0 && (uint32_t)(now_ms - c->last_byte_ms) > CRSF_INTER_BYTE_TIMEOUT_MS) {
        c->frames_timeout++;
        c->bytes_discarded += c->pos;
        c->pos = 0;
    }
    c->last_byte_ms = now_ms;

    if (c->pos == 0) {
        if (!is_sync(byte)) {
            c->bytes_discarded++;
            return 0;
        }
        c->buf[c->pos++] = byte;
        return 0;
    }

    if (c->pos == 1) {
        // len covers type + payload + crc, so at least 2 (type + crc) and at
        // most CRSF_MAX_FRAME_LEN - 2.
        if (byte < 2 || byte > CRSF_MAX_FRAME_LEN - 2) {
            c->bytes_discarded += 2;
            c->pos = 0;
            return 0;
        }
        c->frame_len = byte;
        c->buf[c->pos++] = byte;
        return 0;
    }

    c->buf[c->pos++] = byte;

    if (c->pos < (uint8_t)(c->frame_len + 2)) {
        return 0;
    }

    // Complete frame. CRC is over type + payload.
    uint8_t crc = crc8_dvb_s2(&c->buf[2], c->frame_len - 1);
    uint8_t type = 0;
    if (crc == c->buf[c->frame_len + 1]) {
        type = c->buf[2];
        handle_frame(c, now_ms);
    } else {
        c->frames_crc_err++;
    }
    c->pos = 0;
    return type;
}

uint16_t crsf_channel_to_us(uint16_t ch)
{
    // 172..1811 -> 988..2012, centre 992 -> 1500. Scale factor 5/8.
    int32_t us = ((int32_t)ch - CRSF_CH_MID) * 5 / 8 + 1500;
    if (us < 0) us = 0;
    if (us > 3000) us = 3000;
    return (uint16_t)us;
}

size_t crsf_build_rx_command(uint8_t *out, char c0, char c1)
{
    out[0] = CRSF_ADDR_RECEIVER;
    out[1] = 4;                       // type + 2 payload + crc
    out[2] = CRSF_TYPE_COMMAND;
    out[3] = (uint8_t)c0;
    out[4] = (uint8_t)c1;
    out[5] = crc8_dvb_s2(&out[2], 3);
    return 6;
}

size_t crsf_build_rc_frame(uint8_t *out, const uint16_t ch[CRSF_NUM_CHANNELS])
{
    out[0] = CRSF_SYNC_BYTE;
    out[1] = 24;                      // type + 22 payload + crc
    out[2] = CRSF_TYPE_RC_CHANNELS;
    uint8_t *p = &out[3];
    memset(p, 0, 22);
    uint32_t bits = 0;
    int nbits = 0;
    int o = 0;
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) {
        bits |= (uint32_t)(ch[i] & 0x7FF) << nbits;
        nbits += 11;
        while (nbits >= 8) {
            p[o++] = bits & 0xFF;
            bits >>= 8;
            nbits -= 8;
        }
    }
    if (nbits > 0 && o < 22) {
        p[o] = bits & 0xFF;
    }
    out[25] = crc8_dvb_s2(&out[2], 23);
    return 26;
}

size_t crsf_build_link_stats_frame(uint8_t *out, const crsf_link_stats_t *ls)
{
    out[0] = CRSF_SYNC_BYTE;
    out[1] = 12;                      // type + 10 payload + crc
    out[2] = CRSF_TYPE_LINK_STATS;
    out[3] = ls->uplink_rssi1;
    out[4] = ls->uplink_rssi2;
    out[5] = ls->uplink_lq;
    out[6] = (uint8_t)ls->uplink_snr;
    out[7] = ls->active_antenna;
    out[8] = ls->rf_mode;
    out[9] = ls->uplink_tx_power;
    out[10] = ls->downlink_rssi;
    out[11] = ls->downlink_lq;
    out[12] = (uint8_t)ls->downlink_snr;
    out[13] = crc8_dvb_s2(&out[2], 11);
    return 14;
}
