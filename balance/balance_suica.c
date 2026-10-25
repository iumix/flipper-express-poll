#include "balance.h"

/*
 * Suica / ICOCA / PASMO (system 0x0003), service 0x008B block 0.
 *
 * Layout: the remaining stored-fare
 * balance is a little-endian uint16 at bytes 11..12, in yen.
 */
bool balance_suica_decode(const uint8_t block[BALANCE_BLOCK_LEN], int32_t* value) {
    if(!block || !value) return false;

    *value = (int32_t)((uint16_t)block[11] | ((uint16_t)block[12] << 8));
    return true;
}
