// CRC helpers. Hardware independent.
#pragma once
#include <stdint.h>
#include <stddef.h>

// CRC-8 DVB-S2 (poly 0xD5), as used by CRSF for frame CRCs.
uint8_t crc8_dvb_s2(const uint8_t *data, size_t len);

// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320), used for the config block.
uint32_t crc32_ieee(const uint8_t *data, size_t len);
