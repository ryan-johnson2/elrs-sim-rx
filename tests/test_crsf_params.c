// CRSF parameter client against a simulated device that chunks fields the
// way ExpressLRS does (CRSFEndpoint::sendParameter): 64-byte frames, so 56
// bytes of field data per chunk, no reply for a chunk that does not exist.
#include "test.h"
#include "crsf.h"
#include "crsf_params.h"
#include "crc.h"

#define SELF 0xC8
#define DEV  0xEC
#define CHUNK_MAX 56

typedef struct { const uint8_t *data; size_t len; } sim_field_t;

static const uint8_t F1_SELECTION[] = { 0, 9, 'P','r','o','t','o','c','o','l',0,
    'C','R','S','F',';','I','n','v','e','r','t','e','d',';','S','B','U','S',0, 1, 0, 2, 0, 0 };
static const uint8_t F2_UINT8[]  = { 0, 0, 'O','u','t',' ','C','h',0, 3, 1, 16, 0, 0 };
static const uint8_t F3_INT16[]  = { 2, 3, 'T','r','i','m',0, 0xFF,0xF6, 0xFC,0x18, 0x03,0xE8, 0,0, 'u','s',0 };
static const uint8_t F4_INFO[]   = { 0, 12, 'M','o','d','e','l',' ','I','d',0, 'O','f','f',0 };
static const uint8_t F5_FOLDER[] = { 0, 11, 'T','e','a','m',0, 6, 7, 0xFF };
static const uint8_t F6_HIDDEN_CMD[] = { 5, 0x80 | 13, 'B','i','n','d',0, 2, 200, 'E','n','t','e','r','i','n','g',0 };
static uint8_t F7_LONG[200];     // selection with a long option list: 4 chunks

static sim_field_t sim_fields[8];
static bool sim_answers = true;
static int  sim_drop_next;       // swallow this many requests
static bool sim_cmd_reply;       // answer writes with an entry, like a command
static uint8_t sim_last_write[40];
static size_t  sim_last_write_len;
static int  sim_requests;

static uint8_t replies[8][64];
static size_t  reply_len[8];
static int     n_replies;

static void sim_setup(void)
{
    size_t n = 0;
    F7_LONG[n++] = 0; F7_LONG[n++] = 9;
    memcpy(&F7_LONG[n], "Rate", 5); n += 5;
    for (; n < sizeof(F7_LONG) - 6; n++) F7_LONG[n] = (n % 7 == 0) ? ';' : (uint8_t)('a' + n % 26);
    F7_LONG[n++] = 0;                                   // end of options
    F7_LONG[n++] = 4; F7_LONG[n++] = 0; F7_LONG[n++] = 20; F7_LONG[n++] = 0;
    F7_LONG[n++] = 0;                                   // units ""
    sim_fields[1] = (sim_field_t){ F1_SELECTION, sizeof(F1_SELECTION) };
    sim_fields[2] = (sim_field_t){ F2_UINT8, sizeof(F2_UINT8) };
    sim_fields[3] = (sim_field_t){ F3_INT16, sizeof(F3_INT16) };
    sim_fields[4] = (sim_field_t){ F4_INFO, sizeof(F4_INFO) };
    sim_fields[5] = (sim_field_t){ F5_FOLDER, sizeof(F5_FOLDER) };
    sim_fields[6] = (sim_field_t){ F6_HIDDEN_CMD, sizeof(F6_HIDDEN_CMD) };
    sim_fields[7] = (sim_field_t){ F7_LONG, n };
    sim_answers = true; sim_drop_next = 0; sim_cmd_reply = false; sim_requests = 0; n_replies = 0;
}

static void push_reply(uint8_t type, const uint8_t *payload, size_t n)
{
    uint8_t *f = replies[n_replies];
    f[0] = 0xC8; f[1] = (uint8_t)(n + 4); f[2] = type; f[3] = SELF; f[4] = DEV;
    memcpy(&f[5], payload, n);
    f[5 + n] = crc8_dvb_s2(&f[2], n + 3);
    reply_len[n_replies++] = n + 6;
}

static void push_chunk(uint8_t id, uint8_t chunk)
{
    const sim_field_t *fl = (id < 8) ? &sim_fields[id] : NULL;
    if (!fl || !fl->data) return;
    uint8_t count = (uint8_t)((fl->len + CHUNK_MAX - 1) / CHUNK_MAX);
    if (chunk >= count) return;
    size_t off = (size_t)chunk * CHUNK_MAX;
    size_t sz = fl->len - off < CHUNK_MAX ? fl->len - off : CHUNK_MAX;
    uint8_t pl[60];
    pl[0] = id; pl[1] = (uint8_t)(count - chunk - 1);
    memcpy(&pl[2], fl->data + off, sz);
    push_reply(CRSF_TYPE_PARAM_ENTRY, pl, sz + 2);
}

// The device receives one request frame.
static void sim_rx(const uint8_t *f, size_t len)
{
    sim_requests++;
    CHECK(len >= 6);
    CHECK_EQ(f[0], 0xC8);
    CHECK_EQ(f[1], len - 2);
    CHECK_EQ(f[3], DEV);
    CHECK_EQ(f[4], SELF);
    CHECK_EQ(f[len - 1], crc8_dvb_s2(&f[2], len - 3));
    if (f[2] == CRSF_TYPE_PARAM_WRITE) {
        sim_last_write_len = len - 6;
        memcpy(sim_last_write, &f[5], sim_last_write_len);
    }
    if (!sim_answers) return;
    if (sim_drop_next > 0) { sim_drop_next--; return; }

    if (f[2] == CRSF_TYPE_DEVICE_PING) {
        static const uint8_t info[] = { 'E','P','2',' ','R','X',0, 'E','L','R','S', 0,0,0,0, 0,4,0,1, 7, 0 };
        push_reply(CRSF_TYPE_DEVICE_INFO, info, sizeof(info));
    } else if (f[2] == CRSF_TYPE_PARAM_READ) {
        push_chunk(f[5], f[6]);
    } else if (f[2] == CRSF_TYPE_PARAM_WRITE && sim_cmd_reply) {
        push_chunk(f[5], 0);
    }
}

// Run the client against the device until it leaves BUSY. Returns elapsed ms.
static uint32_t run(crsf_params_t *p, uint32_t *now)
{
    uint32_t t0 = *now;
    uint8_t out[CRSF_PARAMS_TX_MAX];
    while (p->state == CRSF_PARAMS_BUSY && *now - t0 < 5000) {
        size_t n = crsf_params_poll(p, *now, out);
        if (n) sim_rx(out, n);
        for (int i = 0; i < n_replies; i++) crsf_params_on_frame(p, replies[i], reply_len[i]);
        n_replies = 0;
        (*now)++;
    }
    return *now - t0;
}

static void test_ping(void)
{
    crsf_params_t p; uint32_t now = 1000;
    sim_setup();
    crsf_params_init(&p, SELF, DEV);
    CHECK(crsf_params_ping(&p));
    CHECK(!crsf_params_ping(&p));            // one at a time
    CHECK(!crsf_params_read(&p, 1));
    run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_DONE);
    CHECK(!strcmp(p.info.name, "EP2 RX"));
    CHECK_EQ(p.info.serial, 0x454C5253);
    CHECK_EQ(p.info.sw_version, 0x00040001);
    CHECK_EQ(p.info.field_count, 7);
    CHECK_EQ(sim_requests, 1);
}

static void test_read_single_chunk_and_parse(void)
{
    crsf_params_t p; uint32_t now = 0; crsf_param_field_t f;
    sim_setup();
    crsf_params_init(&p, SELF, DEV);

    CHECK(crsf_params_read(&p, 1));
    run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_DONE);
    CHECK_EQ(p.field_len, sizeof(F1_SELECTION));
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.type, CRSF_PARAM_SELECTION);
    CHECK(!f.hidden);
    CHECK(!strcmp(f.name, "Protocol"));
    CHECK(!strcmp(f.options, "CRSF;Inverted;SBUS"));
    CHECK_EQ(f.value, 1); CHECK_EQ(f.min, 0); CHECK_EQ(f.max, 2);
    CHECK(!strcmp(f.units, ""));

    CHECK(crsf_params_read(&p, 2)); run(&p, &now);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.type, CRSF_PARAM_UINT8);
    CHECK_EQ(f.value, 3); CHECK_EQ(f.min, 1); CHECK_EQ(f.max, 16);

    CHECK(crsf_params_read(&p, 3)); run(&p, &now);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.type, CRSF_PARAM_INT16);
    CHECK_EQ(f.parent, 2);
    CHECK_EQ(f.value, -10); CHECK_EQ(f.min, -1000); CHECK_EQ(f.max, 1000);
    CHECK(!strcmp(f.units, "us"));

    CHECK(crsf_params_read(&p, 4)); run(&p, &now);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.type, CRSF_PARAM_INFO);
    CHECK(!strcmp(f.name, "Model Id"));
    CHECK(!strcmp(f.text, "Off"));

    CHECK(crsf_params_read(&p, 5)); run(&p, &now);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.type, CRSF_PARAM_FOLDER);
    CHECK_EQ(f.n_children, 2);
    CHECK_EQ(f.children[0], 6); CHECK_EQ(f.children[1], 7);

    CHECK(crsf_params_read(&p, 6)); run(&p, &now);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.type, CRSF_PARAM_COMMAND);
    CHECK(f.hidden);
    CHECK_EQ(f.step, CRSF_CMD_EXECUTING); CHECK_EQ(f.timeout, 200);
    CHECK(!strcmp(f.text, "Entering"));
}

static void test_read_multi_chunk(void)
{
    crsf_params_t p; uint32_t now = 0; crsf_param_field_t f;
    sim_setup();
    crsf_params_init(&p, SELF, DEV);
    CHECK(crsf_params_read(&p, 7));
    run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_DONE);
    CHECK_EQ(p.field_len, sim_fields[7].len);
    CHECK(!memcmp(p.field, F7_LONG, p.field_len));
    CHECK_EQ(sim_requests, 4);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.value, 4); CHECK_EQ(f.max, 20);
}

static void test_retry_then_success(void)
{
    crsf_params_t p; uint32_t now = 0;
    sim_setup();
    sim_drop_next = 2;
    crsf_params_init(&p, SELF, DEV);
    CHECK(crsf_params_read(&p, 1));
    uint32_t took = run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_DONE);
    CHECK_EQ(sim_requests, 3);
    CHECK(took >= 2 * CRSF_PARAMS_TRY_TIMEOUT_MS);
}

static void test_timeout(void)
{
    crsf_params_t p; uint32_t now = 0xFFFFFF00u;       // across the ms wrap
    sim_setup();
    sim_answers = false;
    crsf_params_init(&p, SELF, DEV);
    CHECK(crsf_params_ping(&p));
    uint32_t took = run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_FAILED);
    CHECK_EQ(p.err, CRSF_PARAMS_ERR_TIMEOUT);
    CHECK_EQ(sim_requests, CRSF_PARAMS_MAX_TRIES);
    CHECK(took <= CRSF_PARAMS_MAX_TRIES * CRSF_PARAMS_TRY_TIMEOUT_MS + 5);

    // A field that does not exist gets no answer either.
    sim_answers = true;
    CHECK(crsf_params_read(&p, 99));
    run(&p, &now);
    CHECK_EQ(p.err, CRSF_PARAMS_ERR_TIMEOUT);
    // And the client is usable again afterwards.
    CHECK(crsf_params_read(&p, 4));
    run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_DONE);
}

static void test_duplicate_and_foreign_frames_ignored(void)
{
    crsf_params_t p; uint8_t out[CRSF_PARAMS_TX_MAX];
    sim_setup();
    crsf_params_init(&p, SELF, DEV);
    CHECK(crsf_params_read(&p, 7));
    CHECK(crsf_params_poll(&p, 0, out) > 0);

    // Wrong field, wrong origin, wrong destination, and unrelated frame types.
    push_chunk(1, 0);
    CHECK(!crsf_params_on_frame(&p, replies[0], reply_len[0]));
    n_replies = 0;
    push_chunk(7, 0);
    uint8_t bad[64];
    memcpy(bad, replies[0], reply_len[0]); bad[4] = 0xEE;
    CHECK(!crsf_params_on_frame(&p, bad, reply_len[0]));
    memcpy(bad, replies[0], reply_len[0]); bad[3] = 0xEA;
    CHECK(!crsf_params_on_frame(&p, bad, reply_len[0]));
    uint8_t rc[26]; uint16_t ch[16] = {0};
    size_t rn = crsf_build_rc_frame(rc, ch);
    CHECK(!crsf_params_on_frame(&p, rc, rn));
    CHECK(!crsf_params_on_frame(&p, replies[0], 5));
    CHECK_EQ(p.field_len, 0);

    // Chunk 0 twice (late answer plus the answer to a resend): counted once.
    CHECK(crsf_params_on_frame(&p, replies[0], reply_len[0]));
    CHECK(crsf_params_on_frame(&p, replies[0], reply_len[0]));
    CHECK_EQ(p.field_len, CHUNK_MAX);
    CHECK_EQ(p.chunk, 1);

    // Nothing is accepted once idle or done.
    crsf_params_cancel(&p);
    CHECK(!crsf_params_on_frame(&p, replies[0], reply_len[0]));
    CHECK_EQ(crsf_params_poll(&p, 10, out), 0);
}

static void test_overflow(void)
{
    crsf_params_t p; uint8_t out[CRSF_PARAMS_TX_MAX];
    sim_setup();
    crsf_params_init(&p, SELF, DEV);
    CHECK(crsf_params_read(&p, 1));
    crsf_params_poll(&p, 0, out);
    // A device that claims ever more chunks.
    uint8_t pl[58]; memset(pl, 'x', sizeof(pl));
    for (int rem = 200; p.state == CRSF_PARAMS_BUSY && rem > 0; rem--) {
        pl[0] = 1; pl[1] = (uint8_t)rem;
        n_replies = 0;
        push_reply(CRSF_TYPE_PARAM_ENTRY, pl, sizeof(pl));
        crsf_params_on_frame(&p, replies[0], reply_len[0]);
    }
    CHECK_EQ(p.state, CRSF_PARAMS_FAILED);
    CHECK_EQ(p.err, CRSF_PARAMS_ERR_OVERFLOW);
    CHECK(p.field_len <= CRSF_PARAMS_FIELD_MAX);
}

static void test_write_plain_value_reads_back(void)
{
    crsf_params_t p; uint32_t now = 0; crsf_param_field_t f;
    sim_setup();
    crsf_params_init(&p, SELF, DEV);
    uint8_t v[2] = { 0x01, 0x02 };
    CHECK(!crsf_params_write(&p, 3, v, 0));
    CHECK(!crsf_params_write(&p, 3, v, CRSF_PARAMS_WRITE_MAX + 1));
    CHECK(crsf_params_write(&p, 3, v, 2));
    uint32_t took = run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_DONE);
    CHECK_EQ(sim_last_write_len, 3);
    CHECK_EQ(sim_last_write[0], 3); CHECK_EQ(sim_last_write[1], 1); CHECK_EQ(sim_last_write[2], 2);
    CHECK_EQ(sim_requests, 2);                     // one write, one read
    CHECK(took >= CRSF_PARAMS_WRITE_SETTLE_MS);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK(!strcmp(f.name, "Trim"));
}

static void test_write_command_takes_its_own_reply(void)
{
    crsf_params_t p; uint32_t now = 0; crsf_param_field_t f;
    sim_setup();
    sim_cmd_reply = true;
    crsf_params_init(&p, SELF, DEV);
    uint8_t click = CRSF_CMD_CLICK;
    CHECK(crsf_params_write(&p, 6, &click, 1));
    uint32_t took = run(&p, &now);
    CHECK_EQ(p.state, CRSF_PARAMS_DONE);
    CHECK_EQ(sim_requests, 1);                     // no read: that would reset the command
    CHECK(took < CRSF_PARAMS_WRITE_SETTLE_MS);
    CHECK(crsf_params_parse_field(p.field, p.field_len + 1, &f));
    CHECK_EQ(f.step, CRSF_CMD_EXECUTING);
}

static void test_parse_rejects_truncated_and_survives_noise(void)
{
    crsf_param_field_t f;
    const sim_field_t *all[] = { &sim_fields[1], &sim_fields[2], &sim_fields[3], &sim_fields[4], &sim_fields[6] };
    sim_setup();
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        CHECK(crsf_params_parse_field(all[i]->data, all[i]->len, &f));
        // Every shorter prefix is missing something.
        for (size_t n = 0; n < all[i]->len; n++) {
            CHECK(!crsf_params_parse_field(all[i]->data, n, &f));
        }
    }
    // Unknown type: name only.
    static const uint8_t flt[] = { 0, 8, 'G','a','i','n',0, 1,2,3 };
    CHECK(crsf_params_parse_field(flt, sizeof(flt), &f));
    CHECK_EQ(f.type, CRSF_PARAM_FLOAT);
    CHECK(!strcmp(f.name, "Gain"));

    // Random data: must not read outside the buffer (exact-size heap blocks
    // so a sanitizer or valgrind run would catch it) and strings stay inside.
    uint32_t seed = 12345;
    for (int iter = 0; iter < 20000; iter++) {
        seed = seed * 1664525u + 1013904223u;
        size_t n = (seed >> 8) % 48;
        uint8_t *buf = malloc(n ? n : 1);
        for (size_t k = 0; k < n; k++) { seed = seed * 1664525u + 1013904223u; buf[k] = (uint8_t)(seed >> 16) & ((seed >> 28) ? 0xFF : 0x0F); }
        if (crsf_params_parse_field(buf, n, &f)) {
            const char *lo = (const char *)buf, *hi = (const char *)buf + n;
            CHECK(f.name >= lo && f.name < hi);
            if (f.type == CRSF_PARAM_FOLDER) CHECK((const char *)f.children + f.n_children <= hi);
        }
        free(buf);
    }
}

int main(void)
{
    RUN(test_ping);
    RUN(test_read_single_chunk_and_parse);
    RUN(test_read_multi_chunk);
    RUN(test_retry_then_success);
    RUN(test_timeout);
    RUN(test_duplicate_and_foreign_frames_ignored);
    RUN(test_overflow);
    RUN(test_write_plain_value_reads_back);
    RUN(test_write_command_takes_its_own_reply);
    RUN(test_parse_rejects_truncated_and_survives_noise);
    TEST_MAIN_END();
}
