/**
 * @file nfca_poll.h
 * @brief ECP broadcast plus minimal NFC-A (ISO14443-3A) activation.
 *
 * Used by the worker to trigger Apple Pay Express for payment / open-loop
 * transit. It broadcasts the ECP frame (unanswered), then runs a standard
 * NFC-A SENS_REQ + anticollision cascade to detect and identify the card the
 * device presents.
 *
 * Read-only on purpose: activation stops at SAK. No RATS/ISO-DEP, no EMV/PPSE
 * selection, no cardholder data. EMV cards expose no balance to a poller.
 *
 * The firmware's Iso14443_3aPoller is internal and cannot be allocated from an
 * app, so this adapts the exported low-level primitives
 * (nfc_iso14443a_poller_trx_short_frame / _trx_sdd_frame / nfc_poller_trx)
 * and the exported iso14443_crc helpers.
 */
#pragma once

#include <lib/nfc/nfc.h>
#include <lib/nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <toolbox/bit_buffer.h>

typedef enum {
    NfcaPollResultOk, /**< A card answered and was identified. */
    NfcaPollResultTimeout, /**< No card answered the poll. */
    NfcaPollResultCollision, /**< Response with bad CRC/format (often a collision). */
    NfcaPollResultError, /**< Transport or anticollision error. */
} NfcaPollResult;

/**
 * @brief Send the ECP frame on NFC-A.
 *
 * Appends CRC_A and transmits. ECP is not answered, so a timeout is the normal
 * outcome and is reported as NfcaPollResultOk. Other transport errors are
 * reported as NfcaPollResultError.
 *
 * Must only be called inside the Nfc poller callback (field on, poller ready).
 */
NfcaPollResult nfca_poll_send_ecp(
    Nfc* nfc,
    BitBuffer* tx,
    BitBuffer* rx,
    const uint8_t* frame,
    size_t frame_len);

/**
 * @brief Run NFC-A activation (SENS_REQ + anticollision) and fill @p data.
 *
 * @return NfcaPollResultOk with UID/ATQA/SAK on success; NfcaPollResultTimeout
 *         when nothing answers SENS_REQ; otherwise a collision/error code.
 *
 * Must only be called inside the Nfc poller callback.
 */
NfcaPollResult nfca_poll_activate(Nfc* nfc, BitBuffer* tx, BitBuffer* rx, Iso14443_3aData* data);
