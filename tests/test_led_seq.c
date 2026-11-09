#include "test.h"
#include "led_seq.h"

static void test_solid(void)
{
    led_seq_t l;
    led_seq_init(&l);
    CHECK(!led_seq_update(&l, 0));
    led_seq_set_solid(&l, true);
    CHECK(led_seq_update(&l, 0));
    CHECK(led_seq_update(&l, 123456));
    led_seq_set_solid(&l, false);
    CHECK(!led_seq_update(&l, 999));
}

static void test_disconnected_500_500(void)
{
    led_seq_t l;
    led_seq_init(&l);
    led_seq_set_pattern(&l, LEDSEQ_DISCONNECTED, 2, 1000);
    CHECK(led_seq_update(&l, 1000));
    CHECK(led_seq_update(&l, 1499));
    CHECK(!led_seq_update(&l, 1500));
    CHECK(!led_seq_update(&l, 1999));
    CHECK(led_seq_update(&l, 2000));
}

static void test_binding_double_blink(void)
{
    // 100 on, 100 off, 100 on, 1000 off, period 1300
    led_seq_t l;
    led_seq_init(&l);
    led_seq_set_pattern(&l, LEDSEQ_BINDING, 4, 0);
    CHECK(led_seq_update(&l, 0));
    CHECK(led_seq_update(&l, 99));
    CHECK(!led_seq_update(&l, 100));
    CHECK(!led_seq_update(&l, 199));
    CHECK(led_seq_update(&l, 200));
    CHECK(led_seq_update(&l, 299));
    CHECK(!led_seq_update(&l, 300));
    CHECK(!led_seq_update(&l, 1299));
    CHECK(led_seq_update(&l, 1300));    // next period
    CHECK(led_seq_update(&l, 2600));    // and the one after
}

static void test_wifi_fast_flicker(void)
{
    led_seq_t l;
    led_seq_init(&l);
    led_seq_set_pattern(&l, LEDSEQ_WIFI_UPDATE, 2, 0);
    CHECK(led_seq_update(&l, 0));
    CHECK(led_seq_update(&l, 19));
    CHECK(!led_seq_update(&l, 20));
    CHECK(!led_seq_update(&l, 49));
    CHECK(led_seq_update(&l, 50));
}

static void test_reapplying_same_pattern_does_not_restart(void)
{
    led_seq_t l;
    led_seq_init(&l);
    led_seq_set_pattern(&l, LEDSEQ_DISCONNECTED, 2, 0);
    CHECK(!led_seq_update(&l, 600));                 // in off phase
    led_seq_set_pattern(&l, LEDSEQ_DISCONNECTED, 2, 600);
    CHECK(!led_seq_update(&l, 600));                 // still off, not restarted
    led_seq_set_pattern(&l, LEDSEQ_BINDING, 4, 600);
    CHECK(led_seq_update(&l, 600));                  // new pattern starts on
}

static void test_catches_up_after_long_gap(void)
{
    led_seq_t l;
    led_seq_init(&l);
    led_seq_set_pattern(&l, LEDSEQ_DISCONNECTED, 2, 0);
    // 10.25 periods later: 250 ms into an on phase
    CHECK(led_seq_update(&l, 10250));
    // 10.75 periods later: in off phase
    CHECK(!led_seq_update(&l, 10750));
}

int main(void)
{
    RUN(test_solid);
    RUN(test_disconnected_500_500);
    RUN(test_binding_double_blink);
    RUN(test_wifi_fast_flicker);
    RUN(test_reapplying_same_pattern_does_not_restart);
    RUN(test_catches_up_after_long_gap);
    TEST_MAIN_END();
}
