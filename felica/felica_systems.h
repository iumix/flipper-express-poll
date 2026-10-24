/**
 * @file felica_systems.h
 * @brief FeliCa system code names and hex formatting/parsing helpers.
 *
 * Pure C, shared with host unit tests.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FELICA_SYSTEM_CODE_SUICA    0x0003U
#define FELICA_SYSTEM_CODE_OCTOPUS  0x8008U
#define FELICA_SYSTEM_CODE_WILDCARD 0xFFFFU

/** Buffer size for "0xABCD" plus terminator. */
#define FELICA_SYSTEM_CODE_STR_SIZE 7U

/** Human-readable name for a known system code, or NULL. */
const char* felica_system_name(uint16_t system_code);

/** True if either byte is 0xFF, i.e. the code matches more than one system. */
bool felica_system_code_is_wildcard(uint16_t system_code);

/** Format as "0x%04X". */
void felica_system_code_format(uint16_t system_code, char out[FELICA_SYSTEM_CODE_STR_SIZE]);

/**
 * Parse 1-4 hex digits with an optional "0x"/"0X" prefix and surrounding spaces.
 * Returns false on empty input, non-hex characters or values above 0xFFFF.
 */
bool felica_system_code_parse(const char* text, uint16_t* system_code);
