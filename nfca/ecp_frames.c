#include "ecp_frames.h"

size_t ecp_frame_build_payment(uint8_t* out, size_t out_size) {
    if(!out || out_size < ECP_FRAME_PAYMENT_LEN) return 0;

    out[0] = ECP_FRAME_HEADER;
    out[1] = ECP_FRAME_VERSION_1;
    /* TCI 00 00 00: VAS or payment. The device presents its Express card. */
    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = 0x00;
    return ECP_FRAME_PAYMENT_LEN;
}
