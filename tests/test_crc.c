#include "test.h"
#include "crc.h"

static void test_crc8_known_vectors(void)
{
    // ExpressLRS bootloader.py: INIT_SEQ [0xEC,0x04,0x32,'b','l'], CRC over [0x32,'b','l']
    uint8_t bl[] = { 0x32, 'b', 'l' };
    uint8_t bd[] = { 0x32, 'b', 'd' };
    // Values computed with the DVB-S2 polynomial 0xD5 (checked with a Python reference impl).
    CHECK_EQ(crc8_dvb_s2(bl, 3), 0x0A);
    CHECK_EQ(crc8_dvb_s2(bd, 3), 0x23);

    // Single-byte properties
    uint8_t zero = 0;
    CHECK_EQ(crc8_dvb_s2(&zero, 1), 0x00);
    uint8_t one = 0x01;
    CHECK_EQ(crc8_dvb_s2(&one, 1), 0xD5);
}

static void test_crc32_known_vector(void)
{
    // "123456789" -> 0xCBF43926 is the standard IEEE check value.
    const char *s = "123456789";
    CHECK_EQ(crc32_ieee((const uint8_t *)s, 9), 0xCBF43926u);
    CHECK_EQ(crc32_ieee((const uint8_t *)"", 0), 0u);
}

int main(void)
{
    RUN(test_crc8_known_vectors);
    RUN(test_crc32_known_vector);
    TEST_MAIN_END();
}
