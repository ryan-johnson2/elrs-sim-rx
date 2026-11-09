#include "test.h"
#include "failsafe.h"

static failsafe_input_t in_at(uint32_t now, bool powered, bool have_rc, uint32_t last_rc, uint8_t lq, bool have_link, uint32_t last_link)
{
    failsafe_input_t in = {
        .now_ms = now, .rx_powered = powered, .have_rc = have_rc, .last_rc_ms = last_rc,
        .have_link = have_link, .last_link_ms = last_link, .lq = lq,
    };
    return in;
}

static void test_starts_in_failsafe_and_sends_once(void)
{
    failsafe_t f;
    failsafe_init(&f, 100, 20);
    CHECK(failsafe_is_active(&f));

    failsafe_input_t in = in_at(0, true, false, 0, 0, false, 0);
    CHECK(failsafe_update(&f, &in));        // wants to send
    failsafe_report_sent(&f, true);
    in.now_ms = 50;
    CHECK(!failsafe_update(&f, &in));       // already sent, no repeat
    CHECK(failsafe_is_active(&f));
}

static void test_retries_until_delivered(void)
{
    failsafe_t f;
    failsafe_init(&f, 100, 20);
    failsafe_input_t in = in_at(1000, true, false, 0, 0, false, 0);
    CHECK(failsafe_update(&f, &in));
    failsafe_report_sent(&f, false);        // USB busy
    in.now_ms = 1010;
    CHECK(!failsafe_update(&f, &in));       // rate limited
    in.now_ms = 1020;
    CHECK(failsafe_update(&f, &in));        // retry
    failsafe_report_sent(&f, true);
    in.now_ms = 1100;
    CHECK(!failsafe_update(&f, &in));
}

static void test_link_clears_and_loss_reengages(void)
{
    failsafe_t f;
    failsafe_init(&f, 100, 20);

    failsafe_input_t in = in_at(500, true, true, 500, 100, true, 490);
    CHECK(!failsafe_update(&f, &in));
    CHECK(!failsafe_is_active(&f));

    // Frames keep coming
    in = in_at(590, true, true, 580, 100, true, 570);
    CHECK(!failsafe_update(&f, &in));

    // 99 ms gap: still fine
    in = in_at(679, true, true, 580, 100, true, 570);
    CHECK(!failsafe_update(&f, &in));
    CHECK(!failsafe_is_active(&f));

    // 100 ms gap: failsafe, send immediately
    in = in_at(680, true, true, 580, 100, true, 570);
    CHECK(failsafe_update(&f, &in));
    CHECK(failsafe_is_active(&f));
    CHECK_EQ(f.engage_count, 1);
    failsafe_report_sent(&f, true);

    // Link returns
    in = in_at(2000, true, true, 1999, 100, true, 1990);
    CHECK(!failsafe_update(&f, &in));
    CHECK(!failsafe_is_active(&f));
}

static void test_lq_zero_after_last_rc_triggers(void)
{
    failsafe_t f;
    failsafe_init(&f, 100, 20);
    failsafe_input_t in = in_at(1000, true, true, 1000, 100, true, 990);
    failsafe_update(&f, &in);
    CHECK(!failsafe_is_active(&f));

    // 20 ms later a link stats frame says LQ 0, newer than the last RC frame.
    in = in_at(1020, true, true, 1000, 0, true, 1020);
    CHECK(failsafe_update(&f, &in));
    CHECK(failsafe_is_active(&f));
}

static void test_lq_zero_older_than_rc_is_ignored(void)
{
    failsafe_t f;
    failsafe_init(&f, 100, 20);
    // Stale LQ 0 from before the RC frame should not matter.
    failsafe_input_t in = in_at(1000, true, true, 1000, 0, true, 900);
    CHECK(!failsafe_update(&f, &in));
    CHECK(!failsafe_is_active(&f));
}

static void test_rx_unpowered_is_failsafe(void)
{
    failsafe_t f;
    failsafe_init(&f, 100, 20);
    failsafe_input_t in = in_at(1000, true, true, 1000, 100, true, 990);
    failsafe_update(&f, &in);
    CHECK(!failsafe_is_active(&f));

    in = in_at(1001, false, true, 1000, 100, true, 990);
    CHECK(failsafe_update(&f, &in));
    CHECK(failsafe_is_active(&f));
}

static void test_millis_wraparound(void)
{
    failsafe_t f;
    failsafe_init(&f, 100, 20);
    uint32_t t = 0xFFFFFFF0u;
    failsafe_input_t in = in_at(t, true, true, t, 100, false, 0);
    failsafe_update(&f, &in);
    CHECK(!failsafe_is_active(&f));
    in = in_at(t + 50, true, true, t, 100, false, 0);   // wraps past zero
    CHECK(!failsafe_update(&f, &in));
    CHECK(!failsafe_is_active(&f));
    in = in_at(t + 100, true, true, t, 100, false, 0);
    CHECK(failsafe_update(&f, &in));
    CHECK(failsafe_is_active(&f));
}

int main(void)
{
    RUN(test_starts_in_failsafe_and_sends_once);
    RUN(test_retries_until_delivered);
    RUN(test_link_clears_and_loss_reengages);
    RUN(test_lq_zero_after_last_rc_triggers);
    RUN(test_lq_zero_older_than_rc_is_ignored);
    RUN(test_rx_unpowered_is_failsafe);
    RUN(test_millis_wraparound);
    TEST_MAIN_END();
}
