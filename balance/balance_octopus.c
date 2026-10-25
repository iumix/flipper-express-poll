#include "balance.h"

/*
 * Octopus (system 0x8008), purse service 0x0117 block 0.
 *
 * Layout: bytes 0..3 hold a
 * big-endian value in units of HK$0.1 with a fixed offset of 500 (HK$50)
 * for cards issued since 2017-10-01, which includes Apple Pay Octopus. Older
 * physical cards used an offset of 350 and are not distinguished here.
 */
#define OCTOPUS_BALANCE_OFFSET 500

bool balance_octopus_decode(const uint8_t block[BALANCE_BLOCK_LEN], int32_t* value) {
    if(!block || !value) return false;

    const uint32_t raw = ((uint32_t)block[0] << 24) | ((uint32_t)block[1] << 16) |
                         ((uint32_t)block[2] << 8) | (uint32_t)block[3];
    /* Real purses are far below 2^31; reject anything that would overflow. */
    if(raw > (uint32_t)INT32_MAX) return false;

    *value = (int32_t)raw - OCTOPUS_BALANCE_OFFSET;
    return true;
}
