#include "../nfca/ksx6924.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Protocol-derived fixtures, never compiled into the app. */
static const uint8_t ats[] = {2, 8};
static const uint8_t fci[] = {0x02, 0x6F, 0x03, 0xB0, 0x01, 0x00, 0x90, 0x00};
static const uint8_t balance_12345[] = {0x03, 0x00, 0x00, 0x30, 0x39, 0x90, 0x00};
static const uint8_t deselected[] = {0xC2};

static const uint8_t select_apdu[] =
    {0x00, 0xA4, 0x04, 0x00, 0x07, 0xD4, 0x10, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00};
static const uint8_t balance_apdu[] = {0x90, 0x4C, 0x00, 0x00, 0x04};

typedef struct {
    const uint8_t* replies[32];
    size_t lengths[32];
    size_t count;
    size_t step;
    size_t selects;
    size_t reads;
    size_t deselects;
    size_t cancel_at;
    size_t timeout_at;
    bool cancel_wait;
    uint8_t pcbs[32];
} Script;

static IsoDepResult exchange(
    void* context,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t* rx_len,
    uint32_t fwt) {
    Script* s = context;
    assert(fwt > 0 && fwt <= ISO_DEP_MAX_FWT_FC);
    if(s->step == s->cancel_at) return IsoDepCancelled;
    if(s->step == s->timeout_at) {
        s->step++;
        return IsoDepTimeout;
    }
    assert(s->step < s->count);
    s->pcbs[s->step] = tx[0];
    if((tx[0] & 0xFE) == 2) {
        /* Only the two allowlisted APDUs may ever be emitted, SELECT first. */
        if(tx_len - 1 == sizeof(select_apdu) && !memcmp(tx + 1, select_apdu, tx_len - 1)) {
            s->selects++;
        } else {
            assert(tx_len - 1 == sizeof(balance_apdu));
            assert(!memcmp(tx + 1, balance_apdu, sizeof(balance_apdu)));
            assert(s->selects == 1 && s->reads == 0);
            s->reads++;
        }
    } else if(tx[0] == 0xC2) {
        s->deselects++;
    } else {
        assert(tx[0] == 0xE0 || tx[0] == 0xF2 || tx[0] == 0xA2 || tx[0] == 0xA3);
    }
    assert(s->lengths[s->step] <= *rx_len);
    *rx_len = s->lengths[s->step];
    memcpy(rx, s->replies[s->step], *rx_len);
    s->step++;
    return IsoDepOk;
}

static bool wait_guard(void* context, uint32_t fc) {
    assert(fc <= ISO_DEP_MAX_FWT_FC);
    return !((Script*)context)->cancel_wait;
}

static Script normal(void) {
    return (Script){
        .replies = {ats, fci, balance_12345, deselected},
        .lengths = {sizeof(ats), sizeof(fci), sizeof(balance_12345), sizeof(deselected)},
        .count = 4,
        .cancel_at = 99,
        .timeout_at = 99};
}

/* Replace the reply at step and end the script with a DESELECT reply after it. */
static void reply_then_deselect(Script* s, size_t step, const uint8_t* reply, size_t len) {
    s->replies[step] = reply;
    s->lengths[step] = len;
    s->replies[step + 1] = deselected;
    s->lengths[step + 1] = sizeof(deselected);
    s->count = step + 2;
}

static void expect(Script* s, Ksx6924Result result, uint16_t sw, int32_t balance) {
    uint16_t actual_sw = 0xFFFF;
    int32_t actual_balance = -1;
    assert(ksx6924_read_balance(exchange, wait_guard, s, &actual_balance, &actual_sw) == result);
    assert(actual_sw == sw);
    assert(actual_balance == balance);
    assert(s->step == s->count || result == Ksx6924Cancelled);
    assert(s->deselects == (result == Ksx6924Cancelled ? 0U : 1U));
}

static void expect_no_deselect(Script* s, Ksx6924Result result) {
    uint16_t sw = 0xFFFF;
    int32_t balance = -1;
    assert(ksx6924_read_balance(exchange, wait_guard, s, &balance, &sw) == result);
    assert(sw == 0 && balance == -1 && s->deselects == 0);
}

static void test_parse(void) {
    int32_t value = -1;
    const uint8_t zero[] = {0, 0, 0, 0};
    assert(ksx6924_parse_balance(zero, 4, &value) && value == 0);
    const uint8_t max[] = {0x00, 0x07, 0xA1, 0x20};
    assert(ksx6924_parse_balance(max, 4, &value) && value == 500000);
    const uint8_t over[] = {0x00, 0x07, 0xA1, 0x21};
    assert(!ksx6924_parse_balance(over, 4, &value) && value == 500000);
    const uint8_t negative[] = {0xFF, 0xFF, 0xFF, 0xFF};
    assert(!ksx6924_parse_balance(negative, 4, &value));
    assert(!ksx6924_parse_balance(zero, 3, &value));
    assert(!ksx6924_parse_balance(zero, 5, &value));
    assert(!ksx6924_parse_balance(NULL, 4, &value));
}

int main(void) {
    test_parse();

    Script s = normal();
    expect(&s, Ksx6924Ok, 0x9000, 12345);
    assert(s.selects == 1 && s.reads == 1);
    assert(s.pcbs[0] == 0xE0 && s.pcbs[1] == 0x02 && s.pcbs[2] == 0x03 && s.pcbs[3] == 0xC2);

    /* SELECT failures stop before GET BALANCE. */
    const uint8_t not_found[] = {0x02, 0x6A, 0x82};
    s = normal();
    reply_then_deselect(&s, 1, not_found, sizeof(not_found));
    expect(&s, Ksx6924NoApplication, 0x6A82, -1);
    assert(s.reads == 0);
    const uint8_t select_denied[] = {0x02, 0x69, 0x85};
    s = normal();
    reply_then_deselect(&s, 1, select_denied, sizeof(select_denied));
    expect(&s, Ksx6924SelectRejected, 0x6985, -1);
    assert(s.reads == 0);

    /* GET BALANCE outcomes. */
    const uint8_t read_denied[] = {0x03, 0x6D, 0x00};
    s = normal();
    reply_then_deselect(&s, 2, read_denied, sizeof(read_denied));
    expect(&s, Ksx6924ReadRejected, 0x6D00, -1);
    const uint8_t short_balance[] = {0x03, 0x00, 0x30, 0x39, 0x90, 0x00};
    s = normal();
    reply_then_deselect(&s, 2, short_balance, sizeof(short_balance));
    expect(&s, Ksx6924InvalidBalance, 0x9000, -1);
    const uint8_t huge_balance[] = {0x03, 0x00, 0x10, 0x00, 0x00, 0x90, 0x00};
    s = normal();
    reply_then_deselect(&s, 2, huge_balance, sizeof(huge_balance));
    expect(&s, Ksx6924InvalidBalance, 0x9000, -1);
    const uint8_t no_sw[] = {0x03, 0x90};
    s = normal();
    reply_then_deselect(&s, 2, no_sw, sizeof(no_sw));
    expect(&s, Ksx6924ProtocolError, 0, -1);
    const uint8_t wrong_block[] = {0x02, 0x00, 0x00, 0x30, 0x39, 0x90, 0x00};
    s = normal();
    reply_then_deselect(&s, 2, wrong_block, sizeof(wrong_block));
    expect(&s, Ksx6924ProtocolError, 0, -1);

    /* WTX while the card computes the balance. */
    const uint8_t wtx[] = {0xF2, 2};
    s = normal();
    s.replies[2] = wtx;
    s.lengths[2] = sizeof(wtx);
    s.replies[3] = balance_12345;
    s.lengths[3] = sizeof(balance_12345);
    s.replies[4] = deselected;
    s.lengths[4] = sizeof(deselected);
    s.count = 5;
    expect(&s, Ksx6924Ok, 0x9000, 12345);
    assert(s.pcbs[3] == 0xF2 && s.reads == 1);

    /* Chained balance response: I-block with chaining, R(ACK), last I-block. */
    const uint8_t chained_first[] = {0x13, 0x00, 0x00};
    const uint8_t chained_last[] = {0x02, 0x30, 0x39, 0x90, 0x00};
    s = normal();
    s.replies[2] = chained_first;
    s.lengths[2] = sizeof(chained_first);
    s.replies[3] = chained_last;
    s.lengths[3] = sizeof(chained_last);
    s.replies[4] = deselected;
    s.lengths[4] = sizeof(deselected);
    s.count = 5;
    expect(&s, Ksx6924Ok, 0x9000, 12345);
    assert(s.pcbs[3] == 0xA2);

    /* ATS variants real cards use: FSC 16 (SELECT still fits), long historical
     * bytes, an FWT beyond the app's cap (exchange asserts the cap), no T0. */
    const uint8_t fsc16_ats[] = {2, 0x00};
    const uint8_t long_ats[] = {25, 0x78, 0x80, 0x70, 0x02, 1,  2,  3,  4,  5,  6,  7, 8,
                                9,  10,   11,   12,   13,   14, 15, 16, 17, 18, 19, 20};
    const uint8_t slow_ats[] = {3, 0x28, 0xE0};
    const uint8_t bare_ats[] = {1};
    const uint8_t* const variants[] = {fsc16_ats, long_ats, slow_ats, bare_ats};
    const size_t variant_lengths[] = {
        sizeof(fsc16_ats), sizeof(long_ats), sizeof(slow_ats), sizeof(bare_ats)};
    for(size_t i = 0; i < 4; i++) {
        s = normal();
        s.replies[0] = variants[i];
        s.lengths[0] = variant_lengths[i];
        expect(&s, Ksx6924Ok, 0x9000, 12345);
    }

    /* No answer to RATS: nothing else is sent. */
    s = normal();
    s.timeout_at = 0;
    s.count = 1;
    expect_no_deselect(&s, Ksx6924NoRats);
    assert(s.step == 1 && s.selects == 0);

    /* ATS problems: deselect, no APDU. */
    const uint8_t malformed_ats[] = {2, 0x78};
    s = normal();
    reply_then_deselect(&s, 0, malformed_ats, sizeof(malformed_ats));
    expect(&s, Ksx6924Unsupported, 0, -1);
    assert(s.selects == 0);

    /* Cancellation at an exchange and at the SFGT wait: no DESELECT. */
    s = normal();
    s.cancel_at = 2;
    expect(&s, Ksx6924Cancelled, 0, -1);
    assert(s.selects == 1 && s.reads == 0);
    s = normal();
    s.cancel_wait = true;
    expect(&s, Ksx6924Cancelled, 0, -1);
    assert(s.selects == 0);
    s = normal();
    s.cancel_at = 0;
    expect(&s, Ksx6924Cancelled, 0, -1);

    /* An endless WTX stream must terminate. */
    s = normal();
    for(size_t i = 2; i < 26; i++) {
        s.replies[i] = wtx;
        s.lengths[i] = sizeof(wtx);
    }
    s.replies[26] = deselected;
    s.lengths[26] = sizeof(deselected);
    s.count = 27;
    expect(&s, Ksx6924Timeout, 0, -1);

    puts("KS X 6924 tests passed");
    return 0;
}
