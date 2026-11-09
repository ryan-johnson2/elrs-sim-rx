#include "failsafe.h"

void failsafe_init(failsafe_t *f, uint32_t timeout_ms, uint32_t retry_ms)
{
    f->timeout_ms = timeout_ms;
    f->retry_ms = retry_ms;
    f->active = true;             // start in failsafe until a frame arrives
    f->send_pending = true;
    f->last_attempt_ms = (uint32_t)0 - retry_ms;   // first send is immediate
    f->engage_count = 0;
}

static bool link_ok(const failsafe_t *f, const failsafe_input_t *in)
{
    if (!in->rx_powered) return false;
    if (!in->have_rc) return false;
    if ((uint32_t)(in->now_ms - in->last_rc_ms) >= f->timeout_ms) return false;
    // LQ 0 from the RX means it has declared the link lost, even if it keeps
    // emitting RC frames (ELRS "Set Position" failsafe mode). Only ignore the
    // report once RC frames are clearly newer than it.
    if (in->have_link && in->lq == 0 &&
        (int32_t)(in->last_rc_ms - in->last_link_ms) < FAILSAFE_LQ_STALE_MS) {
        return false;
    }
    return true;
}

bool failsafe_update(failsafe_t *f, const failsafe_input_t *in)
{
    if (link_ok(f, in)) {
        f->active = false;
        f->send_pending = false;
        return false;
    }

    if (!f->active) {
        f->active = true;
        f->send_pending = true;
        f->engage_count++;
        f->last_attempt_ms = in->now_ms - f->retry_ms;   // send immediately
    }

    if (f->send_pending && (uint32_t)(in->now_ms - f->last_attempt_ms) >= f->retry_ms) {
        f->last_attempt_ms = in->now_ms;
        return true;
    }
    return false;
}

void failsafe_report_sent(failsafe_t *f, bool ok)
{
    if (ok) {
        f->send_pending = false;
    }
}

bool failsafe_is_active(const failsafe_t *f)
{
    return f->active;
}
