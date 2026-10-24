#include "balance.h"

#include <stdio.h>

static const BalanceDecoder balance_decoders[] = {
    {
        .system_code = 0x0003,
        .name = "Suica/ICOCA/PASMO",
        /* Attribute 0x0B: random service, read-only, no authentication. */
        .service_code = 0x008B,
        .block_number = 0,
        .currency = "JPY",
        .decimals = 0,
        .decode = balance_suica_decode,
    },
    {
        .system_code = 0x8008,
        .name = "Octopus",
        /* Attribute 0x17: purse service, read-only, no authentication. */
        .service_code = 0x0117,
        .block_number = 0,
        .currency = "HKD",
        .decimals = 1,
        .decode = balance_octopus_decode,
    },
};

const BalanceDecoder* balance_decoder_find(uint16_t system_code) {
    for(size_t i = 0; i < sizeof(balance_decoders) / sizeof(balance_decoders[0]); i++) {
        if(balance_decoders[i].system_code == system_code) return &balance_decoders[i];
    }
    return NULL;
}

void balance_format(const BalanceDecoder* decoder, int32_t value, char* out, size_t out_size) {
    if(!out || out_size == 0) return;
    if(!decoder) {
        out[0] = '\0';
        return;
    }

    const char* sign = value < 0 ? "-" : "";
    uint32_t magnitude = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;

    if(decoder->decimals == 0) {
        snprintf(out, out_size, "%s%lu %s", sign, (unsigned long)magnitude, decoder->currency);
        return;
    }

    uint32_t scale = 1;
    for(uint8_t i = 0; i < decoder->decimals; i++)
        scale *= 10;
    snprintf(
        out,
        out_size,
        "%s%lu.%0*lu %s",
        sign,
        (unsigned long)(magnitude / scale),
        (int)decoder->decimals,
        (unsigned long)(magnitude % scale),
        decoder->currency);
}
