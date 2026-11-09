#include "test.h"
#include "mapping.h"

static void test_defaults_valid_and_aetr(void)
{
    config_t c;
    config_defaults(&c);
    CHECK(config_is_valid(&c));
    CHECK_EQ(c.axis_channel[AXIS_X], 1);
    CHECK_EQ(c.axis_channel[AXIS_Y], 2);
    CHECK_EQ(c.axis_channel[AXIS_Z], 3);
    CHECK_EQ(c.axis_channel[AXIS_RX], 4);
    CHECK_EQ(c.throttle_axis, AXIS_Z);
    CHECK_EQ(c.rx_power_on_boot, 1);
    CHECK_EQ(c.failsafe_ms, 100);
    // buttons 0..2 come from ch5 low/mid/high, 33..35 from ch16
    CHECK_EQ(c.button_channel[0], 5);
    CHECK_EQ(c.button_level[0], BTN_LOW);
    CHECK_EQ(c.button_level[1], BTN_MID);
    CHECK_EQ(c.button_level[2], BTN_HIGH);
    CHECK_EQ(c.button_channel[35], 16);
    CHECK_EQ(c.button_level[35], BTN_HIGH);
}

static void test_config_crc_detects_change(void)
{
    config_t c;
    config_defaults(&c);
    c.axis_channel[0] = 2;
    CHECK(!config_is_valid(&c));
    config_finalize(&c);
    CHECK(config_is_valid(&c));
    c.magic = 0;
    CHECK(!config_is_valid(&c));
}

static void test_config_rejects_out_of_range(void)
{
    config_t c;
    config_defaults(&c);
    c.axis_channel[0] = 17;
    config_finalize(&c);
    CHECK(!config_is_valid(&c));

    config_defaults(&c);
    c.throttle_axis = 8;
    config_finalize(&c);
    CHECK(!config_is_valid(&c));

    config_defaults(&c);
    c.throttle_axis = 0xFF;
    config_finalize(&c);
    CHECK(config_is_valid(&c));

    // failsafe_ms must be sane: 0 would keep the device in failsafe forever.
    config_defaults(&c);
    c.failsafe_ms = 0;
    config_finalize(&c);
    CHECK(!config_is_valid(&c));
    c.failsafe_ms = 251;
    config_finalize(&c);
    CHECK(!config_is_valid(&c));
    c.failsafe_ms = 20;
    config_finalize(&c);
    CHECK(config_is_valid(&c));
}

static void test_axis_scaling(void)
{
    CHECK_EQ(mapping_channel_to_axis(992, false), 0);
    CHECK_EQ(mapping_channel_to_axis(1811, false), 32767);
    CHECK_EQ(mapping_channel_to_axis(172, false), -32767);   // clamped
    CHECK_EQ(mapping_channel_to_axis(0, false), -32767);
    CHECK_EQ(mapping_channel_to_axis(2047, false), 32767);
    CHECK_EQ(mapping_channel_to_axis(1811, true), -32767);
    CHECK_EQ(mapping_channel_to_axis(172, true), 32767);
    // Monotonic, and 11-bit resolution preserved (adjacent values differ).
    CHECK(mapping_channel_to_axis(1000, false) > mapping_channel_to_axis(999, false));
    CHECK(mapping_channel_to_axis(993, false) > 0);
}

static void test_button_levels(void)
{
    // us = (ch-992)*5/8 + 1500. 1300us -> ch 672, 1700us -> ch 1312.
    CHECK(mapping_button_pressed(172, BTN_LOW));
    CHECK(!mapping_button_pressed(172, BTN_MID));
    CHECK(!mapping_button_pressed(172, BTN_HIGH));

    CHECK(!mapping_button_pressed(992, BTN_LOW));
    CHECK(mapping_button_pressed(992, BTN_MID));
    CHECK(!mapping_button_pressed(992, BTN_HIGH));

    CHECK(!mapping_button_pressed(1811, BTN_LOW));
    CHECK(!mapping_button_pressed(1811, BTN_MID));
    CHECK(mapping_button_pressed(1811, BTN_HIGH));

    CHECK(!mapping_button_pressed(1811, BTN_NONE));
}

static void test_build_report_defaults(void)
{
    config_t c;
    config_defaults(&c);
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = 992;
    ch[0] = 1811;     // roll full right -> X max
    ch[2] = 172;      // throttle low    -> Z min
    ch[4] = 1811;     // ch5 high        -> button 2 (index) set
    ch[15] = 172;     // ch16 low        -> button 33 set

    hid_report_t r;
    mapping_build_report(&c, ch, &r);
    CHECK_EQ(r.axes[AXIS_X], 32767);
    CHECK_EQ(r.axes[AXIS_Y], 0);
    CHECK_EQ(r.axes[AXIS_Z], -32767);
    CHECK_EQ(r.axes[AXIS_RY], 32767);   // ch5 also on Ry by default

    // ch5 high -> button index 2; ch6..15 at mid -> their mid buttons (index 4,7,...,31)
    CHECK(r.buttons[0] & (1u << 2));
    CHECK(!(r.buttons[0] & (1u << 0)));
    CHECK(!(r.buttons[0] & (1u << 1)));
    CHECK(r.buttons[0] & (1u << 4));    // ch6 mid
    // ch16 low -> button 33 -> byte 4 bit 1
    CHECK(r.buttons[4] & (1u << 1));
    CHECK(!(r.buttons[4] & (1u << 2)));
    CHECK(!(r.buttons[4] & (1u << 3)));
    // padding bits must stay clear
    CHECK_EQ(r.buttons[4] & 0xF0, 0);
    CHECK_EQ(sizeof(hid_report_t), 16 + 5);
}

static void test_unmapped_axis_centres(void)
{
    config_t c;
    config_defaults(&c);
    c.axis_channel[AXIS_DIAL] = 0;
    uint16_t ch[16];
    for (int i = 0; i < 16; i++) ch[i] = 1811;
    hid_report_t r;
    mapping_build_report(&c, ch, &r);
    CHECK_EQ(r.axes[AXIS_DIAL], 0);
    CHECK_EQ(r.axes[AXIS_SLIDER], 32767);
}

static void test_failsafe_report(void)
{
    config_t c;
    config_defaults(&c);
    hid_report_t r;
    mapping_failsafe_report(&c, &r);
    for (int a = 0; a < NUM_AXES; a++) {
        if (a == AXIS_Z) CHECK_EQ(r.axes[a], -32767);
        else CHECK_EQ(r.axes[a], 0);
    }
    for (int i = 0; i < NUM_BUTTON_BYTES; i++) CHECK_EQ(r.buttons[i], 0);

    // Inverted throttle axis -> failsafe drives it to +max (which is "low").
    c.axis_invert |= (1u << AXIS_Z);
    mapping_failsafe_report(&c, &r);
    CHECK_EQ(r.axes[AXIS_Z], 32767);

    c.throttle_axis = 0xFF;
    mapping_failsafe_report(&c, &r);
    CHECK_EQ(r.axes[AXIS_Z], 0);
}

int main(void)
{
    RUN(test_defaults_valid_and_aetr);
    RUN(test_config_crc_detects_change);
    RUN(test_config_rejects_out_of_range);
    RUN(test_axis_scaling);
    RUN(test_button_levels);
    RUN(test_build_report_defaults);
    RUN(test_unmapped_axis_centres);
    RUN(test_failsafe_report);
    TEST_MAIN_END();
}
