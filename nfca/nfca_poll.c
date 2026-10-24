#include "nfca_poll.h"

#include <furi.h>
#include <lib/nfc/helpers/iso14443_crc.h>

#include <string.h>

#define TAG "NfcaPoll"

/* ECP is unanswered; keep its wait short so a missing reply costs little. */
#define NFCA_ECP_FWT_FC ISO14443_3A_FDT_LISTEN_FC

#define SEL_CMD(cascade_level) (0x93U + 2U * (cascade_level))
#define SEL_PAR(bytes, bits)   (((bytes) << 4 & 0xF0U) | ((bits) & 0x0FU))
#define SDD_CASCADE_TAG        0x88U
#define MAX_CASCADE_LEVELS     3U

NfcaPollResult nfca_poll_send_ecp(
    Nfc* nfc,
    BitBuffer* tx,
    BitBuffer* rx,
    const uint8_t* frame,
    size_t frame_len) {
    bit_buffer_copy_bytes(tx, frame, frame_len);
    iso14443_crc_append(Iso14443CrcTypeA, tx);
    bit_buffer_reset(rx);

    NfcError error = nfc_poller_trx(nfc, tx, rx, NFCA_ECP_FWT_FC);
    /* The device never answers ECP, so a timeout means the frame went out. */
    if(error == NfcErrorNone || error == NfcErrorTimeout) return NfcaPollResultOk;

    FURI_LOG_D(TAG, "ECP tx error %d", error);
    return NfcaPollResultError;
}

static NfcaPollResult nfca_poll_process_error(NfcError error) {
    if(error == NfcErrorTimeout) return NfcaPollResultTimeout;
    return NfcaPollResultError;
}

/* Standard NFC-A frame exchange: append CRC_A, transceive, verify and trim it. */
static NfcError nfca_poll_std_frame(Nfc* nfc, BitBuffer* tx, BitBuffer* rx) {
    iso14443_crc_append(Iso14443CrcTypeA, tx);
    NfcError error = nfc_poller_trx(nfc, tx, rx, ISO14443_3A_FDT_LISTEN_FC);
    if(error != NfcErrorNone) return error;
    if(!iso14443_crc_check(Iso14443CrcTypeA, rx)) return NfcErrorIncompleteFrame;
    iso14443_crc_trim(rx);
    return NfcErrorNone;
}

NfcaPollResult nfca_poll_activate(Nfc* nfc, BitBuffer* tx, BitBuffer* rx, Iso14443_3aData* data) {
    furi_check(nfc);
    furi_check(data);

    memset(data, 0, sizeof(Iso14443_3aData));

    /* SENS_REQ (REQA) short frame -> ATQA (2 bytes). */
    NfcError error = nfc_iso14443a_poller_trx_short_frame(
        nfc, NfcIso14443aShortFrameSensReq, rx, ISO14443_3A_FDT_LISTEN_FC);
    if(error != NfcErrorNone) return nfca_poll_process_error(error);
    if(bit_buffer_get_size_bytes(rx) != sizeof(Iso14443_3aSensResp))
        return NfcaPollResultCollision;
    bit_buffer_write_bytes(rx, data->atqa, sizeof(data->atqa));

    /* Anticollision cascade: SDD (get NFCID CLn) then SEL (get SAK) per level. */
    for(uint8_t level = 0; level < MAX_CASCADE_LEVELS; level++) {
        Iso14443_3aSddResp sdd_resp = {0};

        bit_buffer_reset(tx);
        bit_buffer_append_byte(tx, SEL_CMD(level));
        bit_buffer_append_byte(tx, SEL_PAR(2, 0));
        error = nfc_iso14443a_poller_trx_sdd_frame(nfc, tx, rx, ISO14443_3A_FDT_LISTEN_FC);
        if(error != NfcErrorNone) return nfca_poll_process_error(error);
        if(bit_buffer_get_size_bytes(rx) != sizeof(Iso14443_3aSddResp))
            return NfcaPollResultCollision;
        bit_buffer_write_bytes(rx, &sdd_resp, sizeof(sdd_resp));

        Iso14443_3aSelReq sel_req = {
            .sel_cmd = SEL_CMD(level),
            .sel_par = SEL_PAR(7, 0),
            .bcc = sdd_resp.bss,
        };
        memcpy(sel_req.nfcid, sdd_resp.nfcid, sizeof(sel_req.nfcid));

        bit_buffer_copy_bytes(tx, (const uint8_t*)&sel_req, sizeof(sel_req));
        error = nfca_poll_std_frame(nfc, tx, rx);
        if(error == NfcErrorIncompleteFrame) return NfcaPollResultCollision;
        if(error != NfcErrorNone) return nfca_poll_process_error(error);
        if(bit_buffer_get_size_bytes(rx) != sizeof(Iso14443_3aSelResp))
            return NfcaPollResultCollision;

        Iso14443_3aSelResp sel_resp = {0};
        bit_buffer_write_bytes(rx, &sel_resp, sizeof(sel_resp));

        if(sdd_resp.nfcid[0] == SDD_CASCADE_TAG) {
            /* Cascade tag: this level carried UID bytes 1..3, more to come. */
            if(data->uid_len + 3U > ISO14443_3A_MAX_UID_SIZE) return NfcaPollResultError;
            memcpy(&data->uid[data->uid_len], &sdd_resp.nfcid[1], 3);
            data->uid_len += 3;
        } else {
            if(data->uid_len + 4U > ISO14443_3A_MAX_UID_SIZE) return NfcaPollResultError;
            memcpy(&data->uid[data->uid_len], &sdd_resp.nfcid[0], 4);
            data->uid_len += 4;
            data->sak = sel_resp.sak;
            return NfcaPollResultOk;
        }
    }

    /* Ran out of cascade levels without a final SAK. */
    return NfcaPollResultError;
}
