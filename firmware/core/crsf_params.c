#include "crsf_params.h"
#include "crsf.h"
#include "crc.h"
#include <string.h>

void crsf_params_init(crsf_params_t *p, uint8_t self_addr, uint8_t dev_addr)
{
    memset(p, 0, sizeof(*p));
    p->self_addr = self_addr;
    p->dev_addr = dev_addr;
}

static bool start(crsf_params_t *p, crsf_params_op_t op, uint8_t field_id)
{
    if (p->state == CRSF_PARAMS_BUSY) return false;
    p->state = CRSF_PARAMS_BUSY;
    p->op = op;
    p->err = CRSF_PARAMS_ERR_NONE;
    p->field_id = field_id;
    p->chunk = 0;
    p->remaining = 0;
    p->have_chunk = false;
    p->phase = 0;
    p->tries = 0;
    p->tx_pending = true;
    p->field_len = 0;
    p->field[0] = 0;
    return true;
}

static void fail(crsf_params_t *p, crsf_params_err_t err)
{
    p->state = CRSF_PARAMS_FAILED;
    p->err = err;
}

bool crsf_params_ping(crsf_params_t *p)
{
    return start(p, CRSF_PARAMS_OP_PING, 0);
}

bool crsf_params_read(crsf_params_t *p, uint8_t field_id)
{
    return start(p, CRSF_PARAMS_OP_READ, field_id);
}

bool crsf_params_write(crsf_params_t *p, uint8_t field_id, const uint8_t *value, size_t len)
{
    if (len == 0 || len > CRSF_PARAMS_WRITE_MAX) return false;
    if (!start(p, CRSF_PARAMS_OP_WRITE, field_id)) return false;
    memcpy(p->write_val, value, len);
    p->write_len = (uint8_t)len;
    return true;
}

void crsf_params_cancel(crsf_params_t *p)
{
    p->state = CRSF_PARAMS_IDLE;
    p->op = CRSF_PARAMS_OP_NONE;
}

static size_t build(const crsf_params_t *p, uint8_t *out, uint8_t type, const uint8_t *payload, size_t n)
{
    out[0] = CRSF_SYNC_BYTE;
    out[1] = (uint8_t)(n + 4);            // type + dest + origin + payload + crc
    out[2] = type;
    out[3] = p->dev_addr;
    out[4] = p->self_addr;
    if (n) memcpy(&out[5], payload, n);
    out[5 + n] = crc8_dvb_s2(&out[2], n + 3);
    return n + 6;
}

size_t crsf_params_poll(crsf_params_t *p, uint32_t now_ms, uint8_t *out)
{
    if (p->state != CRSF_PARAMS_BUSY) return 0;

    if (!p->tx_pending) {
        uint32_t waited = now_ms - p->sent_ms;
        if (p->op == CRSF_PARAMS_OP_WRITE && p->phase == 1) {
            // Nothing came back by itself: read the field to see the result.
            if (waited < CRSF_PARAMS_WRITE_SETTLE_MS) return 0;
            p->phase = 2;
        } else {
            if (waited < CRSF_PARAMS_TRY_TIMEOUT_MS) return 0;
            if (p->tries >= CRSF_PARAMS_MAX_TRIES) {
                fail(p, CRSF_PARAMS_ERR_TIMEOUT);
                return 0;
            }
        }
    }
    p->tx_pending = false;
    p->sent_ms = now_ms;

    uint8_t payload[1 + CRSF_PARAMS_WRITE_MAX];
    if (p->op == CRSF_PARAMS_OP_PING) {
        p->tries++;
        return build(p, out, CRSF_TYPE_DEVICE_PING, NULL, 0);
    }
    payload[0] = p->field_id;
    if (p->op == CRSF_PARAMS_OP_WRITE && p->phase == 0) {
        p->phase = 1;
        memcpy(&payload[1], p->write_val, p->write_len);
        return build(p, out, CRSF_TYPE_PARAM_WRITE, payload, 1u + p->write_len);
    }
    p->tries++;
    payload[1] = p->chunk;
    return build(p, out, CRSF_TYPE_PARAM_READ, payload, 2);
}

static uint32_t be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
}

static bool on_device_info(crsf_params_t *p, const uint8_t *pl, size_t n)
{
    const uint8_t *nul = memchr(pl, 0, n);
    if (!nul) return false;
    size_t name_len = (size_t)(nul - pl);
    if (n - name_len - 1 < 14) return false;
    const uint8_t *d = nul + 1;

    size_t copy = name_len < sizeof(p->info.name) - 1 ? name_len : sizeof(p->info.name) - 1;
    memcpy(p->info.name, pl, copy);
    p->info.name[copy] = 0;
    p->info.serial        = be32(&d[0]);
    p->info.hw_version    = be32(&d[4]);
    p->info.sw_version    = be32(&d[8]);
    p->info.field_count   = d[12];
    p->info.param_version = d[13];
    return true;
}

bool crsf_params_on_frame(crsf_params_t *p, const uint8_t *frame, size_t len)
{
    // sync, len, type, dest, origin, crc at the least
    if (p->state != CRSF_PARAMS_BUSY || len < 6) return false;
    if (frame[4] != p->dev_addr) return false;
    if (frame[3] != p->self_addr && frame[3] != 0x00) return false;   // us or broadcast

    uint8_t type = frame[2];
    const uint8_t *pl = &frame[5];
    size_t n = len - 6;

    if (type == CRSF_TYPE_DEVICE_INFO && p->op == CRSF_PARAMS_OP_PING) {
        if (on_device_info(p, pl, n)) p->state = CRSF_PARAMS_DONE;
        else fail(p, CRSF_PARAMS_ERR_MALFORMED);
        return true;
    }

    bool reading = p->op == CRSF_PARAMS_OP_READ || (p->op == CRSF_PARAMS_OP_WRITE && p->phase >= 1);
    if (type != CRSF_TYPE_PARAM_ENTRY || !reading) return false;
    if (n < 2 || pl[0] != p->field_id) return false;

    // Replies do not carry their chunk number, only how many follow. A late
    // answer to a request we already resent would otherwise be added twice.
    uint8_t remaining = pl[1];
    if (p->have_chunk && remaining != (uint8_t)(p->remaining - 1)) return true;

    size_t add = n - 2;
    if (p->field_len + add > CRSF_PARAMS_FIELD_MAX) {
        fail(p, CRSF_PARAMS_ERR_OVERFLOW);
        return true;
    }
    memcpy(&p->field[p->field_len], &pl[2], add);
    p->field_len = (uint16_t)(p->field_len + add);
    p->field[p->field_len] = 0;
    p->have_chunk = true;
    p->remaining = remaining;

    if (remaining == 0) {
        p->state = CRSF_PARAMS_DONE;
    } else {
        p->phase = 2;
        p->chunk++;
        p->tries = 0;
        p->tx_pending = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Field parsing
// ---------------------------------------------------------------------------
typedef struct {
    const uint8_t *p;
    size_t left;
} cursor_t;

static const char *take_str(cursor_t *c)
{
    const uint8_t *nul = c->left ? memchr(c->p, 0, c->left) : NULL;
    if (!nul) return NULL;
    const char *s = (const char *)c->p;
    size_t used = (size_t)(nul - c->p) + 1;
    c->p += used;
    c->left -= used;
    return s;
}

static bool take_int(cursor_t *c, unsigned width, bool is_signed, int32_t *out)
{
    if (c->left < width) return false;
    uint32_t v = 0;
    for (unsigned i = 0; i < width; i++) v = (v << 8) | c->p[i];
    c->p += width;
    c->left -= width;
    if (is_signed && width == 1) *out = (int8_t)v;
    else if (is_signed && width == 2) *out = (int16_t)v;
    else *out = (int32_t)v;
    return true;
}

bool crsf_params_parse_field(const uint8_t *data, size_t len, crsf_param_field_t *f)
{
    memset(f, 0, sizeof(*f));
    f->units = f->options = f->text = "";
    if (len < 3) return false;

    f->parent = data[0];
    f->type = data[1] & 0x7F;
    f->hidden = (data[1] & 0x80) != 0;
    cursor_t c = { data + 2, len - 2 };
    if (!(f->name = take_str(&c))) { f->name = ""; return false; }

    switch (f->type) {
    case CRSF_PARAM_UINT8: case CRSF_PARAM_INT8:
    case CRSF_PARAM_UINT16: case CRSF_PARAM_INT16:
    case CRSF_PARAM_UINT32: case CRSF_PARAM_INT32: {
        unsigned width = 1u << (f->type / 2);
        bool sg = f->type & 1;
        if (!take_int(&c, width, sg, &f->value) || !take_int(&c, width, sg, &f->min) ||
            !take_int(&c, width, sg, &f->max) || !take_int(&c, width, sg, &f->def)) return false;
        const char *u = take_str(&c);
        if (!u) return false;
        f->units = u;
        return true;
    }
    case CRSF_PARAM_SELECTION: {
        const char *o = take_str(&c);
        if (!o) return false;
        f->options = o;
        if (!take_int(&c, 1, false, &f->value) || !take_int(&c, 1, false, &f->min) ||
            !take_int(&c, 1, false, &f->max) || !take_int(&c, 1, false, &f->def)) return false;
        const char *u = take_str(&c);
        if (!u) return false;
        f->units = u;
        return true;
    }
    case CRSF_PARAM_STRING:
    case CRSF_PARAM_INFO: {
        const char *t = take_str(&c);
        if (!t) return false;
        f->text = t;
        return true;
    }
    case CRSF_PARAM_COMMAND: {
        if (c.left < 2) return false;
        f->step = c.p[0];
        f->timeout = c.p[1];
        c.p += 2;
        c.left -= 2;
        const char *t = take_str(&c);
        if (!t) return false;
        f->text = t;
        return true;
    }
    case CRSF_PARAM_FOLDER: {
        // Child ids up to 0xFF or the end of the data. 0 is the root, never a child.
        f->children = c.p;
        size_t k = 0;
        while (k < c.left && k < 255 && c.p[k] != 0xFF && c.p[k] != 0) k++;
        f->n_children = (uint8_t)k;
        return true;
    }
    default:
        return true;
    }
}
