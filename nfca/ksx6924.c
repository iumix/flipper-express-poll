#include "ksx6924.h"

#include <string.h>

/* AID and GET BALANCE: RfidResearchGroup/proxmark3 client/src/ksx6924/ksx6924core.c
 * (KSX6924Select, KSX6924GetBalance) and the Metrodroid wiki "South Korea" page:
 * 90 4C 00 00 04 returns the pre-paid balance as a 4-byte big-endian value in won. */
#define KSX6924_BALANCE_LEN  4U
/* T-money maximum load (en.wikipedia.org/wiki/T-money); anything above is not a balance. */
#define KSX6924_BALANCE_MAX  500000U
#define KSX6924_SW_OK        0x9000U
#define KSX6924_SW_NOT_FOUND 0x6A82U

/* The only two APDUs this module may ever send. */
static const uint8_t ksx6924_select[] =
    {0x00, 0xA4, 0x04, 0x00, 0x07, 0xD4, 0x10, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00};
static const uint8_t ksx6924_get_balance[] = {0x90, 0x4C, 0x00, 0x00, KSX6924_BALANCE_LEN};

static Ksx6924Result ksx6924_result(IsoDepResult result) {
    switch(result) {
    case IsoDepOk:
        return Ksx6924Ok;
    case IsoDepUnsupported:
        return Ksx6924Unsupported;
    case IsoDepTimeout:
        return Ksx6924Timeout;
    case IsoDepTransportError:
        return Ksx6924TransportError;
    case IsoDepCancelled:
        return Ksx6924Cancelled;
    case IsoDepProtocolError:
        break;
    }
    return Ksx6924ProtocolError;
}

bool ksx6924_parse_balance(const uint8_t* data, size_t len, int32_t* balance) {
    if(!data || !balance || len != KSX6924_BALANCE_LEN) return false;
    uint32_t value = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                     ((uint32_t)data[2] << 8) | data[3];
    if(value > KSX6924_BALANCE_MAX) return false;
    *balance = (int32_t)value;
    return true;
}

static Ksx6924Result ksx6924_exchange(
    IsoDep* dep,
    int32_t* balance,
    uint8_t response[ISO_DEP_RESPONSE_MAX],
    uint16_t* status_word) {
    size_t len = 0;
    IsoDepResult result =
        iso_dep_apdu(dep, ksx6924_select, sizeof(ksx6924_select), response, &len, status_word);
    /* The FCI holds the card number and purse details: never look at it. */
    memset(response, 0, ISO_DEP_RESPONSE_MAX);
    if(result != IsoDepOk) return ksx6924_result(result);
    if(*status_word == KSX6924_SW_NOT_FOUND) return Ksx6924NoApplication;
    if(*status_word != KSX6924_SW_OK) return Ksx6924SelectRejected;

    result = iso_dep_apdu(
        dep, ksx6924_get_balance, sizeof(ksx6924_get_balance), response, &len, status_word);
    if(result != IsoDepOk) return ksx6924_result(result);
    if(*status_word != KSX6924_SW_OK) return Ksx6924ReadRejected;
    return ksx6924_parse_balance(response, len, balance) ? Ksx6924Ok : Ksx6924InvalidBalance;
}

Ksx6924Result ksx6924_read_balance(
    IsoDepExchange exchange,
    IsoDepWait wait,
    void* context,
    int32_t* balance,
    uint16_t* status_word) {
    if(!exchange || !wait || !balance || !status_word) return Ksx6924ProtocolError;
    *status_word = 0;
    IsoDep dep;
    uint8_t response[ISO_DEP_RESPONSE_MAX];
    /* RATS is sent whatever the SAK says; some dual-interface cards still answer it. */
    IsoDepResult activated = iso_dep_activate(&dep, exchange, wait, context);
    Ksx6924Result result = activated == IsoDepTimeout ? Ksx6924NoRats : ksx6924_result(activated);
    if(result == Ksx6924Ok) result = ksx6924_exchange(&dep, balance, response, status_word);
    /* DESELECT after GET BALANCE is Wallet's success condition for T-Money. */
    if(result != Ksx6924Cancelled) iso_dep_deselect(&dep);
    memset(response, 0, sizeof(response));
    return result;
}
