/*
 * Host unit tests for the hardware-independent modules:
 * FeliCa frame building/parsing, system code helpers and balance decoding.
 * Build and run with `make -C tests`.
 */
#include "../balance/balance.h"
#include "../felica/felica_frames.h"
#include "../felica/felica_systems.h"
#include "../nfca/ecp_frames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        checks++;                                                                    \
        if(!(cond)) {                                                                \
            failures++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                            \
    } while(0)

static const uint8_t test_idm[8] = {0x01, 0x2E, 0x4C, 0xD5, 0x3A, 0x1B, 0x22, 0x0F};
static const uint8_t test_pmm[8] = {0x10, 0x0B, 0x4B, 0x42, 0x84, 0x85, 0xD0, 0xFF};

/* --- Frame builders ------------------------------------------------------ */

static void test_build_polling(void) {
    uint8_t frame[8];

    /* Matches transit-polling sensf_req_0003 with the LEN byte prepended. */
    static const uint8_t suica[] = {0x06, 0x00, 0x00, 0x03, 0x01, 0x00};
    CHECK(felica_frame_build_polling(frame, sizeof(frame), 0x0003, 0x01, 0x00) == 6);
    CHECK(memcmp(frame, suica, sizeof(suica)) == 0);

    /* Matches transit-polling sensf_req_8008. */
    static const uint8_t octopus[] = {0x06, 0x00, 0x80, 0x08, 0x01, 0x00};
    CHECK(felica_frame_build_polling(frame, sizeof(frame), 0x8008, 0x01, 0x00) == 6);
    CHECK(memcmp(frame, octopus, sizeof(octopus)) == 0);

    static const uint8_t wildcard_no_rc[] = {0x06, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    CHECK(felica_frame_build_polling(frame, sizeof(frame), 0xFFFF, 0x00, 0x00) == 6);
    CHECK(memcmp(frame, wildcard_no_rc, sizeof(wildcard_no_rc)) == 0);

    CHECK(felica_frame_build_polling(frame, 5, 0x0003, 0x01, 0x00) == 0);
    CHECK(felica_frame_build_polling(NULL, 8, 0x0003, 0x01, 0x00) == 0);
}

static void test_build_read(void) {
    uint8_t frame[20];

    /* Byte-for-byte the read_cmd of transit-polling/src/suica_balance.c. */
    uint8_t suica[16] = {0x10, 0x06, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x8B, 0x00, 0x01, 0x80, 0x00};
    memcpy(suica + 2, test_idm, 8);
    CHECK(felica_frame_build_read_one_block(frame, sizeof(frame), test_idm, 0x008B, 0) == 16);
    CHECK(memcmp(frame, suica, sizeof(suica)) == 0);

    /* Byte-for-byte the read_cmd of transit-polling/src/octopus_balance.c. */
    uint8_t octopus[16] = {0x10, 0x06, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x17, 0x01, 0x01, 0x80, 0x00};
    memcpy(octopus + 2, test_idm, 8);
    CHECK(felica_frame_build_read_one_block(frame, sizeof(frame), test_idm, 0x0117, 0) == 16);
    CHECK(memcmp(frame, octopus, sizeof(octopus)) == 0);

    CHECK(felica_frame_build_read_one_block(frame, sizeof(frame), test_idm, 0x0117, 5) == 16);
    CHECK(frame[15] == 5);
    CHECK(felica_frame_build_read_one_block(frame, 15, test_idm, 0x0117, 0) == 0);
}

static void test_build_request_service(void) {
    uint8_t frame[16];
    uint8_t expected[13] = {0x0D, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x17, 0x01};
    memcpy(expected + 2, test_idm, 8);
    CHECK(felica_frame_build_request_service(frame, sizeof(frame), test_idm, 0x0117) == 13);
    CHECK(memcmp(frame, expected, sizeof(expected)) == 0);
    CHECK(felica_frame_build_request_service(frame, 12, test_idm, 0x0117) == 0);
}

/* --- Response parsers ---------------------------------------------------- */

static size_t make_polling_response(uint8_t* out, bool with_system_code, uint16_t code) {
    size_t len = with_system_code ? 20 : 18;
    out[0] = (uint8_t)len;
    out[1] = 0x01;
    memcpy(out + 2, test_idm, 8);
    memcpy(out + 10, test_pmm, 8);
    if(with_system_code) {
        out[18] = (uint8_t)(code >> 8);
        out[19] = (uint8_t)code;
    }
    return len;
}

static void test_parse_polling(void) {
    uint8_t frame[32];
    FelicaPollingResponse resp;

    size_t len = make_polling_response(frame, true, 0x8008);
    CHECK(felica_frame_parse_polling(frame, len, &resp) == FelicaFrameOk);
    CHECK(memcmp(resp.idm, test_idm, 8) == 0);
    CHECK(memcmp(resp.pmm, test_pmm, 8) == 0);
    CHECK(resp.has_system_code);
    CHECK(resp.system_code == 0x8008);

    len = make_polling_response(frame, false, 0);
    CHECK(felica_frame_parse_polling(frame, len, &resp) == FelicaFrameOk);
    CHECK(!resp.has_system_code);

    len = make_polling_response(frame, true, 0x0003);
    frame[1] = 0x07;
    CHECK(felica_frame_parse_polling(frame, len, &resp) == FelicaFrameErrorResponseCode);

    len = make_polling_response(frame, true, 0x0003);
    frame[0] = 0x14 + 1; /* LEN byte disagrees with received length */
    CHECK(felica_frame_parse_polling(frame, len, &resp) == FelicaFrameErrorLength);

    len = make_polling_response(frame, false, 0);
    CHECK(felica_frame_parse_polling(frame, 17, &resp) == FelicaFrameErrorLength);
    CHECK(felica_frame_parse_polling(NULL, 18, &resp) == FelicaFrameErrorLength);
}

static void test_parse_request_service(void) {
    uint8_t frame[13] = {0x0D, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x00, 0x10};
    memcpy(frame + 2, test_idm, 8);
    uint16_t key_version = 0;

    CHECK(felica_frame_parse_request_service(frame, 13, test_idm, &key_version) == FelicaFrameOk);
    CHECK(key_version == 0x1000);

    frame[11] = 0xFF;
    frame[12] = 0xFF;
    CHECK(felica_frame_parse_request_service(frame, 13, test_idm, &key_version) == FelicaFrameOk);
    CHECK(key_version == FELICA_FRAME_KEY_VERSION_MISSING);

    uint8_t other_idm[8] = {0};
    CHECK(
        felica_frame_parse_request_service(frame, 13, other_idm, &key_version) ==
        FelicaFrameErrorIdm);

    frame[1] = 0x01;
    CHECK(
        felica_frame_parse_request_service(frame, 13, test_idm, &key_version) ==
        FelicaFrameErrorResponseCode);
    frame[1] = 0x03;

    frame[10] = 2; /* We asked for one node */
    CHECK(
        felica_frame_parse_request_service(frame, 13, test_idm, &key_version) ==
        FelicaFrameErrorLength);
}

static size_t make_read_response(uint8_t* out, const uint8_t block[16]) {
    out[0] = 29;
    out[1] = 0x07;
    memcpy(out + 2, test_idm, 8);
    out[10] = 0x00;
    out[11] = 0x00;
    out[12] = 0x01;
    memcpy(out + 13, block, 16);
    return 29;
}

static void test_parse_read(void) {
    uint8_t block[16];
    for(int i = 0; i < 16; i++)
        block[i] = (uint8_t)(0xA0 + i);
    uint8_t frame[32];
    uint8_t out[16];
    uint8_t sf1 = 0xEE, sf2 = 0xEE;

    size_t len = make_read_response(frame, block);
    CHECK(
        felica_frame_parse_read_one_block(frame, len, test_idm, &sf1, &sf2, out) == FelicaFrameOk);
    CHECK(sf1 == 0 && sf2 == 0);
    CHECK(memcmp(out, block, 16) == 0);

    /* Error response: 12 bytes, no block count. SF2 0xA6 = illegal service code. */
    uint8_t error_frame[12] = {0x0C, 0x07, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0xA6};
    memcpy(error_frame + 2, test_idm, 8);
    CHECK(
        felica_frame_parse_read_one_block(error_frame, 12, test_idm, &sf1, &sf2, out) ==
        FelicaFrameErrorStatus);
    CHECK(sf1 == 0x01 && sf2 == 0xA6);

    uint8_t other_idm[8] = {0xFF};
    CHECK(
        felica_frame_parse_read_one_block(frame, len, other_idm, &sf1, &sf2, out) ==
        FelicaFrameErrorIdm);

    /* Truncated block data. */
    frame[0] = 28;
    CHECK(
        felica_frame_parse_read_one_block(frame, 28, test_idm, &sf1, &sf2, out) ==
        FelicaFrameErrorLength);

    /* Block count says 0 despite success. */
    len = make_read_response(frame, block);
    frame[12] = 0;
    CHECK(
        felica_frame_parse_read_one_block(frame, len, test_idm, &sf1, &sf2, out) ==
        FelicaFrameErrorLength);

    len = make_read_response(frame, block);
    frame[1] = 0x09;
    CHECK(
        felica_frame_parse_read_one_block(frame, len, test_idm, &sf1, &sf2, out) ==
        FelicaFrameErrorResponseCode);
}

static void test_pmm_timeout(void) {
    uint8_t pmm[8] = {0};

    pmm[5] = 0x00; /* E=0 B=0 A=0 -> 4096 * (1 + 1) */
    CHECK(felica_frame_pmm_timeout_fc(pmm, 5, 1) == 8192);

    pmm[5] = 0xFF; /* E=3 B=7 A=7 -> 4096 * (8 + 8) * 64 */
    CHECK(felica_frame_pmm_timeout_fc(pmm, 5, 1) == 4194304);

    /* 0x85: E=2 B=0 A=5 -> 4096 * (1 + 6) * 16 = 458752 fc (~33.8 ms). */
    CHECK(felica_frame_pmm_timeout_fc(test_pmm, FELICA_FRAME_PMM_READ, 1) == 458752);
    /* Two blocks add another (B + 1) unit. */
    CHECK(felica_frame_pmm_timeout_fc(test_pmm, FELICA_FRAME_PMM_READ, 2) == 524288);

    CHECK(felica_frame_pmm_timeout_fc(pmm, 8, 1) == 0);
    CHECK(felica_frame_pmm_timeout_fc(NULL, 5, 1) == 0);
}

/* --- System codes -------------------------------------------------------- */

static void test_system_codes(void) {
    char text[FELICA_SYSTEM_CODE_STR_SIZE];
    uint16_t code = 0;

    felica_system_code_format(0x0003, text);
    CHECK(strcmp(text, "0x0003") == 0);
    felica_system_code_format(0x8008, text);
    CHECK(strcmp(text, "0x8008") == 0);
    felica_system_code_format(0xfe00, text);
    CHECK(strcmp(text, "0xFE00") == 0);

    CHECK(felica_system_code_parse("0x0003", &code) && code == 0x0003);
    CHECK(felica_system_code_parse("8008", &code) && code == 0x8008);
    CHECK(felica_system_code_parse("0Xfe00", &code) && code == 0xFE00);
    CHECK(felica_system_code_parse(" 12fc ", &code) && code == 0x12FC);
    CHECK(felica_system_code_parse("3", &code) && code == 0x0003);
    CHECK(!felica_system_code_parse("", &code));
    CHECK(!felica_system_code_parse("0x", &code));
    CHECK(!felica_system_code_parse("0x10000", &code));
    CHECK(!felica_system_code_parse("12G4", &code));
    CHECK(!felica_system_code_parse("-1", &code));
    CHECK(!felica_system_code_parse(NULL, &code));

    /* Round trip of every value. */
    bool round_trip = true;
    for(uint32_t value = 0; value <= 0xFFFF; value++) {
        felica_system_code_format((uint16_t)value, text);
        if(!felica_system_code_parse(text, &code) || code != value) round_trip = false;
    }
    CHECK(round_trip);

    CHECK(felica_system_code_is_wildcard(0xFFFF));
    CHECK(felica_system_code_is_wildcard(0x00FF));
    CHECK(felica_system_code_is_wildcard(0xFF03));
    CHECK(!felica_system_code_is_wildcard(0x0003));
    CHECK(!felica_system_code_is_wildcard(0x8008));

    CHECK(strcmp(felica_system_name(0x0003), "Suica/ICOCA/PASMO") == 0);
    CHECK(strcmp(felica_system_name(0x8008), "Octopus") == 0);
    CHECK(felica_system_name(0x1234) == NULL);
}

/* --- Balance ------------------------------------------------------------- */

static void test_decoder_table(void) {
    const BalanceDecoder* suica = balance_decoder_find(0x0003);
    const BalanceDecoder* octopus = balance_decoder_find(0x8008);
    CHECK(suica && octopus);
    CHECK(balance_decoder_find(0xFE00) == NULL);
    CHECK(balance_decoder_find(0xFFFF) == NULL);

    /* Same services as the reference implementation. */
    CHECK(suica->service_code == 0x008B && suica->block_number == 0);
    CHECK(octopus->service_code == 0x0117 && octopus->block_number == 0);

    /* Every decoder must target a read-only, no-authentication service:
     * 0x0B random RO, 0x0F cyclic RO, 0x17 purse RO (all without key). */
    const BalanceDecoder* all[] = {suica, octopus};
    for(size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        uint8_t attribute = all[i]->service_code & 0x3F;
        CHECK(attribute == 0x0B || attribute == 0x0F || attribute == 0x17);
    }
}

static void test_suica_balance(void) {
    const BalanceDecoder* decoder = balance_decoder_find(0x0003);
    uint8_t block[16] = {0};
    int32_t value = -1;
    char text[BALANCE_STR_SIZE];

    block[11] = 0x10; /* 0x2710 = 10000, little-endian */
    block[12] = 0x27;
    CHECK(decoder->decode(block, &value) && value == 10000);
    balance_format(decoder, value, text, sizeof(text));
    CHECK(strcmp(text, "10000 JPY") == 0);

    block[11] = 0xFF;
    block[12] = 0xFF;
    CHECK(decoder->decode(block, &value) && value == 65535);

    memset(block, 0xAA, sizeof(block));
    block[11] = 0x00;
    block[12] = 0x00;
    CHECK(decoder->decode(block, &value) && value == 0);
    balance_format(decoder, value, text, sizeof(text));
    CHECK(strcmp(text, "0 JPY") == 0);
}

static void test_octopus_balance(void) {
    const BalanceDecoder* decoder = balance_decoder_find(0x8008);
    uint8_t block[16] = {0};
    int32_t value = 0;
    char text[BALANCE_STR_SIZE];

    /* Raw 0x00000294 = 660 -> (660 - 500) / 10 = HK$16.0 */
    block[2] = 0x02;
    block[3] = 0x94;
    CHECK(decoder->decode(block, &value) && value == 160);
    balance_format(decoder, value, text, sizeof(text));
    CHECK(strcmp(text, "16.0 HKD") == 0);

    /* Raw 500 -> HK$0.0 */
    block[2] = 0x01;
    block[3] = 0xF4;
    CHECK(decoder->decode(block, &value) && value == 0);
    balance_format(decoder, value, text, sizeof(text));
    CHECK(strcmp(text, "0.0 HKD") == 0);

    /* Raw 485 -> -HK$1.5 (Octopus allows a negative balance) */
    block[2] = 0x01;
    block[3] = 0xE5;
    CHECK(decoder->decode(block, &value) && value == -15);
    balance_format(decoder, value, text, sizeof(text));
    CHECK(strcmp(text, "-1.5 HKD") == 0);

    /* Raw 0 -> -HK$50.0 */
    memset(block, 0, sizeof(block));
    CHECK(decoder->decode(block, &value) && value == -500);
    balance_format(decoder, value, text, sizeof(text));
    CHECK(strcmp(text, "-50.0 HKD") == 0);

    /* Raw 123456 -> HK$12295.6 */
    block[1] = 0x01;
    block[2] = 0xE2;
    block[3] = 0x40;
    CHECK(decoder->decode(block, &value) && value == 122956);
    balance_format(decoder, value, text, sizeof(text));
    CHECK(strcmp(text, "12295.6 HKD") == 0);

    /* Values with the top bit set cannot be a real purse. */
    memset(block, 0, sizeof(block));
    block[0] = 0x80;
    CHECK(!decoder->decode(block, &value));
}

static void test_balance_format_edges(void) {
    char text[BALANCE_STR_SIZE] = "untouched";
    balance_format(NULL, 5, text, sizeof(text));
    CHECK(text[0] == '\0');

    const BalanceDecoder* octopus = balance_decoder_find(0x8008);
    char small[6];
    balance_format(octopus, 160, small, sizeof(small));
    CHECK(strlen(small) == sizeof(small) - 1); /* Truncated, still terminated */

    balance_format(octopus, INT32_MIN, text, sizeof(text));
    CHECK(strcmp(text, "-214748364.8 HKD") == 0);
}

/* --- ECP ----------------------------------------------------------------- */

static void test_ecp_frame(void) {
    uint8_t frame[ECP_FRAME_PAYMENT_LEN];

    /* Published TfL capture, with ECP v2 header/config and without CRC. */
    static const uint8_t payment[] = {
        0x6A, 0x02, 0xC8, 0x01, 0x00, 0x03, 0x00, 0x02, 0x79, 0, 0, 0, 0};
    CHECK(ecp_frame_build_payment(frame, sizeof(frame)) == ECP_FRAME_PAYMENT_LEN);
    CHECK(ECP_FRAME_PAYMENT_LEN == sizeof(payment));
    CHECK(memcmp(frame, payment, sizeof(payment)) == 0);

    /* Buffer-size guards. */
    CHECK(ecp_frame_build_payment(frame, ECP_FRAME_PAYMENT_LEN - 1) == 0);
    CHECK(ecp_frame_build_payment(NULL, sizeof(frame)) == 0);
}

int main(void) {
    test_build_polling();
    test_build_read();
    test_build_request_service();
    test_parse_polling();
    test_parse_request_service();
    test_parse_read();
    test_pmm_timeout();
    test_system_codes();
    test_decoder_table();
    test_suica_balance();
    test_octopus_balance();
    test_balance_format_edges();
    test_ecp_frame();

    if(failures) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return EXIT_FAILURE;
    }
    printf("All %d checks passed\n", checks);
    return EXIT_SUCCESS;
}
