#include "noise_framing.h"

#include <assert.h>
#include <string.h>

int main(void) {
    MuseNoiseFrameDecoder decoder;
    uint8_t output[32];
    size_t output_size = 0;

    muse_noise_frame_decoder_init(&decoder);
    assert(muse_noise_frame_decoder_feed(&decoder, 7, 1, 3, (const uint8_t *)"B", 1,
                                         10, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_NEED_MORE);
    assert(muse_noise_frame_decoder_feed(&decoder, 8, 0, 2, (const uint8_t *)"x", 1,
                                         11, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_NEED_MORE);
    assert(muse_noise_frame_decoder_feed(&decoder, 7, 0, 3, (const uint8_t *)"A", 1,
                                         12, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_NEED_MORE);
    assert(muse_noise_frame_decoder_feed(&decoder, 8, 1, 2, (const uint8_t *)"y", 1,
                                         13, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_COMPLETE);
    assert(output_size == 2 && memcmp(output, "xy", 2) == 0);
    assert(muse_noise_frame_decoder_feed(&decoder, 7, 2, 3, (const uint8_t *)"C", 1,
                                         14, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_COMPLETE);
    assert(output_size == 3 && memcmp(output, "ABC", 3) == 0);

    assert(muse_noise_frame_decoder_feed(&decoder, 9, 0, 2, (const uint8_t *)"A", 1,
                                         20, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_NEED_MORE);
    assert(muse_noise_frame_decoder_feed(&decoder, 9, 0, 2, (const uint8_t *)"A", 1,
                                         21, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_INVALID);

    assert(muse_noise_frame_decoder_feed(&decoder, 10, 0, 2, (const uint8_t *)"A", 1,
                                         30, output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_NEED_MORE);
    assert(muse_noise_frame_decoder_feed(&decoder, 11, 0, 2, (const uint8_t *)"B", 1,
                                         30 + MUSE_NOISE_ASSEMBLY_TTL_US + 1,
                                         output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_NEED_MORE);
    assert(muse_noise_frame_decoder_feed(&decoder, 10, 1, 2, (const uint8_t *)"C", 1,
                                         30 + MUSE_NOISE_ASSEMBLY_TTL_US + 2,
                                         output, sizeof(output), &output_size) ==
           MUSE_NOISE_FRAME_NEED_MORE);

    muse_noise_frame_decoder_free(&decoder);
    return 0;
}
