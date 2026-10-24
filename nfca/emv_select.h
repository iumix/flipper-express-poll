#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    EmvSelectNotAttempted,
    EmvSelectOk,
    EmvSelectUnsupported,
    EmvSelectTimeout,
    EmvSelectProtocolError,
    EmvSelectTransportError,
    EmvSelectNoApplication,
    EmvSelectRejected,
    EmvSelectCancelled,
} EmvSelectResult;

/* CRC-free frames, at most 256 bytes; transport checks cancellation before each exchange. */
typedef EmvSelectResult (*EmvSelectExchange)(
    void* context,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t* rx_len,
    uint32_t fwt_fc);
typedef bool (*EmvSelectWait)(void* context, uint32_t delay_fc);

/* Only RATS, SELECT PPSE, SELECT advertised AID, and ISO-DEP flow control/deselect. */
EmvSelectResult emv_select_run(
    EmvSelectExchange exchange,
    EmvSelectWait wait,
    void* context,
    uint16_t* status_word);
