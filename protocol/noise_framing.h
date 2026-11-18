/* Shared bounded reassembly for the official Muse NoiseTransportFrame wire. */
#ifndef MUSE_NOISE_FRAMING_H
#define MUSE_NOISE_FRAMING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_wire.h"

#define MUSE_NOISE_MAX_PENDING_ASSEMBLIES 4u
#define MUSE_NOISE_ASSEMBLY_MAX_BYTES (2u * 1024u * 1024u)
#define MUSE_NOISE_ASSEMBLY_TTL_US (60ULL * 1000ULL * 1000ULL)

typedef struct MuseNoiseFrameAssembly {
    bool active;
    int64_t chunk_id;
    uint32_t total_chunks;
    uint32_t received_chunks;
    size_t total_bytes;
    uint64_t last_update_us;
    uint8_t seen[(MUSE_LINK_MAX_NOISE_CHUNKS + 7u) / 8u];
    uint8_t *chunks[MUSE_LINK_MAX_NOISE_CHUNKS];
    size_t chunk_sizes[MUSE_LINK_MAX_NOISE_CHUNKS];
} MuseNoiseFrameAssembly;

typedef struct MuseNoiseFrameDecoder {
    MuseNoiseFrameAssembly assemblies[MUSE_NOISE_MAX_PENDING_ASSEMBLIES];
} MuseNoiseFrameDecoder;

typedef enum MuseNoiseFrameResult {
    MUSE_NOISE_FRAME_NEED_MORE = 0,
    MUSE_NOISE_FRAME_COMPLETE = 1,
    MUSE_NOISE_FRAME_INVALID = -1,
    MUSE_NOISE_FRAME_OUTPUT_TOO_SMALL = -2,
    MUSE_NOISE_FRAME_OUT_OF_MEMORY = -3,
} MuseNoiseFrameResult;

void muse_noise_frame_decoder_init(MuseNoiseFrameDecoder *decoder);
void muse_noise_frame_decoder_free(MuseNoiseFrameDecoder *decoder);

/* Feed one already-decoded NoiseTransportFrame. The timestamp is monotonic
   microseconds; output is written only when the complete payload is ready. */
MuseNoiseFrameResult muse_noise_frame_decoder_feed(
    MuseNoiseFrameDecoder *decoder, int64_t chunk_id, uint32_t chunk_index,
    uint32_t total_chunks, const uint8_t *payload, size_t payload_size,
    uint64_t now_us, uint8_t *output, size_t output_capacity, size_t *output_size);

#endif
