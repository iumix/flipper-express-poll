#include "../nfca/emv_select.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Protocol-derived fixtures, never compiled into the app. */
static const uint8_t ats[] = {2, 8};
static const uint8_t ppse[] = {0x02, 0x6F, 0x12, 0xA5, 0x10, 0xBF, 0x0C, 0x0D,
                               0x61, 0x0B, 0x4F, 0x07, 0xA0, 0,    0,    0,
                               3,    0x10, 0x10, 0x87, 0x00, 0x90, 0x00};
static const uint8_t selected[] = {3, 0x90, 0};
static const uint8_t deselected[] = {0xC2};

typedef struct {
    const uint8_t* replies[32];
    size_t lengths[32];
    size_t count;
    size_t step;
    size_t selects;
    size_t cancel_at;
    bool cancel_wait;
    uint8_t names[2][16];
    size_t name_lengths[2];
    uint8_t pcbs[32];
} Script;

static EmvSelectResult exchange(
    void* context,
    const uint8_t* tx,
    size_t tx_len,
    uint8_t* rx,
    size_t* rx_len,
    uint32_t fwt) {
    Script* s = context;
    assert(fwt > 0 && fwt <= 4068000);
    if(s->step == s->cancel_at) return EmvSelectCancelled;
    assert(s->step < s->count);
    s->pcbs[s->step] = tx[0];
    if((tx[0] & 0xFE) == 2) {
        /* No APDU other than SELECT by name may ever be emitted. */
        assert(tx_len >= 12 && tx[1] == 0 && tx[2] == 0xA4);
        assert(tx[3] == 4 && tx[4] == 0 && tx[tx_len - 1] == 0);
        assert(tx_len == (size_t)tx[5] + 7 && s->selects < 2);
        s->name_lengths[s->selects] = tx[5];
        memcpy(s->names[s->selects++], tx + 6, tx[5]);
    } else {
        assert(tx[0] == 0xE0 || tx[0] == 0xF2 || tx[0] == 0xC2 || tx[0] == 0xA2 || tx[0] == 0xA3);
    }
    assert(s->lengths[s->step] <= *rx_len);
    *rx_len = s->lengths[s->step];
    memcpy(rx, s->replies[s->step], *rx_len);
    s->step++;
    return EmvSelectOk;
}

static bool wait_guard(void* context, uint32_t fc) {
    assert(fc <= 4068000);
    return !((Script*)context)->cancel_wait;
}

static Script normal(void) {
    return (Script){
        .replies = {ats, ppse, selected, deselected},
        .lengths = {sizeof(ats), sizeof(ppse), sizeof(selected), sizeof(deselected)},
        .count = 4,
        .cancel_at = 99};
}

static void expect(Script* s, EmvSelectResult result, uint16_t sw) {
    uint16_t actual = 0xFFFF;
    assert(emv_select_run(exchange, wait_guard, s, &actual) == result);
    assert(actual == sw);
}

int main(void) {
    Script s = normal();
    expect(&s, EmvSelectOk, 0x9000);
    assert(s.step == 4 && s.selects == 2);
    assert(s.name_lengths[0] == 14 && memcmp(s.names[0], "2PAY.SYS.DDF01", 14) == 0);
    assert(s.name_lengths[1] == 7 && memcmp(s.names[1], ppse + 12, 7) == 0);

    const uint8_t rejected[] = {2, 0x6A, 0x82};
    s = normal();
    s.replies[1] = rejected;
    s.lengths[1] = sizeof(rejected);
    s.replies[2] = deselected;
    s.lengths[2] = 1;
    expect(&s, EmvSelectRejected, 0x6A82);
    assert(s.selects == 1);

    s = normal();
    s.cancel_at = 2;
    expect(&s, EmvSelectCancelled, 0);
    assert(s.selects == 1);
    s = normal();
    s.cancel_wait = true;
    expect(&s, EmvSelectCancelled, 0);
    assert(s.selects == 0);

    const uint8_t malformed_ats[] = {2, 0x78};
    s = normal();
    s.replies[0] = malformed_ats;
    s.replies[1] = deselected;
    s.lengths[1] = 1;
    expect(&s, EmvSelectUnsupported, 0);
    assert(s.selects == 0);

    /* Validate every truncation of a successful directory response. */
    for(size_t len = 0; len < sizeof(ppse) - 3; len++) {
        uint8_t truncated[sizeof(ppse)];
        memcpy(truncated, ppse, len + 1);
        truncated[len + 1] = 0x90;
        truncated[len + 2] = 0;
        s = normal();
        s.replies[1] = truncated;
        s.lengths[1] = len + 3;
        s.replies[2] = deselected;
        s.lengths[2] = 1;
        expect(&s, len == 0 ? EmvSelectNoApplication : EmvSelectProtocolError, 0x9000);
        assert(s.selects == 1);
    }

    const uint8_t wtx[] = {0xF2, 2};
    s = normal();
    s.replies[1] = wtx;
    s.lengths[1] = sizeof(wtx);
    s.replies[2] = ppse;
    s.lengths[2] = sizeof(ppse);
    s.replies[3] = selected;
    s.lengths[3] = sizeof(selected);
    s.replies[4] = deselected;
    s.lengths[4] = 1;
    s.count = 5;
    expect(&s, EmvSelectOk, 0x9000);
    assert(s.pcbs[2] == 0xF2);

    const uint8_t invalid_wtx[] = {0xF2, 0};
    s = normal();
    s.replies[1] = invalid_wtx;
    s.lengths[1] = 2;
    s.replies[2] = deselected;
    s.lengths[2] = 1;
    expect(&s, EmvSelectProtocolError, 0);

    /* An endless WTX stream must terminate, even if every individual frame is valid. */
    s = normal();
    for(size_t i = 1; i < 25; i++) {
        s.replies[i] = wtx;
        s.lengths[i] = 2;
    }
    s.replies[25] = deselected;
    s.lengths[25] = 1;
    s.count = 26;
    expect(&s, EmvSelectTimeout, 0);
    assert(s.step == 26);

    uint8_t first[11], last[sizeof(ppse) - 10 + 1];
    memcpy(first, ppse, sizeof(first));
    first[0] = 0x12;
    last[0] = 3;
    memcpy(last + 1, ppse + 11, sizeof(ppse) - 11);
    const uint8_t selected_zero[] = {2, 0x90, 0};
    s = normal();
    s.replies[1] = first;
    s.lengths[1] = sizeof(first);
    s.replies[2] = last;
    s.lengths[2] = sizeof(ppse) - 10;
    s.replies[3] = selected_zero;
    s.lengths[3] = 3;
    s.replies[4] = deselected;
    s.lengths[4] = 1;
    s.count = 5;
    expect(&s, EmvSelectOk, 0x9000);
    assert(s.pcbs[2] == 0xA3 && s.pcbs[3] == 2);

    const uint8_t app_rejected[] = {3, 0x69, 0x85};
    s = normal();
    s.replies[2] = app_rejected;
    s.lengths[2] = sizeof(app_rejected);
    expect(&s, EmvSelectRejected, 0x6985);
    assert(s.selects == 2);

    const uint8_t no_aid[] = {2, 0x6F, 2, 0xA5, 0, 0x90, 0};
    s = normal();
    s.replies[1] = no_aid;
    s.lengths[1] = sizeof(no_aid);
    s.replies[2] = deselected;
    s.lengths[2] = 1;
    expect(&s, EmvSelectNoApplication, 0x9000);
    assert(s.selects == 1);

    /* Bad TLV lengths, invalid AID sizes, duplicate block number, and short SW. */
    const uint8_t bad_length[] = {2, 0x6F, 0x82, 0xFF, 0xFF, 0x90, 0};
    s = normal();
    s.replies[1] = bad_length;
    s.lengths[1] = sizeof(bad_length);
    s.replies[2] = deselected;
    s.lengths[2] = 1;
    expect(&s, EmvSelectProtocolError, 0x9000);
    uint8_t bad_aid[sizeof(ppse)];
    memcpy(bad_aid, ppse, sizeof(ppse));
    bad_aid[11] = 17;
    s = normal();
    s.replies[1] = bad_aid;
    s.replies[2] = deselected;
    s.lengths[2] = 1;
    expect(&s, EmvSelectProtocolError, 0x9000);
    s = normal();
    s.replies[1] = selected;
    s.lengths[1] = sizeof(selected);
    s.replies[2] = deselected;
    s.lengths[2] = 1;
    expect(&s, EmvSelectProtocolError, 0);
    const uint8_t short_sw[] = {2, 0x90};
    s = normal();
    s.replies[1] = short_sw;
    s.lengths[1] = sizeof(short_sw);
    s.replies[2] = deselected;
    s.lengths[2] = 1;
    expect(&s, EmvSelectProtocolError, 0);

    /* Legal timing values that exceed the app's bound must not reach transport. */
    const uint8_t slow_ats[] = {3, 0x28, 0xE0};
    s = normal();
    s.replies[0] = slow_ats;
    s.lengths[0] = sizeof(slow_ats);
    s.replies[1] = deselected;
    s.lengths[1] = 1;
    expect(&s, EmvSelectUnsupported, 0);
    assert(s.selects == 0);

    uint8_t oversized[254] = {0x12};
    uint8_t oversized_next[254] = {0x13};
    s = normal();
    s.replies[1] = oversized;
    s.lengths[1] = sizeof(oversized);
    s.replies[2] = oversized_next;
    s.lengths[2] = sizeof(oversized_next);
    s.replies[3] = oversized;
    s.lengths[3] = sizeof(oversized);
    s.replies[4] = deselected;
    s.lengths[4] = 1;
    s.count = 5;
    expect(&s, EmvSelectProtocolError, 0);

    puts("EMV selection tests passed");
    return 0;
}
