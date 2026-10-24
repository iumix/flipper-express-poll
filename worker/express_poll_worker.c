#include "express_poll_worker.h"

#include "../felica/felica_systems.h"

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/helpers/felica_crc.h>
#include <nfc/protocols/felica/felica.h>

#include <string.h>

#define TAG "ExpressPollWorker"

#define WORKER_BUFFER_SIZE 256U

/* Same Polling frame wait time as the firmware FeliCa poller (~14.7 ms). */
#define WORKER_POLLING_FWT_FC 200000U
/* Upper bound for PMm-derived timeouts (~1 s), protects against odd PMm values. */
#define WORKER_MAX_FWT_FC     13560000U

#define WORKER_WAIT_SLICE_MS            10U
/* Consecutive missed presence polls before a card counts as removed. */
#define WORKER_REMOVAL_MISSES           3U
/* Presence checks never run faster than this, to give phones some air time. */
#define WORKER_PRESENCE_MIN_INTERVAL_MS 250U

struct ExpressPollWorker {
    Nfc* nfc;
    BitBuffer* tx;
    BitBuffer* rx;
    FuriMutex* mutex;

    ExpressPollWorkerConfig config;
    ExpressPollStatus status; /* Guarded by mutex. */
    uint8_t misses;

    volatile bool running;
    volatile bool stop_requested;
    volatile bool update_pending;

    ExpressPollWorkerCallback callback;
    void* context;
};

typedef enum {
    WorkerTrxOk,
    WorkerTrxTimeout,
    WorkerTrxCrc,
    WorkerTrxTransport,
} WorkerTrx;

/* --- Status helpers ------------------------------------------------------ */

static void worker_lock(ExpressPollWorker* worker) {
    furi_check(furi_mutex_acquire(worker->mutex, FuriWaitForever) == FuriStatusOk);
}

static void worker_unlock(ExpressPollWorker* worker) {
    furi_check(furi_mutex_release(worker->mutex) == FuriStatusOk);
}

/*
 * Notify the GUI. At most one update is outstanding, so the NFC thread never
 * blocks on a full GUI queue (the GUI thread may be joining us in stop()).
 */
static void worker_notify(ExpressPollWorker* worker) {
    if(worker->stop_requested || !worker->callback || worker->update_pending) return;
    worker->update_pending = true;
    worker->callback(worker->context);
}

static void worker_wait(ExpressPollWorker* worker, uint32_t ms) {
    const uint32_t start = furi_get_tick();
    const uint32_t ticks = furi_ms_to_ticks(ms);
    while(!worker->stop_requested && (furi_get_tick() - start) < ticks) {
        furi_delay_ms(WORKER_WAIT_SLICE_MS);
    }
}

static uint32_t worker_fwt_from_pmm(const uint8_t* pmm, size_t index) {
    /* PMm gives the card's own maximum; add 50% for the transport's latency. */
    uint32_t fwt = felica_frame_pmm_timeout_fc(pmm, index, 1);
    fwt += fwt / 2;
    if(fwt < WORKER_POLLING_FWT_FC) fwt = WORKER_POLLING_FWT_FC;
    if(fwt > WORKER_MAX_FWT_FC) fwt = WORKER_MAX_FWT_FC;
    return fwt;
}

/* --- Transport ------------------------------------------------------------ */

static WorkerTrx
    worker_trx(ExpressPollWorker* worker, const uint8_t* frame, size_t len, uint32_t fwt) {
    bit_buffer_copy_bytes(worker->tx, frame, len);
    felica_crc_append(worker->tx);
    bit_buffer_reset(worker->rx);

    NfcError error = nfc_poller_trx(worker->nfc, worker->tx, worker->rx, fwt);
    if(error == NfcErrorTimeout) return WorkerTrxTimeout;
    if(error != NfcErrorNone) {
        FURI_LOG_D(TAG, "trx error %d", error);
        return WorkerTrxTransport;
    }
    if(!felica_crc_check(worker->rx)) return WorkerTrxCrc;
    felica_crc_trim(worker->rx);
    return WorkerTrxOk;
}

static ExpressPollResult worker_poll_once(ExpressPollWorker* worker, FelicaPollingResponse* resp) {
    uint8_t frame[FELICA_FRAME_POLLING_LEN];
    size_t len = felica_frame_build_polling(
        frame,
        sizeof(frame),
        worker->config.system_code,
        worker->config.request_code,
        FELICA_FRAME_TSN_1);

    switch(worker_trx(worker, frame, len, WORKER_POLLING_FWT_FC)) {
    case WorkerTrxTimeout:
        return ExpressPollResultTimeout;
    case WorkerTrxCrc:
        return ExpressPollResultCrcError;
    case WorkerTrxTransport:
        return ExpressPollResultTransportError;
    case WorkerTrxOk:
        break;
    }

    const FelicaFrameResult parsed = felica_frame_parse_polling(
        bit_buffer_get_data(worker->rx), bit_buffer_get_size_bytes(worker->rx), resp);
    return parsed == FelicaFrameOk ? ExpressPollResultSuccess : ExpressPollResultFrameError;
}

/* --- Balance -------------------------------------------------------------- */

typedef struct {
    ExpressPollBalanceState state;
    int32_t value;
    uint8_t status1;
    uint8_t status2;
} WorkerBalance;

/* Returns false only when the card positively reports the service as absent. */
static bool worker_service_exists(
    ExpressPollWorker* worker,
    const FelicaPollingResponse* card,
    uint16_t service_code) {
    uint8_t frame[FELICA_FRAME_REQUEST_SERVICE_LEN];
    size_t len = felica_frame_build_request_service(frame, sizeof(frame), card->idm, service_code);
    uint32_t fwt = worker_fwt_from_pmm(card->pmm, FELICA_FRAME_PMM_REQUEST_SERVICE);

    if(worker_trx(worker, frame, len, fwt) != WorkerTrxOk) {
        /* Not every implementation answers Request Service; let Read decide. */
        FURI_LOG_D(TAG, "Request Service failed, trying read anyway");
        return true;
    }

    uint16_t key_version;
    if(felica_frame_parse_request_service(
           bit_buffer_get_data(worker->rx),
           bit_buffer_get_size_bytes(worker->rx),
           card->idm,
           &key_version) != FelicaFrameOk) {
        return true;
    }
    return key_version != FELICA_FRAME_KEY_VERSION_MISSING;
}

static WorkerBalance worker_read_balance(
    ExpressPollWorker* worker,
    const FelicaPollingResponse* card,
    const BalanceDecoder* decoder) {
    WorkerBalance result = {.state = ExpressPollBalanceInvalid};

    if(!worker_service_exists(worker, card, decoder->service_code)) {
        result.state = ExpressPollBalanceServiceMissing;
        return result;
    }

    uint8_t frame[FELICA_FRAME_READ_ONE_BLOCK_LEN];
    size_t len = felica_frame_build_read_one_block(
        frame, sizeof(frame), card->idm, decoder->service_code, decoder->block_number);
    uint32_t fwt = worker_fwt_from_pmm(card->pmm, FELICA_FRAME_PMM_READ);

    WorkerTrx trx = worker_trx(worker, frame, len, fwt);
    if(trx == WorkerTrxTimeout || trx == WorkerTrxTransport) {
        result.state = ExpressPollBalanceNoResponse;
        return result;
    }
    if(trx == WorkerTrxCrc) return result;

    uint8_t block[FELICA_FRAME_BLOCK_LEN];
    FelicaFrameResult parsed = felica_frame_parse_read_one_block(
        bit_buffer_get_data(worker->rx),
        bit_buffer_get_size_bytes(worker->rx),
        card->idm,
        &result.status1,
        &result.status2,
        block);

    if(parsed == FelicaFrameErrorStatus) {
        result.state = ExpressPollBalanceCardError;
    } else if(parsed == FelicaFrameOk && decoder->decode(block, &result.value)) {
        result.state = ExpressPollBalanceOk;
    }
    return result;
}

/* --- State machine -------------------------------------------------------- */

static void worker_handle_new_card(ExpressPollWorker* worker, const FelicaPollingResponse* card) {
    /* Prefer the system code the card reported; fall back to a specific polled code. */
    bool identified = card->has_system_code ||
                      !felica_system_code_is_wildcard(worker->config.system_code);
    uint16_t system_code = card->has_system_code ? card->system_code : worker->config.system_code;
    const BalanceDecoder* decoder = identified ? balance_decoder_find(system_code) : NULL;

    ExpressPollBalanceState initial = ExpressPollBalanceNotRead;
    if(!worker->config.read_balance) {
        initial = ExpressPollBalanceDisabled;
    } else if(!decoder) {
        initial = ExpressPollBalanceUnsupported;
    }

    worker_lock(worker);
    worker->status.phase = initial == ExpressPollBalanceNotRead ? ExpressPollPhaseReading :
                                                                  ExpressPollPhasePresent;
    worker->status.detect_count++;
    worker->status.has_card = true;
    worker->status.card_present = true;
    worker->status.card = *card;
    worker->status.identified = identified;
    worker->status.identified_system_code = system_code;
    worker->status.decoder = decoder;
    worker->status.balance_state = initial;
    worker->status.balance = 0;
    worker->status.status1 = 0;
    worker->status.status2 = 0;
    worker_unlock(worker);
    worker_notify(worker);

    if(initial == ExpressPollBalanceNotRead) {
        WorkerBalance balance = worker_read_balance(worker, card, decoder);
        worker_lock(worker);
        worker->status.phase = ExpressPollPhasePresent;
        worker->status.balance_state = balance.state;
        worker->status.balance = balance.value;
        worker->status.status1 = balance.status1;
        worker->status.status2 = balance.status2;
        worker_unlock(worker);
        worker_notify(worker);
    }

    worker->misses = 0;
}

static void worker_step(ExpressPollWorker* worker) {
    worker_lock(worker);
    const ExpressPollPhase phase = worker->status.phase;
    uint8_t idm[FELICA_FRAME_IDM_LEN];
    memcpy(idm, worker->status.card.idm, sizeof(idm));
    worker_unlock(worker);

    FelicaPollingResponse resp;
    ExpressPollResult result = worker_poll_once(worker, &resp);

    worker_lock(worker);
    worker->status.poll_count++;
    worker->status.last_result = result;
    worker_unlock(worker);

    if(result == ExpressPollResultSuccess) {
        bool same_card = phase == ExpressPollPhasePresent &&
                         memcmp(resp.idm, idm, sizeof(idm)) == 0;
        if(same_card) {
            worker->misses = 0;
            worker_notify(worker);
        } else {
            worker_handle_new_card(worker, &resp);
        }
    } else if(phase == ExpressPollPhasePresent && ++worker->misses >= WORKER_REMOVAL_MISSES) {
        worker_lock(worker);
        worker->status.phase = ExpressPollPhaseSearching;
        worker->status.card_present = false;
        worker_unlock(worker);
        worker_notify(worker);
    } else {
        worker_notify(worker);
    }

    uint32_t interval = worker->config.interval_ms;
    if(phase == ExpressPollPhasePresent && interval < WORKER_PRESENCE_MIN_INTERVAL_MS) {
        interval = WORKER_PRESENCE_MIN_INTERVAL_MS;
    }
    worker_wait(worker, interval);
}

static NfcCommand worker_nfc_callback(NfcEvent event, void* context) {
    ExpressPollWorker* worker = context;

    if(worker->stop_requested) return NfcCommandStop;
    if(event.type == NfcEventTypePollerReady) worker_step(worker);
    return worker->stop_requested ? NfcCommandStop : NfcCommandContinue;
}

/* --- Public API ----------------------------------------------------------- */

ExpressPollWorker* express_poll_worker_alloc(void) {
    ExpressPollWorker* worker = malloc(sizeof(ExpressPollWorker));
    memset(worker, 0, sizeof(ExpressPollWorker));
    worker->tx = bit_buffer_alloc(WORKER_BUFFER_SIZE);
    worker->rx = bit_buffer_alloc(WORKER_BUFFER_SIZE);
    worker->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return worker;
}

void express_poll_worker_free(ExpressPollWorker* worker) {
    furi_check(worker);
    express_poll_worker_stop(worker);
    furi_mutex_free(worker->mutex);
    bit_buffer_free(worker->rx);
    bit_buffer_free(worker->tx);
    free(worker);
}

void express_poll_worker_set_callback(
    ExpressPollWorker* worker,
    ExpressPollWorkerCallback callback,
    void* context) {
    furi_check(worker);
    furi_check(!worker->running);
    worker->callback = callback;
    worker->context = context;
}

void express_poll_worker_start(ExpressPollWorker* worker, const ExpressPollWorkerConfig* config) {
    furi_check(worker);
    furi_check(config);
    furi_check(!worker->running);

    worker->config = *config;
    worker->misses = 0;
    worker->stop_requested = false;
    worker->update_pending = false;

    worker_lock(worker);
    memset(&worker->status, 0, sizeof(worker->status));
    worker->status.phase = ExpressPollPhaseSearching;
    worker->status.polled_system_code = config->system_code;
    worker_unlock(worker);

    worker->nfc = nfc_alloc();
    nfc_config(worker->nfc, NfcModePoller, NfcTechFelica);
    nfc_set_guard_time_us(worker->nfc, FELICA_GUARD_TIME_US);
    nfc_set_fdt_poll_fc(worker->nfc, FELICA_FDT_POLL_FC);
    nfc_set_fdt_poll_poll_us(worker->nfc, FELICA_POLL_POLL_MIN_US);

    worker->running = true;
    nfc_start(worker->nfc, worker_nfc_callback, worker);
    FURI_LOG_I(TAG, "Polling 0x%04X", config->system_code);
}

void express_poll_worker_stop(ExpressPollWorker* worker) {
    furi_check(worker);
    if(!worker->running) return;

    /* In poller mode nfc_stop() only joins; the callback returns NfcCommandStop. */
    worker->stop_requested = true;
    nfc_stop(worker->nfc);
    nfc_free(worker->nfc);
    worker->nfc = NULL;
    worker->running = false;

    worker_lock(worker);
    worker->status.phase = ExpressPollPhaseIdle;
    worker->status.card_present = false;
    worker_unlock(worker);
    FURI_LOG_I(TAG, "Stopped");
}

bool express_poll_worker_is_running(const ExpressPollWorker* worker) {
    furi_check(worker);
    return worker->running;
}

void express_poll_worker_get_status(ExpressPollWorker* worker, ExpressPollStatus* status) {
    furi_check(worker);
    furi_check(status);
    worker->update_pending = false;
    worker_lock(worker);
    *status = worker->status;
    worker_unlock(worker);
}
