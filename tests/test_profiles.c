#include "test.h"
#include "profiles.h"

static profile_store_t s;

static void test_init_is_valid_with_one_default_profile(void)
{
    char name[PROFILE_NAME_LEN];
    config_t c, d;
    profiles_init(&s, NULL);
    CHECK(profiles_is_valid(&s));
    CHECK_EQ(s.active, 0);
    CHECK_EQ(s.slot[0].used, 1);
    for (int n = 1; n < PROFILE_COUNT; n++) CHECK_EQ(s.slot[n].used, 0);
    profiles_name(&s, 0, name);
    CHECK(strcmp(name, "Profile 1") == 0);
    profiles_name(&s, 15, name);
    CHECK(strcmp(name, "Profile 16") == 0);
    profiles_get(&s, 0, &c);
    config_defaults(&d);
    CHECK(memcmp(&c, &d, sizeof(c)) == 0);
    CHECK(sizeof(profile_store_t) <= 4096);
}

static void test_legacy_config_becomes_profile_one(void)
{
    config_t old, c;
    config_defaults(&old);
    old.axis_channel[0] = 4;
    old.failsafe_ms = 150;
    config_finalize(&old);
    profiles_init(&s, &old);
    CHECK(profiles_is_valid(&s));
    profiles_get(&s, 0, &c);
    CHECK_EQ(c.axis_channel[0], 4);
    CHECK_EQ(c.failsafe_ms, 150);
}

static void test_put_get_and_empty_slot_defaults(void)
{
    config_t c, out;
    profiles_init(&s, NULL);
    config_defaults(&c);
    c.axis_channel[2] = 7;
    c.axis_invert = 0x05;
    profiles_put(&s, 9, &c);          // crc of c is stale on purpose; put must refresh it
    profiles_finalize(&s);
    CHECK(profiles_is_valid(&s));
    CHECK_EQ(s.slot[9].used, 1);
    profiles_get(&s, 9, &out);
    CHECK_EQ(out.axis_channel[2], 7);
    CHECK_EQ(out.axis_invert, 0x05);
    CHECK(config_is_valid(&out));
    profiles_get(&s, 3, &out);        // never written
    CHECK_EQ(out.axis_channel[2], 3);
    profiles_get(&s, 99, &out);       // out of range
    CHECK(config_is_valid(&out));
    profiles_put(&s, -1, &c);         // ignored
    profiles_put(&s, PROFILE_COUNT, &c);
    profiles_finalize(&s);
    CHECK(profiles_is_valid(&s));
}

static void test_names_are_cleaned(void)
{
    char name[PROFILE_NAME_LEN];
    CHECK_EQ(profiles_clean_name(name, "  Velocidrone  "), 11);
    CHECK(strcmp(name, "Velocidrone") == 0);
    profiles_clean_name(name, "Lift\"off\\ {x}");
    CHECK(strcmp(name, "Liftoff x") == 0);
    CHECK_EQ(profiles_clean_name(name, "A very long profile name indeed"), 15);
    CHECK(strcmp(name, "A very long pro") == 0);
    CHECK_EQ(profiles_clean_name(name, "\"\"\\"), 0);

    profiles_init(&s, NULL);
    CHECK(profiles_rename(&s, 0, "Tryp (mode 2)"));
    profiles_name(&s, 0, name);
    CHECK(strcmp(name, "Tryp (mode 2)") == 0);
    CHECK(!profiles_rename(&s, 0, "{}"));          // nothing usable left
    CHECK(!profiles_rename(&s, 5, "Empty slot"));  // not in use
    profiles_finalize(&s);
    CHECK(profiles_is_valid(&s));
}

static void test_clear_refuses_the_active_slot(void)
{
    config_t c;
    profiles_init(&s, NULL);
    config_defaults(&c);
    profiles_put(&s, 4, &c);
    CHECK(!profiles_clear(&s, 0));
    CHECK(profiles_clear(&s, 4));
    CHECK_EQ(s.slot[4].used, 0);
    CHECK(!profiles_clear(&s, PROFILE_COUNT));
    profiles_finalize(&s);
    CHECK(profiles_is_valid(&s));
}

static void test_validation_catches_damage(void)
{
    profiles_init(&s, NULL);
    s.slot[0].cfg.axis_channel[0] ^= 1;
    CHECK(!profiles_is_valid(&s));                 // crc
    profiles_init(&s, NULL);
    s.active = 3;                                  // points at an empty slot
    profiles_finalize(&s);
    CHECK(!profiles_is_valid(&s));
    profiles_init(&s, NULL);
    s.slot[0].cfg.failsafe_ms = 5;                 // slot config out of range, store crc refreshed
    profiles_finalize(&s);
    CHECK(!profiles_is_valid(&s));
    profiles_init(&s, NULL);
    memset(s.slot[0].name, 'x', PROFILE_NAME_LEN); // name not terminated
    profiles_finalize(&s);
    CHECK(!profiles_is_valid(&s));
    memset(&s, 0xFF, sizeof(s));                   // erased flash
    CHECK(!profiles_is_valid(&s));
}

int main(void)
{
    RUN(test_init_is_valid_with_one_default_profile);
    RUN(test_legacy_config_becomes_profile_one);
    RUN(test_put_get_and_empty_slot_defaults);
    RUN(test_names_are_cleaned);
    RUN(test_clear_refuses_the_active_slot);
    RUN(test_validation_catches_damage);
    TEST_MAIN_END();
}
