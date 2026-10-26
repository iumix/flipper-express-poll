/**
 * @file express_poll_worker.h
 * @brief Background polling (FeliCa, ECP, T-Money) and read-only balance retrieval.
 *
 * Runs on the firmware's NfcWorker thread via the raw Nfc transport. The GUI
 * thread starts/stops it and reads snapshots of its status.
 */
#pragma once

#include "../balance/balance.h"
#include "../nfca/emv_select.h"
#include "../nfca/ksx6924.h"
#include "../felica/felica_frames.h"

#include <lib/nfc/protocols/iso14443_3a/iso14443_3a.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct ExpressPollWorker ExpressPollWorker;

typedef enum {
    ExpressPollTechFelica, /**< NFC-F Polling: Express Transit (Suica, Octopus). */
    ExpressPollTechEcp, /**< NFC-A ECP broadcast: Express payment / open-loop transit. */
    ExpressPollTechTmoney, /**< Plain NFC-A without ECP: T-Money (Wallet GenericA). */
} ExpressPollTech;

typedef enum {
    ExpressPollPhaseIdle,
    ExpressPollPhaseSearching, /**< Sending Polling commands, nothing answering. */
    ExpressPollPhaseReading, /**< Card found, reading balance. */
    ExpressPollPhasePresent, /**< Card handled, waiting for removal. */
} ExpressPollPhase;

typedef enum {
    ExpressPollResultNone,
    ExpressPollResultSuccess, /**< Valid Polling response received. */
    ExpressPollResultTimeout, /**< No response within the frame wait time. */
    ExpressPollResultCrcError, /**< Response received with bad CRC (often a collision). */
    ExpressPollResultFrameError, /**< Response with valid CRC but malformed content. */
    ExpressPollResultTransportError, /**< NFC hardware/transport error. */
} ExpressPollResult;

typedef enum {
    ExpressPollBalanceNotRead, /**< Not attempted yet. */
    ExpressPollBalanceDisabled, /**< Balance reading switched off in settings. */
    ExpressPollBalanceUnsupported, /**< No verified decoder for the detected system. */
    ExpressPollBalanceServiceMissing, /**< Card says the balance service does not exist. */
    ExpressPollBalanceNoResponse, /**< Read timed out or failed in transport. */
    ExpressPollBalanceCardError, /**< Card returned non-zero status flags. */
    ExpressPollBalanceInvalid, /**< Malformed response or implausible value. */
    ExpressPollBalanceOk,
} ExpressPollBalanceState;

typedef struct {
    ExpressPollTech tech;
    uint16_t system_code; /**< FeliCa only: code sent in Polling. */
    uint8_t request_code; /**< FeliCa only. */
    uint32_t interval_ms;
    bool read_balance; /**< FeliCa only. */
} ExpressPollWorkerConfig;

typedef struct {
    ExpressPollTech tech; /**< Which technology produced this status. */
    ExpressPollPhase phase;
    uint16_t polled_system_code; /**< FeliCa only. */
    uint32_t poll_count;
    uint32_t detect_count; /**< Increments on each new card/device arrival. */
    ExpressPollResult last_result;

    bool has_card; /**< A card/device was detected (card or nfca holds its data). */
    bool card_present; /**< That card still answers presence checks. */

    /* FeliCa (ExpressPollTechFelica). */
    FelicaPollingResponse card;
    bool identified; /**< identified_system_code is meaningful. */
    uint16_t identified_system_code;
    ExpressPollBalanceState balance_state;
    const BalanceDecoder* decoder;
    int32_t balance;
    uint8_t status1;
    uint8_t status2;

    /* NFC-A (ExpressPollTechEcp, ExpressPollTechTmoney). */
    Iso14443_3aData nfca;
    EmvSelectResult emv_result;
    uint16_t emv_status_word;
    Ksx6924Result tmoney_result; /**< T-Money read; the value is in balance. */
    uint16_t tmoney_status_word;
} ExpressPollStatus;

/** Called from the NFC thread when status changed. Must not block. */
typedef void (*ExpressPollWorkerCallback)(void* context);

ExpressPollWorker* express_poll_worker_alloc(void);

/** Stops the worker if running, then frees it. */
void express_poll_worker_free(ExpressPollWorker* worker);

void express_poll_worker_set_callback(
    ExpressPollWorker* worker,
    ExpressPollWorkerCallback callback,
    void* context);

/** Acquire the NFC hardware and start polling. */
void express_poll_worker_start(ExpressPollWorker* worker, const ExpressPollWorkerConfig* config);

/** Stop polling, wait for the NFC thread to finish and release the hardware. */
void express_poll_worker_stop(ExpressPollWorker* worker);

bool express_poll_worker_is_running(const ExpressPollWorker* worker);

/** Copy the current status. Also re-arms the update callback. */
void express_poll_worker_get_status(ExpressPollWorker* worker, ExpressPollStatus* status);
