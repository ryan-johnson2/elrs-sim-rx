#include "test.h"
#include "button.h"

static void test_short_press(void)
{
    button_t b;
    button_init(&b, 20, 1500);
    CHECK_EQ(button_update(&b, false, 0), BUTTON_EVENT_NONE);
    CHECK_EQ(button_update(&b, true, 100), BUTTON_EVENT_NONE);     // raw press
    CHECK_EQ(button_update(&b, true, 110), BUTTON_EVENT_NONE);     // debouncing
    CHECK_EQ(button_update(&b, true, 120), BUTTON_EVENT_NONE);     // stable pressed
    CHECK(button_is_pressed(&b));
    CHECK_EQ(button_update(&b, false, 400), BUTTON_EVENT_NONE);    // raw release
    CHECK_EQ(button_update(&b, false, 420), BUTTON_EVENT_SHORT);   // stable release -> SHORT
    CHECK(!button_is_pressed(&b));
}

static void test_long_press_fires_once_while_held(void)
{
    button_t b;
    button_init(&b, 20, 1500);
    button_update(&b, true, 0);
    CHECK_EQ(button_update(&b, true, 20), BUTTON_EVENT_NONE);
    CHECK_EQ(button_update(&b, true, 1519), BUTTON_EVENT_NONE);
    CHECK_EQ(button_update(&b, true, 1520), BUTTON_EVENT_LONG);
    CHECK_EQ(button_update(&b, true, 3000), BUTTON_EVENT_NONE);    // no repeat
    // Release after a long press must not also produce SHORT.
    button_update(&b, false, 3100);
    CHECK_EQ(button_update(&b, false, 3120), BUTTON_EVENT_NONE);
}

static void test_bounce_is_ignored(void)
{
    button_t b;
    button_init(&b, 20, 1500);
    // Contact chatter for 15 ms, never stable.
    CHECK_EQ(button_update(&b, true, 0), BUTTON_EVENT_NONE);
    CHECK_EQ(button_update(&b, false, 5), BUTTON_EVENT_NONE);
    CHECK_EQ(button_update(&b, true, 10), BUTTON_EVENT_NONE);
    CHECK_EQ(button_update(&b, false, 15), BUTTON_EVENT_NONE);
    CHECK_EQ(button_update(&b, false, 100), BUTTON_EVENT_NONE);
    CHECK(!button_is_pressed(&b));
}

static void test_second_press_after_long(void)
{
    button_t b;
    button_init(&b, 20, 1500);
    button_update(&b, true, 0);
    button_update(&b, true, 20);
    CHECK_EQ(button_update(&b, true, 1520), BUTTON_EVENT_LONG);
    button_update(&b, false, 2000);
    button_update(&b, false, 2020);
    button_update(&b, true, 3000);
    button_update(&b, true, 3020);
    button_update(&b, false, 3200);
    CHECK_EQ(button_update(&b, false, 3220), BUTTON_EVENT_SHORT);
}

int main(void)
{
    RUN(test_short_press);
    RUN(test_long_press_fires_once_while_held);
    RUN(test_bounce_is_ignored);
    RUN(test_second_press_after_long);
    TEST_MAIN_END();
}
