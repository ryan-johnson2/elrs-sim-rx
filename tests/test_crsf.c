#include "test.h"
#include "crsf.h"
#include "crc.h"

static uint8_t feed_all(crsf_t *c, const uint8_t *buf, size_t n, uint32_t t)
{
    uint8_t last = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t r = crsf_feed(c, buf[i], t);
        if (r) last = r;
    }
    return last;
}

static void test_rc_frame_roundtrip(void)
{
    crsf_t c;
    crsf_init(&c);

    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(172 + i * 100);
    ch[0] = 172; ch[1] = 992; ch[2] = 1811; ch[15] = 1811;

    uint8_t frame[26];
    size_t n = crsf_build_rc_frame(frame, ch);
    CHECK_EQ(n, 26);
    CHECK_EQ(frame[0], 0xC8);
    CHECK_EQ(frame[1], 24);
    CHECK_EQ(frame[2], 0x16);

    uint8_t type = feed_all(&c, frame, n, 1000);
    CHECK_EQ(type, CRSF_TYPE_RC_CHANNELS);
    CHECK(c.have_rc);
    CHECK_EQ(c.last_rc_ms, 1000);
    CHECK_EQ(c.frames_ok, 1);
    for (int i = 0; i < 16; i++) CHECK_EQ(c.channels[i], ch[i]);
}

static void test_known_rc_payload(void)
{
    // All channels at 992 (0x3E0), packed LSB-first 11 bits each. The first
    // eight payload bytes are E0 03 1F F8 C0 07 3E F0 (pattern period is 11 bytes).
    crsf_t c;
    crsf_init(&c);
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = 992;
    uint8_t frame[26];
    crsf_build_rc_frame(frame, ch);
    CHECK_EQ(frame[3], 0xE0);
    CHECK_EQ(frame[4], 0x03);
    CHECK_EQ(frame[5], 0x1F);
    CHECK_EQ(frame[6], 0xF8);
    CHECK_EQ(frame[7], 0xC0);
    CHECK_EQ(frame[8], 0x07);
    CHECK_EQ(frame[9], 0x3E);
    CHECK_EQ(frame[10], 0xF0);
    CHECK_EQ(feed_all(&c, frame, 26, 5), CRSF_TYPE_RC_CHANNELS);
    for (int i = 0; i < 16; i++) CHECK_EQ(c.channels[i], 992);
}

static void test_link_stats_roundtrip(void)
{
    crsf_t c;
    crsf_init(&c);
    crsf_link_stats_t ls = {
        .uplink_rssi1 = 60, .uplink_rssi2 = 70, .uplink_lq = 99, .uplink_snr = -5,
        .active_antenna = 1, .rf_mode = 6, .uplink_tx_power = 3,
        .downlink_rssi = 55, .downlink_lq = 100, .downlink_snr = 8,
    };
    uint8_t frame[14];
    size_t n = crsf_build_link_stats_frame(frame, &ls);
    CHECK_EQ(n, 14);
    CHECK_EQ(feed_all(&c, frame, n, 20), CRSF_TYPE_LINK_STATS);
    CHECK(c.have_link);
    CHECK(!c.have_rc);
    CHECK_EQ(c.link.uplink_lq, 99);
    CHECK_EQ(c.link.uplink_snr, -5);
    CHECK_EQ(c.link.rf_mode, 6);
    CHECK_EQ(c.link.downlink_snr, 8);
    CHECK_EQ(c.last_link_ms, 20);
}

static void test_crc_error_is_rejected(void)
{
    crsf_t c;
    crsf_init(&c);
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = 1500;
    uint8_t frame[26];
    crsf_build_rc_frame(frame, ch);
    frame[10] ^= 0x01;   // corrupt payload
    CHECK_EQ(feed_all(&c, frame, 26, 0), 0);
    CHECK_EQ(c.frames_crc_err, 1);
    CHECK_EQ(c.frames_ok, 0);
    CHECK(!c.have_rc);
    CHECK_EQ(c.channels[0], 992);   // untouched
}

static void test_garbage_then_frame(void)
{
    crsf_t c;
    crsf_init(&c);
    uint8_t junk[] = { 0x00, 0x55, 0xAA, 0x13 };
    feed_all(&c, junk, sizeof(junk), 0);
    CHECK_EQ(c.bytes_discarded, 4);

    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = 1000;
    uint8_t frame[26];
    crsf_build_rc_frame(frame, ch);
    CHECK_EQ(feed_all(&c, frame, 26, 1), CRSF_TYPE_RC_CHANNELS);
    CHECK_EQ(c.channels[3], 1000);
}

static void test_inter_byte_timeout_resyncs(void)
{
    crsf_t c;
    crsf_init(&c);
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = 1200;
    uint8_t frame[26];
    crsf_build_rc_frame(frame, ch);

    // Half a frame, then a gap, then a full frame.
    feed_all(&c, frame, 12, 100);
    CHECK_EQ(feed_all(&c, frame, 26, 110), CRSF_TYPE_RC_CHANNELS);
    CHECK_EQ(c.frames_timeout, 1);
    CHECK_EQ(c.frames_ok, 1);
    CHECK_EQ(c.channels[0], 1200);
}

static void test_bad_length_rejected(void)
{
    crsf_t c;
    crsf_init(&c);
    uint8_t bad1[] = { 0xC8, 0x01 };       // too short
    uint8_t bad2[] = { 0xC8, 0x7F };       // too long
    feed_all(&c, bad1, 2, 0);
    feed_all(&c, bad2, 2, 0);
    CHECK_EQ(c.pos, 0);
    CHECK_EQ(c.frames_ok, 0);
}

static void test_two_frames_back_to_back(void)
{
    crsf_t c;
    crsf_init(&c);
    uint16_t a[16], b[16];
    for (int i = 0; i < 16; i++) { a[i] = 300; b[i] = 1700; }
    uint8_t buf[52];
    crsf_build_rc_frame(buf, a);
    crsf_build_rc_frame(buf + 26, b);
    int rc_frames = 0;
    for (size_t i = 0; i < 52; i++) {
        if (crsf_feed(&c, buf[i], 0) == CRSF_TYPE_RC_CHANNELS) rc_frames++;
    }
    CHECK_EQ(rc_frames, 2);
    CHECK_EQ(c.channels[0], 1700);
}

static void test_channel_to_us(void)
{
    CHECK_EQ(crsf_channel_to_us(992), 1500);
    CHECK_EQ(crsf_channel_to_us(172), 988);
    CHECK_EQ(crsf_channel_to_us(1811), 2011);   // 5/8 scaling, integer math
    CHECK_EQ(crsf_channel_to_us(0), 880);
}

static void test_rx_command_frames_match_elrs_flasher(void)
{
    // ExpressLRS src/python/bootloader.py:
    //   INIT_SEQ = [0xEC,0x04,0x32,'b','l'] + crc
    //   BIND_SEQ = [0xEC,0x04,0x32,'b','d'] + crc
    uint8_t f[8];
    size_t n = crsf_build_rx_command(f, 'b', 'l');
    CHECK_EQ(n, 6);
    CHECK_EQ(f[0], 0xEC);
    CHECK_EQ(f[1], 0x04);
    CHECK_EQ(f[2], 0x32);
    CHECK_EQ(f[3], 'b');
    CHECK_EQ(f[4], 'l');
    CHECK_EQ(f[5], crc8_dvb_s2(&f[2], 3));

    n = crsf_build_rx_command(f, 'b', 'd');
    CHECK_EQ(f[4], 'd');
    CHECK_EQ(f[5], crc8_dvb_s2(&f[2], 3));

    // The frame we build must parse as a valid COMMAND frame too.
    crsf_t c;
    crsf_init(&c);
    CHECK_EQ(feed_all(&c, f, n, 0), CRSF_TYPE_COMMAND);
}

static void test_build_rx_model_id(void)
{
    // ExpressLRS RXEndpoint::handleRaw: [0xEC, len>=4, 0x32, 'm', 'm', id] + crc,
    // id 0-63 or 0xFF for model match off.
    uint8_t f[8];
    size_t n = crsf_build_rx_model_id(f, 7);
    CHECK_EQ(n, 7);
    CHECK_EQ(f[0], 0xEC);
    CHECK_EQ(f[1], 0x05);
    CHECK_EQ(f[2], 0x32);
    CHECK_EQ(f[3], 'm');
    CHECK_EQ(f[4], 'm');
    CHECK_EQ(f[5], 7);
    CHECK_EQ(f[6], crc8_dvb_s2(&f[2], 4));

    n = crsf_build_rx_model_id(f, CRSF_MODEL_ID_OFF);
    CHECK_EQ(f[5], 0xFF);
    CHECK_EQ(f[6], crc8_dvb_s2(&f[2], 4));

    crsf_t c;
    crsf_init(&c);
    CHECK_EQ(feed_all(&c, f, n, 0), CRSF_TYPE_COMMAND);
}

int main(void)
{
    RUN(test_rc_frame_roundtrip);
    RUN(test_known_rc_payload);
    RUN(test_link_stats_roundtrip);
    RUN(test_crc_error_is_rejected);
    RUN(test_garbage_then_frame);
    RUN(test_inter_byte_timeout_resyncs);
    RUN(test_bad_length_rejected);
    RUN(test_two_frames_back_to_back);
    RUN(test_channel_to_us);
    RUN(test_rx_command_frames_match_elrs_flasher);
    RUN(test_build_rx_model_id);
    TEST_MAIN_END();
}
