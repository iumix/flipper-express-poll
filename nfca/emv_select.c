#include "emv_select.h"

#include <string.h>

/* SELECT/PPSE: RfidResearchGroup/proxmark3, client/src/emv/emvcore.c (EMVSelect). */
#define EMV_AID_MAX    16U
#define EMV_SELECT_INS 0xA4U
#define EMV_SW_OK      0x9000U

static EmvSelectResult emv_select_result(IsoDepResult result) {
    switch(result) {
    case IsoDepOk:
        return EmvSelectOk;
    case IsoDepUnsupported:
        return EmvSelectUnsupported;
    case IsoDepTimeout:
        return EmvSelectTimeout;
    case IsoDepTransportError:
        return EmvSelectTransportError;
    case IsoDepCancelled:
        return EmvSelectCancelled;
    case IsoDepProtocolError:
        break;
    }
    return EmvSelectProtocolError;
}

/* Only descend through the PPSE directory path; ignore unrelated FCI fields. */
static bool emv_select_find_aid(
    const uint8_t* data,
    size_t len,
    unsigned depth,
    uint8_t aid[EMV_AID_MAX],
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
                if(size < 5 || size > EMV_AID_MAX) return false;
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

static EmvSelectResult emv_select_by_name(
    IsoDep* dep,
    const uint8_t* name,
    size_t name_len,
    uint8_t response[ISO_DEP_RESPONSE_MAX],
    size_t* response_len,
    uint16_t* status_word) {
    uint8_t apdu[6 + EMV_AID_MAX] = {0x00, EMV_SELECT_INS, 0x04, 0x00};
    apdu[4] = (uint8_t)name_len;
    memcpy(apdu + 5, name, name_len);
    apdu[5 + name_len] = 0; /* Le: maximum short response. */
    IsoDepResult result =
        iso_dep_apdu(dep, apdu, name_len + 6, response, response_len, status_word);
    if(result != IsoDepOk) return emv_select_result(result);
    return *status_word == EMV_SW_OK ? EmvSelectOk : EmvSelectRejected;
}

EmvSelectResult
    emv_select_run(IsoDepExchange exchange, IsoDepWait wait, void* context, uint16_t* status_word) {
    if(!exchange || !wait || !status_word) return EmvSelectProtocolError;
    *status_word = 0;
    IsoDep dep;
    uint8_t response[ISO_DEP_RESPONSE_MAX];
    uint8_t aid[EMV_AID_MAX] = {0};
    size_t len = 0;
    EmvSelectResult result = emv_select_result(iso_dep_activate(&dep, exchange, wait, context));
    if(result == EmvSelectOk) {
        static const uint8_t ppse[] = "2PAY.SYS.DDF01";
        result = emv_select_by_name(&dep, ppse, sizeof(ppse) - 1, response, &len, status_word);
        if(result == EmvSelectOk) {
            size_t aid_len = 0;
            if(!emv_select_find_aid(response, len, 0, aid, &aid_len)) {
                result = EmvSelectProtocolError;
            } else if(!aid_len) {
                result = EmvSelectNoApplication;
            } else {
                result = emv_select_by_name(&dep, aid, aid_len, response, &len, status_word);
            }
        }
    }
    if(result != EmvSelectCancelled) iso_dep_deselect(&dep);
    memset(response, 0, sizeof(response));
    memset(aid, 0, sizeof(aid));
    return result;
}
