#include "emv_select.h"

#include <string.h>

/* ISO-DEP framing follows flipperdevices/flipperzero-firmware:
 * lib/nfc/protocols/iso14443_4a/iso14443_4a_poller_i.c and helpers/iso14443_4_layer.c.
 * SELECT/PPSE: RfidResearchGroup/proxmark3, client/src/emv/emvcore.c (EMVSelect). */
#define EMV_FRAME_MAX     256U
#define EMV_RESPONSE_MAX  512U
#define EMV_MAX_EXCHANGES 24U
#define EMV_MAX_FWT_FC    4068000U /* 300 ms; keeps Stop responsive. */
#define EMV_RATS_FWT_FC   71680U
#define EMV_I_BLOCK       0x02U
#define EMV_CHAIN         0x10U
#define EMV_R_ACK         0xA2U
#define EMV_WTX           0xF2U
#define EMV_DESELECT      0xC2U
#define EMV_SELECT_INS    0xA4U
#define EMV_SW_OK         0x9000U

static bool emv_select_ats(const uint8_t* ats, size_t len, uint32_t* fwt, uint32_t* sfgt) {
    if(len < 1 || len > 20 || ats[0] != len) return false;
    *fwt = 4096U << 4;
    *sfgt = 0;
    if(len == 1) return true;
    uint8_t t0 = ats[1];
    /* shortcut: SELECT fits an FSC >=32; smaller frames need outbound chaining. */
    if((t0 & 0x80) || (t0 & 0x0F) < 2 || (t0 & 0x0F) > 8) return false;
    size_t pos = 2;
    if(t0 & 0x10) {
        if(pos >= len) return false;
        pos++;
    }
    if(t0 & 0x20) {
        if(pos >= len) return false;
        uint8_t tb = ats[pos++];
        if((tb >> 4) == 15 || (tb & 15) == 15) return false;
        *fwt = 4096U << (tb >> 4);
        if(tb & 15) *sfgt = 4096U << (tb & 15);
    }
    if((t0 & 0x40) && pos >= len) return false;
    return *fwt <= EMV_MAX_FWT_FC && *sfgt <= EMV_MAX_FWT_FC;
}

/* Only descend through the PPSE directory path; ignore unrelated FCI fields. */
static bool emv_select_find_aid(
    const uint8_t* data,
    size_t len,
    unsigned depth,
    uint8_t aid[16],
    size_t* aid_len) {
    static const uint16_t path[] = {0x6F, 0xA5, 0xBF0C, 0x61, 0x4F};
    if(depth >= sizeof(path) / sizeof(path[0])) return false;
    size_t pos = 0;
    while(pos < len) {
        uint8_t first = data[pos++];
        uint16_t tag = first;
        if((first & 0x1F) == 0x1F) {
            if(pos >= len || (data[pos] & 0x80) || !(data[pos] & 0x7F)) return false;
            tag = ((uint16_t)first << 8) | data[pos++];
        }
        if(pos >= len) return false;
        size_t size = data[pos++];
        if(size & 0x80) {
            size_t bytes = size & 0x7F;
            if(bytes == 0 || bytes > 2 || bytes > len - pos) return false;
            size = 0;
            while(bytes--)
                size = (size << 8) | data[pos++];
        }
        if(size > len - pos) return false;
        if(tag == path[depth]) {
            if(tag == 0x4F) {
                if(size < 5 || size > 16) return false;
                if(*aid_len == 0) {
                    memcpy(aid, data + pos, size);
                    *aid_len = size;
                }
            } else if(!emv_select_find_aid(data + pos, size, depth + 1, aid, aid_len)) {
                return false;
            }
        }
        pos += size;
    }
    return true;
}

static EmvSelectResult emv_select_apdu(
    EmvSelectExchange exchange,
    void* context,
    uint32_t fwt,
    uint8_t* block,
    const uint8_t* name,
    size_t name_len,
    uint8_t response[EMV_RESPONSE_MAX],
    size_t* response_len,
    uint16_t* status_word) {
    uint8_t tx[24] = {*block | EMV_I_BLOCK, 0x00, EMV_SELECT_INS, 0x04, 0x00};
    tx[5] = (uint8_t)name_len;
    memcpy(tx + 6, name, name_len);
    tx[6 + name_len] = 0; /* Le: maximum short response. */
    size_t tx_len = name_len + 7;
    uint32_t timeout = fwt;
    *response_len = 0;
    *status_word = 0;
    EmvSelectResult outcome = EmvSelectTimeout;
    uint8_t rx[EMV_FRAME_MAX];
    for(unsigned i = 0; i < EMV_MAX_EXCHANGES; i++) {
        size_t rx_len = sizeof(rx);
        EmvSelectResult result = exchange(context, tx, tx_len, rx, &rx_len, timeout);
        if(result != EmvSelectOk) {
            outcome = result;
            break;
        }
        if(rx_len < 1 || rx_len > sizeof(rx)) {
            outcome = EmvSelectProtocolError;
            break;
        }
        if(rx[0] == EMV_WTX) {
            if(rx_len != 2 || rx[1] == 0 || rx[1] > 59) {
                outcome = EmvSelectProtocolError;
                break;
            }
            if(fwt > EMV_MAX_FWT_FC / rx[1]) {
                outcome = EmvSelectTimeout;
                break;
            }
            timeout = fwt * rx[1];
            tx[0] = EMV_WTX;
            tx[1] = rx[1];
            tx_len = 2;
            continue;
        }
        /* No CID/NAD was negotiated. Reject duplicate, R- and unexpected S-blocks. */
        if((rx[0] & ~EMV_CHAIN) != (EMV_I_BLOCK | *block) ||
           rx_len - 1 > EMV_RESPONSE_MAX - *response_len) {
            outcome = EmvSelectProtocolError;
            break;
        }
        memcpy(response + *response_len, rx + 1, rx_len - 1);
        *response_len += rx_len - 1;
        *block ^= 1;
        if(!(rx[0] & EMV_CHAIN)) {
            if(*response_len < 2) {
                outcome = EmvSelectProtocolError;
                break;
            }
            *response_len -= 2;
            *status_word = ((uint16_t)response[*response_len] << 8) | response[*response_len + 1];
            outcome = *status_word == EMV_SW_OK ? EmvSelectOk : EmvSelectRejected;
            break;
        }
        tx[0] = EMV_R_ACK | *block;
        tx_len = 1;
        timeout = fwt;
    }
    memset(rx, 0, sizeof(rx));
    memset(tx, 0, sizeof(tx));
    return outcome;
}

EmvSelectResult emv_select_run(
    EmvSelectExchange exchange,
    EmvSelectWait wait,
    void* context,
    uint16_t* status_word) {
    if(!exchange || !wait || !status_word) return EmvSelectProtocolError;
    *status_word = 0;
    const uint8_t rats[] = {0xE0, 0x80}; /* FSD 256, no CID. */
    uint8_t response[EMV_RESPONSE_MAX];
    uint8_t aid[16] = {0};
    size_t len = EMV_FRAME_MAX;
    EmvSelectResult result =
        exchange(context, rats, sizeof(rats), response, &len, EMV_RATS_FWT_FC);
    if(result != EmvSelectOk) return result;
    uint32_t fwt = 0, sfgt = 0;
    if(!emv_select_ats(response, len, &fwt, &sfgt)) {
        result = EmvSelectUnsupported;
    } else if(!wait(context, sfgt)) {
        result = EmvSelectCancelled;
    } else {
        static const uint8_t ppse[] = "2PAY.SYS.DDF01";
        uint8_t block = 0;
        result = emv_select_apdu(
            exchange, context, fwt, &block, ppse, sizeof(ppse) - 1, response, &len, status_word);
        if(result == EmvSelectOk) {
            size_t aid_len = 0;
            if(!emv_select_find_aid(response, len, 0, aid, &aid_len)) {
                result = EmvSelectProtocolError;
            } else if(!aid_len) {
                result = EmvSelectNoApplication;
            } else {
                result = emv_select_apdu(
                    exchange, context, fwt, &block, aid, aid_len, response, &len, status_word);
            }
        }
    }
    /* End ISO-DEP so subsequent WUPA presence checks can activate the same device. */
    if(result != EmvSelectCancelled) {
        const uint8_t deselect = EMV_DESELECT;
        len = EMV_FRAME_MAX;
        exchange(context, &deselect, 1, response, &len, EMV_RATS_FWT_FC);
    }
    memset(response, 0, sizeof(response));
    memset(aid, 0, sizeof(aid));
    return result;
}
