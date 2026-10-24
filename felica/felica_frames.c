#include "felica_frames.h"

#include <string.h>

#define FELICA_PMM_TIME_UNIT_FC (256U * 16U)

/* SENSF_RES: LEN, 0x01, IDm[8], PMm[8] (+ request data[2]). */
#define POLLING_RSP_MIN_LEN     18U
#define POLLING_RSP_WITH_RD     20U
/* LEN, 0x03, IDm[8], node count, key version[2]. */
#define REQUEST_SERVICE_RSP_LEN 13U
/* LEN, 0x07, IDm[8], SF1, SF2 (+ block count, data). */
#define READ_RSP_STATUS_LEN     12U
#define READ_RSP_ONE_BLOCK_LEN  (13U + FELICA_FRAME_BLOCK_LEN)

static bool frame_header_ok(const uint8_t* frame, size_t len, size_t min_len) {
    return frame && len >= min_len && frame[0] == len;
}

size_t felica_frame_build_polling(
    uint8_t* out,
    size_t out_size,
    uint16_t system_code,
    uint8_t request_code,
    uint8_t time_slot) {
    if(!out || out_size < FELICA_FRAME_POLLING_LEN) return 0;

    out[0] = FELICA_FRAME_POLLING_LEN;
    out[1] = FELICA_FRAME_CMD_POLLING;
    /* System code is big-endian on the wire in Polling (unlike service codes). */
    out[2] = (uint8_t)(system_code >> 8);
    out[3] = (uint8_t)system_code;
    out[4] = request_code;
    out[5] = time_slot;
    return FELICA_FRAME_POLLING_LEN;
}

FelicaFrameResult
    felica_frame_parse_polling(const uint8_t* frame, size_t len, FelicaPollingResponse* out) {
    if(!out || !frame_header_ok(frame, len, POLLING_RSP_MIN_LEN)) return FelicaFrameErrorLength;
    if(frame[1] != FELICA_FRAME_RSP_POLLING) return FelicaFrameErrorResponseCode;

    memcpy(out->idm, frame + 2, FELICA_FRAME_IDM_LEN);
    memcpy(out->pmm, frame + 2 + FELICA_FRAME_IDM_LEN, FELICA_FRAME_PMM_LEN);
    out->has_system_code = len >= POLLING_RSP_WITH_RD;
    out->system_code = out->has_system_code ? (uint16_t)((frame[18] << 8) | frame[19]) : 0;
    return FelicaFrameOk;
}

size_t felica_frame_build_request_service(
    uint8_t* out,
    size_t out_size,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint16_t service_code) {
    if(!out || !idm || out_size < FELICA_FRAME_REQUEST_SERVICE_LEN) return 0;

    out[0] = FELICA_FRAME_REQUEST_SERVICE_LEN;
    out[1] = FELICA_FRAME_CMD_REQUEST_SERVICE;
    memcpy(out + 2, idm, FELICA_FRAME_IDM_LEN);
    out[10] = 1; /* Number of nodes */
    out[11] = (uint8_t)service_code; /* Node codes are little-endian */
    out[12] = (uint8_t)(service_code >> 8);
    return FELICA_FRAME_REQUEST_SERVICE_LEN;
}

FelicaFrameResult felica_frame_parse_request_service(
    const uint8_t* frame,
    size_t len,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint16_t* key_version) {
    if(!key_version || !frame_header_ok(frame, len, REQUEST_SERVICE_RSP_LEN))
        return FelicaFrameErrorLength;
    if(frame[1] != FELICA_FRAME_RSP_REQUEST_SERVICE) return FelicaFrameErrorResponseCode;
    if(memcmp(frame + 2, idm, FELICA_FRAME_IDM_LEN) != 0) return FelicaFrameErrorIdm;
    if(frame[10] != 1 || len != REQUEST_SERVICE_RSP_LEN) return FelicaFrameErrorLength;

    *key_version = (uint16_t)(frame[11] | (frame[12] << 8));
    return FelicaFrameOk;
}

size_t felica_frame_build_read_one_block(
    uint8_t* out,
    size_t out_size,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint16_t service_code,
    uint8_t block_number) {
    if(!out || !idm || out_size < FELICA_FRAME_READ_ONE_BLOCK_LEN) return 0;

    out[0] = FELICA_FRAME_READ_ONE_BLOCK_LEN;
    out[1] = FELICA_FRAME_CMD_READ_WITHOUT_ENC;
    memcpy(out + 2, idm, FELICA_FRAME_IDM_LEN);
    out[10] = 1; /* Number of services */
    out[11] = (uint8_t)service_code; /* Service code, little-endian */
    out[12] = (uint8_t)(service_code >> 8);
    out[13] = 1; /* Number of blocks */
    out[14] = 0x80; /* 2-byte block list element, service list index 0 */
    out[15] = block_number;
    return FELICA_FRAME_READ_ONE_BLOCK_LEN;
}

FelicaFrameResult felica_frame_parse_read_one_block(
    const uint8_t* frame,
    size_t len,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint8_t* status1,
    uint8_t* status2,
    uint8_t block[FELICA_FRAME_BLOCK_LEN]) {
    if(!status1 || !status2 || !block || !frame_header_ok(frame, len, READ_RSP_STATUS_LEN))
        return FelicaFrameErrorLength;
    if(frame[1] != FELICA_FRAME_RSP_READ_WITHOUT_ENC) return FelicaFrameErrorResponseCode;
    if(memcmp(frame + 2, idm, FELICA_FRAME_IDM_LEN) != 0) return FelicaFrameErrorIdm;

    *status1 = frame[10];
    *status2 = frame[11];
    if(*status1 != 0x00) return FelicaFrameErrorStatus;
    if(len != READ_RSP_ONE_BLOCK_LEN || frame[12] != 1) return FelicaFrameErrorLength;

    memcpy(block, frame + 13, FELICA_FRAME_BLOCK_LEN);
    return FelicaFrameOk;
}

uint32_t
    felica_frame_pmm_timeout_fc(const uint8_t pmm[FELICA_FRAME_PMM_LEN], size_t index, uint8_t n) {
    if(!pmm || index >= FELICA_FRAME_PMM_LEN) return 0;

    const uint8_t param = pmm[index];
    const uint32_t a = param & 0x07U;
    const uint32_t b = (param >> 3) & 0x07U;
    const uint32_t e = (param >> 6) & 0x03U;
    /* Worst case: 4096 * (8 * 255 + 8) * 64 ~= 537M fc, fits in uint32_t. */
    return FELICA_PMM_TIME_UNIT_FC * ((b + 1U) * n + a + 1U) * (1UL << (2U * e));
}
