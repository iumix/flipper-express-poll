/**
 * @file iso_dep.h
 * @brief Minimal bounded ISO-DEP (ISO 14443-4) poller layer.
 *
 * Shared by the selection-only EMV probe and the read-only KS X 6924 read. It only
 * does RATS, I-block APDU exchange with response chaining and WTX, and DESELECT.
 * Outbound chaining, CID/NAD and retransmission are unsupported.
 *
 * Pure C: no Flipper dependencies, shared with the host unit tests.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ISO_DEP_FRAME_MAX    256U
#define ISO_DEP_RESPONSE_MAX 512U
#define ISO_DEP_MAX_FWT_FC   4068000U /* 300 ms; keeps Stop responsive. */
/* Largest APDU sent; each one must also fit the card's FSC with PCB + CRC_A. */
#define ISO_DEP_APDU_MAX     (32U - 3U)

typedef enum {
    IsoDepOk,
    IsoDepUnsupported, /**< ATS malformed, guard time too long, or APDU exceeds FSC. */
    IsoDepTimeout,
    IsoDepProtocolError,
    IsoDepTransportError,
    IsoDepCancelled,
} IsoDepResult;

/* CRC-free frames, at most ISO_DEP_FRAME_MAX bytes; transport checks cancellation
 * before each exchange. */
typedef IsoDepResult (*IsoDepExchange)(
    void* context,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t* rx_len,
    uint32_t fwt_fc);
typedef bool (*IsoDepWait)(void* context, uint32_t delay_fc);

typedef struct {
    IsoDepExchange exchange;
    void* context;
    uint32_t fwt; /**< From the ATS, capped at ISO_DEP_MAX_FWT_FC. */
    uint16_t fsc; /**< Card's maximum frame size, from the ATS. */
    uint8_t block;
    bool active; /**< RATS was answered, so the card must be deselected. */
} IsoDep;

/** Send RATS (FSD 256, no CID), validate the ATS and wait SFGT. */
IsoDepResult
    iso_dep_activate(IsoDep* dep, IsoDepExchange exchange, IsoDepWait wait, void* context);

/**
 * Exchange one APDU. IsoDepOk means a complete response arrived; the caller checks
 * @p status_word. The body (without SW) is written to @p response.
 */
IsoDepResult iso_dep_apdu(
    IsoDep* dep,
    const uint8_t* apdu,
    size_t apdu_len,
    uint8_t response[ISO_DEP_RESPONSE_MAX],
    size_t* response_len,
    uint16_t* status_word);

/** End ISO-DEP so later WUPA presence checks can activate the card again. */
void iso_dep_deselect(IsoDep* dep);
