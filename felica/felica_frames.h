/**
 * @file felica_frames.h
 * @brief Pure FeliCa (NFC-F) command builders and response parsers.
 *
 * No Flipper dependencies: this file is compiled both into the .fap and into
 * the host unit tests. Frames include the leading LEN byte but NOT the CRC;
 * the CRC is appended/checked by the transport layer.
 *
 * Only read-only commands are implemented, deliberately:
 *   0x00 Polling, 0x02 Request Service, 0x06 Read Without Encryption.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FELICA_FRAME_IDM_LEN   8U
#define FELICA_FRAME_PMM_LEN   8U
#define FELICA_FRAME_BLOCK_LEN 16U

#define FELICA_FRAME_CMD_POLLING          0x00U
#define FELICA_FRAME_RSP_POLLING          0x01U
#define FELICA_FRAME_CMD_REQUEST_SERVICE  0x02U
#define FELICA_FRAME_RSP_REQUEST_SERVICE  0x03U
#define FELICA_FRAME_CMD_READ_WITHOUT_ENC 0x06U
#define FELICA_FRAME_RSP_READ_WITHOUT_ENC 0x07U

/** Polling request codes (RC). */
#define FELICA_FRAME_RC_NONE        0x00U
#define FELICA_FRAME_RC_SYSTEM_CODE 0x01U

/** Polling time slot number for a single slot. */
#define FELICA_FRAME_TSN_1 0x00U

/** Key version reported by Request Service for a non-existent node. */
#define FELICA_FRAME_KEY_VERSION_MISSING 0xFFFFU

/** PMm byte indices holding maximum response time parameters. */
#define FELICA_FRAME_PMM_REQUEST_SERVICE 2U
#define FELICA_FRAME_PMM_READ            5U

#define FELICA_FRAME_POLLING_LEN         6U
#define FELICA_FRAME_REQUEST_SERVICE_LEN 13U
#define FELICA_FRAME_READ_ONE_BLOCK_LEN  16U

typedef enum {
    FelicaFrameOk,
    FelicaFrameErrorLength, /**< Frame shorter than required or LEN byte mismatch. */
    FelicaFrameErrorResponseCode, /**< Unexpected response command code. */
    FelicaFrameErrorIdm, /**< Response IDm differs from the addressed card. */
    FelicaFrameErrorStatus, /**< Card reported non-zero status flags. */
} FelicaFrameResult;

typedef struct {
    uint8_t idm[FELICA_FRAME_IDM_LEN];
    uint8_t pmm[FELICA_FRAME_PMM_LEN];
    bool has_system_code; /**< True when the card returned request data (RC=0x01). */
    uint16_t system_code;
} FelicaPollingResponse;

/** Build a Polling (SENSF_REQ) frame. Returns frame length, 0 if out is too small. */
size_t felica_frame_build_polling(
    uint8_t* out,
    size_t out_size,
    uint16_t system_code,
    uint8_t request_code,
    uint8_t time_slot);

/** Parse a Polling response (SENSF_RES), CRC already stripped. */
FelicaFrameResult
    felica_frame_parse_polling(const uint8_t* frame, size_t len, FelicaPollingResponse* out);

/** Build a Request Service frame for a single service code. */
size_t felica_frame_build_request_service(
    uint8_t* out,
    size_t out_size,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint16_t service_code);

/** Parse a Request Service response for a single node; outputs its key version. */
FelicaFrameResult felica_frame_parse_request_service(
    const uint8_t* frame,
    size_t len,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint16_t* key_version);

/** Build a Read Without Encryption frame for one block of one service. */
size_t felica_frame_build_read_one_block(
    uint8_t* out,
    size_t out_size,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint16_t service_code,
    uint8_t block_number);

/**
 * Parse a Read Without Encryption response holding one block.
 *
 * Status flags are always written when the header is long enough, so callers
 * can report them on FelicaFrameErrorStatus.
 */
FelicaFrameResult felica_frame_parse_read_one_block(
    const uint8_t* frame,
    size_t len,
    const uint8_t idm[FELICA_FRAME_IDM_LEN],
    uint8_t* status1,
    uint8_t* status2,
    uint8_t block[FELICA_FRAME_BLOCK_LEN]);

/**
 * Maximum response time announced in PMm, in 13.56 MHz carrier cycles.
 *
 * T = (256 * 16 / fc) * ((B + 1) * n + (A + 1)) * 4^E, where the PMm byte is
 * laid out as E[7:6] B[5:3] A[2:0] and n is the number of blocks/nodes.
 */
uint32_t
    felica_frame_pmm_timeout_fc(const uint8_t pmm[FELICA_FRAME_PMM_LEN], size_t index, uint8_t n);
