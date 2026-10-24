#include "ecp_frames.h"

#include <string.h>

size_t ecp_frame_build_payment(uint8_t* out, size_t out_size) {
    if(!out || out_size < ECP_FRAME_PAYMENT_LEN) return 0;
    /* Published TfL capture: kormax/apple-enhanced-contactless-polling#configuration-examples. */
    static const uint8_t frame[] = {
        0x6A, 0x02, 0xC8, 0x01, 0x00, 0x03, 0x00, 0x02, 0x79, 0x00, 0x00, 0x00, 0x00};
    memcpy(out, frame, sizeof(frame));
    return sizeof(frame);
}
