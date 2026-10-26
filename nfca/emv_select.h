#pragma once

#include "iso_dep.h"

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

/* Only RATS, SELECT PPSE, SELECT advertised AID, and ISO-DEP flow control/deselect. */
EmvSelectResult
    emv_select_run(IsoDepExchange exchange, IsoDepWait wait, void* context, uint16_t* status_word);
