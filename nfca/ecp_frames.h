/**
 * @file ecp_frames.h
 * @brief Apple Enhanced Contactless Polling (ECP) frame builder.
 *
 * ECP is a short frame Apple devices broadcast during their NFC-A polling loop
 * to tell a nearby iPhone/Apple Watch which kind of pass to present. It is
 * documented at https://github.com/kormax/apple-enhanced-contactless-polling.
 *
 * This app sends only the generic payment/VAS frame (ECP v1, TCI 00 00 00),
 * which asks the device to present its Express payment card. ECP is NOT
 * answered by the device; it responds to the regular NFC-A poll that follows.
 *
 * Pure C: no Flipper dependencies, shared with the host unit tests. The frame
 * is built WITHOUT the trailing CRC_A, which the transport layer appends (the
 * same convention as felica_frames.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ECP_FRAME_HEADER    0x6AU
#define ECP_FRAME_VERSION_1 0x01U

/* 6A 01 00 00 00 -> generic "VAS or payment" ECP v1 frame. */
#define ECP_FRAME_PAYMENT_LEN 5U

/**
 * Build the generic payment/VAS ECP v1 frame (without CRC_A).
 *
 * @return the frame length, or 0 if the buffer is too small.
 */
size_t ecp_frame_build_payment(uint8_t* out, size_t out_size);
