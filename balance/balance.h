/**
 * @file balance.h
 * @brief Registry of read-only FeliCa balance decoders.
 *
 * Pure C, shared with host unit tests. To support another system, add a
 * decoder file and one entry to the table in balance.c. Decoding rules must
 * come from a verified reference (see AGENTS.md); never guess layouts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BALANCE_BLOCK_LEN 16U
#define BALANCE_STR_SIZE  24U

typedef struct {
    uint16_t system_code; /**< System the decoder applies to. */
    const char* name; /**< Short display name. */
    uint16_t service_code; /**< Read-only, no-authentication service holding the balance. */
    uint8_t block_number; /**< Block within the service. */
    const char* currency; /**< ISO 4217 code. */
    uint8_t decimals; /**< Fraction digits of the decoded value. */
    /** Decode a 16-byte block into a value in minor units. False if implausible. */
    bool (*decode)(const uint8_t block[BALANCE_BLOCK_LEN], int32_t* value);
} BalanceDecoder;

/** Decoder for a system code, or NULL when no verified layout is known. */
const BalanceDecoder* balance_decoder_find(uint16_t system_code);

/** Format a value as e.g. "1234 JPY" or "-12.3 HKD". */
void balance_format(const BalanceDecoder* decoder, int32_t value, char* out, size_t out_size);

/* Individual decoders (balance_suica.c, balance_octopus.c). */
bool balance_suica_decode(const uint8_t block[BALANCE_BLOCK_LEN], int32_t* value);
bool balance_octopus_decode(const uint8_t block[BALANCE_BLOCK_LEN], int32_t* value);
