#include "noise_framing.h"

#include <stdlib.h>
#include <string.h>

static void clear_assembly(MuseNoiseFrameAssembly *assembly) {
    if (assembly == NULL) return;
    for (size_t i = 0; i < MUSE_LINK_MAX_NOISE_CHUNKS; i++) {
        free(assembly->chunks[i]);
        assembly->chunks[i] = NULL;
    }
    memset(assembly, 0, sizeof(*assembly));
}

static void expire_assemblies(MuseNoiseFrameDecoder *decoder, uint64_t now_us) {
    for (size_t i = 0; i < MUSE_NOISE_MAX_PENDING_ASSEMBLIES; i++) {
        MuseNoiseFrameAssembly *assembly = &decoder->assemblies[i];
        if (assembly->active && now_us - assembly->last_update_us > MUSE_NOISE_ASSEMBLY_TTL_US)
            clear_assembly(assembly);
    }
}

static MuseNoiseFrameAssembly *find_assembly(MuseNoiseFrameDecoder *decoder, int64_t chunk_id) {
    for (size_t i = 0; i < MUSE_NOISE_MAX_PENDING_ASSEMBLIES; i++) {
        if (decoder->assemblies[i].active && decoder->assemblies[i].chunk_id == chunk_id)
            return &decoder->assemblies[i];
    }
    return NULL;
}

static MuseNoiseFrameAssembly *new_assembly(MuseNoiseFrameDecoder *decoder,
                                             int64_t chunk_id, uint32_t total_chunks,
                                             uint64_t now_us) {
    for (size_t i = 0; i < MUSE_NOISE_MAX_PENDING_ASSEMBLIES; i++) {
        MuseNoiseFrameAssembly *assembly = &decoder->assemblies[i];
        if (!assembly->active) {
            memset(assembly, 0, sizeof(*assembly));
            assembly->active = true;
            assembly->chunk_id = chunk_id;
            assembly->total_chunks = total_chunks;
            assembly->last_update_us = now_us;
            return assembly;
        }
    }
    return NULL;
}

void muse_noise_frame_decoder_init(MuseNoiseFrameDecoder *decoder) {
    if (decoder != NULL) memset(decoder, 0, sizeof(*decoder));
}

void muse_noise_frame_decoder_free(MuseNoiseFrameDecoder *decoder) {
    if (decoder == NULL) return;
    for (size_t i = 0; i < MUSE_NOISE_MAX_PENDING_ASSEMBLIES; i++)
        clear_assembly(&decoder->assemblies[i]);
}

MuseNoiseFrameResult muse_noise_frame_decoder_feed(
    MuseNoiseFrameDecoder *decoder, int64_t chunk_id, uint32_t chunk_index,
    uint32_t total_chunks, const uint8_t *payload, size_t payload_size,
    uint64_t now_us, uint8_t *output, size_t output_capacity, size_t *output_size) {
    MuseNoiseFrameAssembly *assembly;
    if (decoder == NULL || output == NULL || output_size == NULL ||
        (payload == NULL && payload_size != 0) || total_chunks == 0 ||
        total_chunks > MUSE_LINK_MAX_NOISE_CHUNKS || chunk_index >= total_chunks ||
        payload_size > MUSE_LINK_MAX_NOISE_CHUNK_BYTES) return MUSE_NOISE_FRAME_INVALID;
    *output_size = 0;
    if (total_chunks == 1) {
        if (payload_size > output_capacity) return MUSE_NOISE_FRAME_OUTPUT_TOO_SMALL;
        if (payload_size != 0) memcpy(output, payload, payload_size);
        *output_size = payload_size;
        return MUSE_NOISE_FRAME_COMPLETE;
    }
    expire_assemblies(decoder, now_us);
    assembly = find_assembly(decoder, chunk_id);
    if (assembly == NULL) assembly = new_assembly(decoder, chunk_id, total_chunks, now_us);
    if (assembly == NULL) return MUSE_NOISE_FRAME_INVALID;
    if (assembly->total_chunks != total_chunks) {
        clear_assembly(assembly);
        return MUSE_NOISE_FRAME_INVALID;
    }
    if ((assembly->seen[chunk_index / 8u] & (uint8_t)(1u << (chunk_index % 8u))) != 0 ||
        payload_size > MUSE_NOISE_ASSEMBLY_MAX_BYTES - assembly->total_bytes) {
        clear_assembly(assembly);
        return MUSE_NOISE_FRAME_INVALID;
    }
    if (payload_size != 0) {
        assembly->chunks[chunk_index] = malloc(payload_size);
        if (assembly->chunks[chunk_index] == NULL) {
            clear_assembly(assembly);
            return MUSE_NOISE_FRAME_OUT_OF_MEMORY;
        }
        memcpy(assembly->chunks[chunk_index], payload, payload_size);
    }
    assembly->chunk_sizes[chunk_index] = payload_size;
    assembly->seen[chunk_index / 8u] |= (uint8_t)(1u << (chunk_index % 8u));
    assembly->received_chunks++;
    assembly->total_bytes += payload_size;
    assembly->last_update_us = now_us;
    if (assembly->received_chunks != assembly->total_chunks) return MUSE_NOISE_FRAME_NEED_MORE;
    if (assembly->total_bytes > output_capacity) {
        clear_assembly(assembly);
        return MUSE_NOISE_FRAME_OUTPUT_TOO_SMALL;
    }
    {
        size_t offset = 0;
        for (uint32_t i = 0; i < assembly->total_chunks; i++) {
            size_t chunk_size = assembly->chunk_sizes[i];
            if (chunk_size != 0 && assembly->chunks[i] == NULL) {
                clear_assembly(assembly);
                return MUSE_NOISE_FRAME_INVALID;
            }
            if (chunk_size != 0) memcpy(output + offset, assembly->chunks[i], chunk_size);
            offset += chunk_size;
        }
        *output_size = offset;
    }
    clear_assembly(assembly);
    return MUSE_NOISE_FRAME_COMPLETE;
}
