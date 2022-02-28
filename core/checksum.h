#ifndef __CORE_CHECKSUM_H__
#define __CORE_CHECKSUM_H__

#include <cstdint>

uint64_t crc64(uint64_t seed, const uint8_t *data, uint64_t len);
#endif

