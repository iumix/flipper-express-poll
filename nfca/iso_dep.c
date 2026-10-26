#include "iso_dep.h"

#include <string.h>

/* ISO-DEP framing follows flipperdevices/flipperzero-firmware:
 * lib/nfc/protocols/iso14443_4a/iso14443_4a_poller_i.c and helpers/iso14443_4_layer.c. */
#define ISO_DEP_MAX_EXCHANGES 24U
#define ISO_DEP_RATS_FWT_FC   71680U
#define ISO_DEP_I_BLOCK       0x02U
#define ISO_DEP_CHAIN         0x10U
#define ISO_DEP_R_ACK         0xA2U
#define ISO_DEP_WTX           0xF2U
#define ISO_DEP_DESELECT      0xC2U

/* FSCI 0..8 per ISO 14443-4; larger and RFU codes mean at least 256, more than we send. */
static uint16_t iso_dep_fsc(uint8_t fsci) {
    static const uint16_t fsc[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
    return fsci < sizeof(fsc) / sizeof(fsc[0]) ? fsc[fsci] : 256;
}

static bool iso_dep_parse_ats(const uint8_t* ats, size_t len, IsoDep* dep, uint32_t* sfgt) {
    if(len < 1 || ats[0] != len) return false;
    dep->fwt = 4096U << 4;
    dep->fsc = 32; /* Default FSCI 2 when T0 is absent. */
    *sfgt = 0;
    if(len == 1) return true;
    uint8_t t0 = ats[1];
    if(t0 & 0x80) return false;
    dep->fsc = iso_dep_fsc(t0 & 0x0F);
    size_t pos = 2;
    if(t0 & 0x10) {
        if(pos >= len) return false;
        pos++;
    }
    if(t0 & 0x20) {
        if(pos >= len) return false;
        uint8_t tb = ats[pos++];
        if((tb >> 4) == 15 || (tb & 15) == 15) return false;
        dep->fwt = 4096U << (tb >> 4);
        if(tb & 15) *sfgt = 4096U << (tb & 15);
    }
    if((t0 & 0x40) && pos >= len) return false;
    /* Cards answer well within their declared FWT and use WTX when slower, so cap it
     * to keep Stop responsive; a genuinely slower card then ends as a timeout. */
    if(dep->fwt > ISO_DEP_MAX_FWT_FC) dep->fwt = ISO_DEP_MAX_FWT_FC;
    return *sfgt <= ISO_DEP_MAX_FWT_FC;
}

IsoDepResult
    iso_dep_activate(IsoDep* dep, IsoDepExchange exchange, IsoDepWait wait, void* context) {
    memset(dep, 0, sizeof(IsoDep));
    if(!exchange || !wait) return IsoDepProtocolError;
    dep->exchange = exchange;
    dep->context = context;

    const uint8_t rats[] = {0xE0, 0x80}; /* FSD 256, no CID. */
    uint8_t ats[ISO_DEP_FRAME_MAX];
    size_t len = sizeof(ats);
    IsoDepResult result = exchange(context, rats, sizeof(rats), ats, &len, ISO_DEP_RATS_FWT_FC);
    if(result != IsoDepOk) return result;
    /* The card answered RATS, so it must be deselected whatever happens next. */
    dep->active = true;

    uint32_t sfgt = 0;
    if(!iso_dep_parse_ats(ats, len, dep, &sfgt)) {
        result = IsoDepUnsupported;
    } else if(!wait(context, sfgt)) {
        result = IsoDepCancelled;
    }
    memset(ats, 0, sizeof(ats));
    return result;
}

IsoDepResult iso_dep_apdu(
    IsoDep* dep,
    const uint8_t* apdu,
    size_t apdu_len,
    uint8_t response[ISO_DEP_RESPONSE_MAX],
    size_t* response_len,
    uint16_t* status_word) {
    *response_len = 0;
    *status_word = 0;
    /* No outbound chaining: PCB + APDU + CRC_A must fit the card's FSC. */
    if(!dep->active || !apdu || apdu_len == 0 || apdu_len > ISO_DEP_APDU_MAX ||
       apdu_len + 3 > dep->fsc) {
        return IsoDepProtocolError;
    }

    uint8_t tx[1 + ISO_DEP_APDU_MAX] = {dep->block | ISO_DEP_I_BLOCK};
    memcpy(tx + 1, apdu, apdu_len);
    size_t tx_len = apdu_len + 1;
    uint32_t timeout = dep->fwt;
    IsoDepResult outcome = IsoDepTimeout;
    uint8_t rx[ISO_DEP_FRAME_MAX];
    for(unsigned i = 0; i < ISO_DEP_MAX_EXCHANGES; i++) {
        size_t rx_len = sizeof(rx);
        IsoDepResult result = dep->exchange(dep->context, tx, tx_len, rx, &rx_len, timeout);
        if(result != IsoDepOk) {
            outcome = result;
            break;
        }
        if(rx_len < 1 || rx_len > sizeof(rx)) {
            outcome = IsoDepProtocolError;
            break;
        }
        if(rx[0] == ISO_DEP_WTX) {
            if(rx_len != 2 || rx[1] == 0 || rx[1] > 59) {
                outcome = IsoDepProtocolError;
                break;
            }
            if(dep->fwt > ISO_DEP_MAX_FWT_FC / rx[1]) {
                outcome = IsoDepTimeout;
                break;
            }
            timeout = dep->fwt * rx[1];
            tx[0] = ISO_DEP_WTX;
            tx[1] = rx[1];
            tx_len = 2;
            continue;
        }
        /* No CID/NAD was negotiated. Reject duplicate, R- and unexpected S-blocks. */
        if((rx[0] & ~ISO_DEP_CHAIN) != (ISO_DEP_I_BLOCK | dep->block) ||
           rx_len - 1 > ISO_DEP_RESPONSE_MAX - *response_len) {
            outcome = IsoDepProtocolError;
            break;
        }
        memcpy(response + *response_len, rx + 1, rx_len - 1);
        *response_len += rx_len - 1;
        dep->block ^= 1;
        if(!(rx[0] & ISO_DEP_CHAIN)) {
            if(*response_len < 2) {
                outcome = IsoDepProtocolError;
                break;
            }
            *response_len -= 2;
            *status_word = ((uint16_t)response[*response_len] << 8) | response[*response_len + 1];
            outcome = IsoDepOk;
            break;
        }
        tx[0] = ISO_DEP_R_ACK | dep->block;
        tx_len = 1;
        timeout = dep->fwt;
    }
    memset(rx, 0, sizeof(rx));
    memset(tx, 0, sizeof(tx));
    return outcome;
}

void iso_dep_deselect(IsoDep* dep) {
    if(!dep->active) return;
    const uint8_t deselect = ISO_DEP_DESELECT;
    uint8_t rx[ISO_DEP_FRAME_MAX];
    size_t len = sizeof(rx);
    dep->exchange(dep->context, &deselect, 1, rx, &len, ISO_DEP_RATS_FWT_FC);
    memset(rx, 0, sizeof(rx));
    dep->active = false;
}
