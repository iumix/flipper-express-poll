/**
 * @file ksx6924.h
 * @brief Read-only KS X 6924 (T-Money) balance read.
 *
 * Apple Wallet presents T-Money as a GenericA pass: it answers plain NFC-A polling
 * without ECP, and counts a read as successful after SELECT plus one successful
 * protocol command. This sends exactly two APDUs, SELECT of the T-Money AID and
 * GET BALANCE, then deselects. Nothing else is parsed or kept.
 *
 * Pure C: no Flipper dependencies, shared with the host unit tests.
 */
#pragma once

#include "iso_dep.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KSX6924_CURRENCY "KRW"

typedef enum {
    Ksx6924NotAttempted,
    Ksx6924Ok,
    Ksx6924NoRats, /**< RATS not answered: no ISO-DEP. */
    Ksx6924Unsupported, /**< ATS malformed or beyond the app's bounds. */
    Ksx6924Timeout,
    Ksx6924ProtocolError,
    Ksx6924TransportError,
    Ksx6924NoApplication, /**< SELECT answered "file not found". */
    Ksx6924SelectRejected,
    Ksx6924ReadRejected,
    Ksx6924InvalidBalance, /**< GET BALANCE reply malformed or implausible. */
    Ksx6924Cancelled,
} Ksx6924Result;

/** Decode a GET BALANCE body into won. False if malformed or implausible. */
bool ksx6924_parse_balance(const uint8_t* data, size_t len, int32_t* balance);

/**
 * Run RATS, SELECT T-Money, GET BALANCE and DESELECT.
 *
 * @param status_word last status word received (0 if none).
 */
Ksx6924Result ksx6924_read_balance(
    IsoDepExchange exchange,
    IsoDepWait wait,
    void* context,
    int32_t* balance,
    uint16_t* status_word);
