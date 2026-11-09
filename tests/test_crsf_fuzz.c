// Fuzz the CRSF parser: random bytes, mutated valid frames, split deliveries,
// and timing gaps. The parser must never read outside its buffer, must always
// return to a sane state, and must decode every intact frame embedded in noise.
#include "test.h"
#include "crsf.h"
#include "crc.h"

static uint32_t rng_state = 0x12345678u;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void check_invariants(const crsf_t *c)
{
    CHECK(c->pos < CRSF_MAX_FRAME_LEN);
    if (c->pos >= 2) CHECK(c->frame_len >= 2 && c->frame_len <= CRSF_MAX_FRAME_LEN - 2);
    for (int i = 0; i < CRSF_NUM_CHANNELS; i++) CHECK(c->channels[i] <= 0x7FF);
}

static void test_random_bytes_never_break_state(void)
{
    crsf_t c;
    crsf_init(&c);
    uint32_t t = 0;
    for (int i = 0; i < 2000000; i++) {
        uint8_t b = (uint8_t)rnd();
        // mostly contiguous, occasionally a gap
        if ((rnd() & 0xFF) == 0) t += 10;
        crsf_feed(&c, b, t);
        if ((i & 0xFFF) == 0) check_invariants(&c);
    }
    check_invariants(&c);
    // Random data will occasionally pass CRC on short frames; that is fine,
    // but channel values must still be in range (checked above).
}

static void test_frames_in_noise_all_decode(void)
{
    crsf_t c;
    crsf_init(&c);
    uint8_t frame[26];
    uint16_t ch[16];
    int sent = 0, got = 0;
    uint32_t t = 0;

    for (int iter = 0; iter < 20000; iter++) {
        // noise burst, then a gap so the parser resyncs, then a frame
        int n = (int)(rnd() % 40);
        for (int k = 0; k < n; k++) crsf_feed(&c, (uint8_t)rnd(), t);
        t += 5;
        for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(CRSF_CH_MIN + rnd() % (CRSF_CH_MAX - CRSF_CH_MIN + 1));
        crsf_build_rc_frame(frame, ch);
        sent++;
        bool ok = false;
        for (int k = 0; k < 26; k++) {
            if (crsf_feed(&c, frame[k], t) == CRSF_TYPE_RC_CHANNELS) ok = true;
        }
        if (ok) {
            got++;
            for (int i = 0; i < 16; i++) {
                if (c.channels[i] != ch[i]) { CHECK(c.channels[i] == ch[i]); break; }
            }
        }
        t += 1;
    }
    CHECK_EQ(got, sent);
}

static void test_mutated_frames_rejected_or_valid(void)
{
    // Flip random bits in valid frames. Either the CRC rejects the frame, or
    // (rare collision) it is accepted; in both cases state stays sane and an
    // accepted frame must have a correct CRC.
    crsf_t c;
    crsf_init(&c);
    uint8_t frame[26];
    uint16_t ch[16];
    int accepted = 0, rejected = 0;
    for (int iter = 0; iter < 200000; iter++) {
        for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(CRSF_CH_MIN + rnd() % 1640);
        crsf_build_rc_frame(frame, ch);
        int flips = 1 + (int)(rnd() % 3);
        for (int f = 0; f < flips; f++) frame[2 + rnd() % 24] ^= (uint8_t)(1u << (rnd() % 8));

        uint32_t before = c.frames_ok;
        for (int k = 0; k < 26; k++) crsf_feed(&c, frame[k], 1000 + iter * 10);
        if (c.frames_ok != before) {
            accepted++;
            CHECK_EQ(crc8_dvb_s2(&frame[2], 23), frame[25]);
        } else {
            rejected++;
        }
        check_invariants(&c);
        c.pos = 0;   // start clean for the next mutated frame
    }
    CHECK(rejected > accepted * 50);   // CRC-8 should reject the overwhelming majority
}

static void test_split_and_interleaved_delivery(void)
{
    // Frames arriving one byte per call with arbitrary (sub-timeout) pauses
    // and link-stats frames interleaved.
    crsf_t c;
    crsf_init(&c);
    uint8_t rc[26], ls[14];
    uint16_t ch[16];
    crsf_link_stats_t stats = { .uplink_lq = 77, .rf_mode = 4 };
    crsf_build_link_stats_frame(ls, &stats);
    uint32_t t = 0;
    int rc_frames = 0, ls_frames = 0;
    for (int iter = 0; iter < 5000; iter++) {
        for (int i = 0; i < 16; i++) ch[i] = (uint16_t)(CRSF_CH_MIN + rnd() % 1640);
        crsf_build_rc_frame(rc, ch);
        for (int k = 0; k < 26; k++) {
            if (rnd() & 1) t += 1;          // 0 or 1 ms gaps, below timeout
            if (crsf_feed(&c, rc[k], t) == CRSF_TYPE_RC_CHANNELS) rc_frames++;
        }
        for (int k = 0; k < 14; k++) {
            if (crsf_feed(&c, ls[k], t) == CRSF_TYPE_LINK_STATS) ls_frames++;
        }
        CHECK_EQ(c.channels[7], ch[7]);
    }
    CHECK_EQ(rc_frames, 5000);
    CHECK_EQ(ls_frames, 5000);
    CHECK_EQ(c.link.uplink_lq, 77);
    CHECK_EQ(c.frames_timeout, 0);
}

static void test_timeout_boundary(void)
{
    crsf_t c;
    crsf_init(&c);
    uint8_t rc[26];
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = 1000;
    crsf_build_rc_frame(rc, ch);

    // Gap exactly equal to the timeout must not reset; one more ms must.
    crsf_feed(&c, rc[0], 100);
    crsf_feed(&c, rc[1], 100 + CRSF_INTER_BYTE_TIMEOUT_MS);
    CHECK_EQ(c.pos, 2);
    crsf_feed(&c, rc[2], 100 + CRSF_INTER_BYTE_TIMEOUT_MS + 1 + CRSF_INTER_BYTE_TIMEOUT_MS);
    CHECK_EQ(c.pos, 0);
    CHECK_EQ(c.frames_timeout, 1);
}

int main(void)
{
    RUN(test_random_bytes_never_break_state);
    RUN(test_frames_in_noise_all_decode);
    RUN(test_mutated_frames_rejected_or_valid);
    RUN(test_split_and_interleaved_delivery);
    RUN(test_timeout_boundary);
    TEST_MAIN_END();
}
